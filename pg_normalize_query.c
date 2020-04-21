#include "postgres.h"

#include "funcapi.h"
#include "nodes/nodeFuncs.h"
#include "parser/analyze.h"
#include "parser/parser.h"
#include "parser/parsetree.h"
#include "parser/scanner.h"
#include "parser/scansup.h"

#include "utils/builtins.h"

PG_MODULE_MAGIC;

/* Define scanner_init parameters following the PostgreSQL versions */
#if PG_VERSION_NUM >= 120000
#define PGNQ_SCANNER_INIT_ARGS query, &yyextra, &ScanKeywords, ScanKeywordTokens
#else
#define PGNQ_SCANNER_INIT_ARGS query, &yyextra, ScanKeywords, NumScanKeywords
#endif

/*
 * Struct for tracking locations/lengths of constants during normalization
 */
typedef struct pgnqLocationLen
{
	int			location;		/* start offset in query text */
	int			length;			/* length in bytes, or -1 to ignore */
} pgnqLocationLen;

/*
 * Working state for constant tree walker
 */
typedef struct pgnqConstLocations
{
	/* Array of locations of constants that should be removed */
	pgnqLocationLen *clocations;

	/* Allocated length of clocations array */
	int			clocations_buf_size;

	/* Current number of valid entries in clocations array */
	int			clocations_count;

	/* highest Param id we've seen, in order to start normalization correctly */
	int			highest_extern_param_id;
} pgnqConstLocations;

static int pgnq_comp_location(const void *a, const void *b);
static void pgnq_fill_in_constant_lengths(pgnqConstLocations *jstate, const char *query);
static char *pgnq_build_normalized_query(pgnqConstLocations *jstate, const char *query,
						  int query_loc, int *query_len_p);
static void pgnq_record_const_location(pgnqConstLocations *jstate, int location);
static bool pgnq_const_record_walker(Node *node, pgnqConstLocations *jstate);

PG_FUNCTION_INFO_V1(pg_normalize_query);

Datum
pg_normalize_query(PG_FUNCTION_ARGS)
{
	text *sql_t = PG_GETARG_TEXT_P(0);
	text *out_t;
	char *sql, *out;
	List *tree;
	pgnqConstLocations jstate;
	int query_len;

	/* Parse query */
	sql = text_to_cstring(sql_t);
	tree = raw_parser(sql);

	/* Set up workspace for constant recording */
	jstate.clocations_buf_size = 32;
	jstate.clocations = (pgnqLocationLen *)
		palloc(jstate.clocations_buf_size * sizeof(pgnqLocationLen));
	jstate.clocations_count = 0;
	jstate.highest_extern_param_id = 0;

	/* Walk tree and record const locations */
	pgnq_const_record_walker((Node *) tree, &jstate);

	/* Normalize query */
	query_len = (int) strlen(sql);
	out = strdup(pgnq_build_normalized_query(&jstate, sql, 0, &query_len));
	out_t = cstring_to_text(out);
	
	PG_RETURN_TEXT_P(out_t);
}

/*
 * pgnq_comp_location: comparator for qsorting pgnqLocationLen structs by location
 */
static int
pgnq_comp_location(const void *a, const void *b)
{
	int			l = ((const pgnqLocationLen *) a)->location;
	int			r = ((const pgnqLocationLen *) b)->location;

	if (l < r)
		return -1;
	else if (l > r)
		return +1;
	else
		return 0;
}

/*
 * Given a valid SQL string and an array of constant-location records,
 * fill in the textual lengths of those constants.
 *
 * The constants may use any allowed constant syntax, such as float literals,
 * bit-strings, single-quoted strings and dollar-quoted strings.  This is
 * accomplished by using the public API for the core scanner.
 *
 * It is the caller's job to ensure that the string is a valid SQL statement
 * with constants at the indicated locations.  Since in practice the string
 * has already been parsed, and the locations that the caller provides will
 * have originated from within the authoritative parser, this should not be
 * a problem.
 *
 * Duplicate constant pointers are possible, and will have their lengths
 * marked as '-1', so that they are later ignored.  (Actually, we assume the
 * lengths were initialized as -1 to start with, and don't change them here.)
 *
 * N.B. There is an assumption that a '-' character at a Const location begins
 * a negative numeric constant.  This precludes there ever being another
 * reason for a constant to start with a '-'.
 */
