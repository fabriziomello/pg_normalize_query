\set VERBOSITY terse
CREATE EXTENSION pg_normalize_query;
SELECT pg_normalize_query($$SELECT * FROM pg_proc WHERE proname = 'pg_normalize_query'$$);
SELECT pg_normalize_query($$SELECT oid, relname, relkind FROM pg_class WHERE relkind IN ('r', 'i') LIMIT 10$$);
SELECT pg_normalize_query($$SELECT
	n.nspname, r.relname, r.relkind
FROM
	pg_namespace n
	JOIN pg_class r ON r.relnamespace = n.oid AND r.relkind = 'r'
WHERE
	n.nspname ~ '^pg_catalog'$$);
SELECT pg_normalize_query('SELECT * FROM pg_attribute WHERE attname = $$relkind$$');
SELECT pg_normalize_query($$UPDATE foo SET f1=now(), f2='xxx', f3=current_timestamp WHERE id=1 AND f1 > now() - interval '1 day'$$);
SELECT pg_normalize_query($$DELETE FROM foo WHERE id IN (SELECT id FROM bar WHERE f1 <= now() - '1 week'::interval)$$);
SELECT pg_normalize_query($$SELECT * FROM foo WHERE$$); -- Should fail due to syntax error
