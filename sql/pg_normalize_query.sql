\set VERBOSITY terse
CREATE EXTENSION pg_normalize_query;
SELECT pg_normalize_query($$SELECT * FROM pg_proc WHERE proname = 'pg_normalize_query'$$);
SELECT pg_normalize_query($$SELECT oid, relname, relkind FROM pg_class WHERE relkind IN ('r', 'i') LIMIT 10$$);