static void
pgnq_fill_in_constant_lengths(pgnqConstLocations *jstate, const char *query)
{
	pgnqLocationLen *locs;
	core_yyscan_t yyscanner;
	core_yy_extra_type yyextra;
	core_YYSTYPE yylval;
	YYLTYPE		yylloc;
	int			last_loc = -1;
	int			i;

	/*
	 * Sort the records by location so that we can process them in order while
	 * scanning the query text.
	 */
	if (jstate->clocations_count > 1)
		qsort(jstate->clocations, jstate->clocations_count,
			  sizeof(pgnqLocationLen), pgnq_comp_location);
	locs = jstate->clocations;

	/* initialize the flex scanner --- should match raw_parser() */
	yyscanner = scanner_init(PGNQ_SCANNER_INIT_ARGS);

	/* Search for each constant, in sequence */
	for (i = 0; i < jstate->clocations_count; i++)
	{
		int			loc = locs[i].location;
		int			tok;

		Assert(loc >= 0);

		if (loc <= last_loc)
			continue;			/* Duplicate constant, ignore */

		/* Lex tokens until we find the desired constant */
		for (;;)
		{
			tok = core_yylex(&yylval, &yylloc, yyscanner);

			/* We should not hit end-of-string, but if we do, behave sanely */
			if (tok == 0)
				break;			/* out of inner for-loop */

			/*
			 * We should find the token position exactly, but if we somehow
			 * run past it, work with that.
			 */
			if (yylloc >= loc)
			{
				if (query[loc] == '-')
				{
					/*
					 * It's a negative value - this is the one and only case
					 * where we replace more than a single token.
					 *
					 * Do not compensate for the core system's special-case
					 * adjustment of location to that of the leading '-'
					 * operator in the event of a negative constant.  It is
					 * also useful for our purposes to start from the minus
					 * symbol.  In this way, queries like "select * from foo
					 * where bar = 1" and "select * from foo where bar = -2"
					 * will have identical normalized query strings.
					 */
					tok = core_yylex(&yylval, &yylloc, yyscanner);
					if (tok == 0)
						break;	/* out of inner for-loop */
				}

				/*
				 * We now rely on the assumption that flex has placed a zero
				 * byte after the text of the current token in scanbuf.
				 */
				locs[i].length = (int) strlen(yyextra.scanbuf + loc);

				/* Quoted string with Unicode escapes
				 *
				 * The lexer consumes trailing whitespace in order to find UESCAPE, but if there
				 * is no UESCAPE it has still consumed it - don't include it in constant length.
				 */
				if (locs[i].length > 4 && /* U&'' */
					(yyextra.scanbuf[loc] == 'u' || yyextra.scanbuf[loc] == 'U') &&
					 yyextra.scanbuf[loc + 1] == '&' && yyextra.scanbuf[loc + 2] == '\'')
				{
					int j = locs[i].length - 1; /* Skip the \0 */
					for (; j >= 0 && scanner_isspace(yyextra.scanbuf[loc + j]); j--) {}
					locs[i].length = j + 1; /* Count the \0 */
				}

				break;			/* out of inner for-loop */
			}
		}

		/* If we hit end-of-string, give up, leaving remaining lengths -1 */
		if (tok == 0)
			break;

		last_loc = loc;
	}

	scanner_finish(yyscanner);
}

/*
 * Generate a normalized version of the query string that will be used to
 * represent all similar queries.
 *
 * Note that the normalized representation may well vary depending on
 * just which "equivalent" query is used to create the hashtable entry.
 * We assume this is OK.
 *
 * *query_len_p contains the input string length, and is updated with
 * the result string length (which cannot be longer) on exit.
 *
 * Returns a palloc'd string.
 */
static char *
pgnq_build_normalized_query(pgnqConstLocations *jstate, const char *query,
						  int query_loc, int *query_len_p)
{
	char	   *norm_query;
	int			query_len = *query_len_p;
	int			i,
				norm_query_buflen,		/* Space allowed for norm_query */
				len_to_wrt,		/* Length (in bytes) to write */
				quer_loc = 0,	/* Source query byte location */
				n_quer_loc = 0, /* Normalized query byte location */
				last_off = 0,	/* Offset from start for previous tok */
				last_tok_len = 0;		/* Length (in bytes) of that tok */

	/*
	 * Get constants' lengths (core system only gives us locations).  Note
	 * this also ensures the items are sorted by location.
	 */
	pgnq_fill_in_constant_lengths(jstate, query);

	/*
	 * Allow for $n symbols to be longer than the constants they replace.
	 * Constants must take at least one byte in text form, while a $n symbol
	 * certainly isn't more than 11 bytes, even if n reaches INT_MAX.  We
	 * could refine that limit based on the max value of n for the current
	 * query, but it hardly seems worth any extra effort to do so.
	 */
	norm_query_buflen = query_len + jstate->clocations_count * 10;

	/* Allocate result buffer */
	norm_query = palloc(norm_query_buflen + 1);

	for (i = 0; i < jstate->clocations_count; i++)
	{
		int			off,		/* Offset from start for cur tok */
					tok_len;	/* Length (in bytes) of that tok */

		off = jstate->clocations[i].location;
		/* Adjust recorded location if we're dealing with partial string */
		off -= query_loc;

		tok_len = jstate->clocations[i].length;

		if (tok_len < 0)
			continue;			/* ignore any duplicates */

		/* Copy next chunk (what precedes the next constant) */
		len_to_wrt = off - last_off;
		len_to_wrt -= last_tok_len;

		Assert(len_to_wrt >= 0);
		memcpy(norm_query + n_quer_loc, query + quer_loc, len_to_wrt);
		n_quer_loc += len_to_wrt;

		/* And insert a param symbol in place of the constant token */
		n_quer_loc += sprintf(norm_query + n_quer_loc, "$%d",
							  i + 1 + jstate->highest_extern_param_id);

		quer_loc = off + tok_len;
		last_off = off;
		last_tok_len = tok_len;
	}

	/*
	 * We've copied up until the last ignorable constant.  Copy over the
	 * remaining bytes of the original query string.
	 */
	len_to_wrt = query_len - quer_loc;

	Assert(len_to_wrt >= 0);
	memcpy(norm_query + n_quer_loc, query + quer_loc, len_to_wrt);
	n_quer_loc += len_to_wrt;

	Assert(n_quer_loc <= norm_query_buflen);
	norm_query[n_quer_loc] = '\0';

	*query_len_p = n_quer_loc;
	return norm_query;
}

static void pgnq_record_const_location(pgnqConstLocations *jstate, int location)
{
	/* -1 indicates unknown or undefined location */
	if (location >= 0)
	{
		/* enlarge array if needed */
		if (jstate->clocations_count >= jstate->clocations_buf_size)
		{
			jstate->clocations_buf_size *= 2;
			jstate->clocations = (pgnqLocationLen *)
				repalloc(jstate->clocations,
						 jstate->clocations_buf_size *
						 sizeof(pgnqLocationLen));
		}
		jstate->clocations[jstate->clocations_count].location = location;
		/* initialize lengths to -1 to simplify pgnq_fill_in_constant_lengths */
		jstate->clocations[jstate->clocations_count].length = -1;
		jstate->clocations_count++;
	}
}

static bool pgnq_const_record_walker(Node *node, pgnqConstLocations *jstate)
{
	Node	*nodeReturn = NULL;

	if (node == NULL)
		return false;

	/* Guard against stack overflow due to overly complex expressions */
	check_stack_depth();

	switch (nodeTag(node))
	{
		case T_A_Const:
			pgnq_record_const_location(jstate, castNode(A_Const, node)->location);
			break;

		case T_ParamRef:
			if (((ParamRef *) node)->number > jstate->highest_extern_param_id)
				jstate->highest_extern_param_id = castNode(ParamRef, node)->number;
			break;

		case T_DefElem:
			nodeReturn = (Node *) ((DefElem *) node)->arg;
			break;

#if PG_VERSION_NUM >= 100000
		case T_RawStmt:
			nodeReturn = (Node *) ((RawStmt *) node)->stmt;
			break;
#endif

		case T_VariableSetStmt:
			nodeReturn = (Node *) ((VariableSetStmt *) node)->args;
			break;

		case T_CopyStmt:
			nodeReturn = (Node *) ((CopyStmt *) node)->query;
			break;

		case T_ExplainStmt:
			nodeReturn = (Node *) ((ExplainStmt *) node)->query;
			break;

		case T_AlterRoleStmt:
			nodeReturn = (Node *) ((AlterRoleStmt *) node)->options;
			break;

		case T_DeclareCursorStmt:
			nodeReturn = (Node *) ((DeclareCursorStmt *) node)->query;
			break;

		default:
			break;
	}

	if (nodeReturn != NULL)
		return pgnq_const_record_walker((Node *) nodeReturn, jstate);

	return raw_expression_tree_walker(node, pgnq_const_record_walker, (void*) jstate);
}
