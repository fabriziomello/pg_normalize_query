# pg_normalize_query
PostgreSQL extension to normalize SQL statements similar to [pg_stat_statements](https://www.postgresql.org/docs/current/pgstatstatements.html).

This code was extracted from [libpg_query](https://github.com/lfittl/libpg_query/blob/10-latest/src/pg_query_normalize.c) and transformed into a PostgreSQL extension.

## Supported PostgreSQL versions

The aim of the project is support as many community-supported major versions of Postgres as possible. Currently, the following versions of PostgreSQL are supported:

9.4, 9.5, 9.6, 10, 11, 12 and master (pre-13).

## Installation

```sh
$ git clone https://github.com/fabriziomello/pg_normalize_query.git
$ cd pg_normalize_query
# Make sure your path includes the bin directory that contains the correct `pg_config`
$ PATH=/path/to/pg/bin:$PATH
$ USE_PGXS=1 make
$ USE_PGXS=1 make install
```

## Tests

```sh
$ USE_PGXS=1 make installcheck
```

## Examples

```
fabrizio=# CREATE EXTENSION pg_normalize_query;
CREATE EXTENSION
fabrizio=# \dx
                       List of installed extensions
        Name        | Version |   Schema   |         Description          
--------------------+---------+------------+------------------------------
 pg_normalize_query | 1.0     | public     | Normalize SQL Query
 plpgsql            | 1.0     | pg_catalog | PL/pgSQL procedural language
(2 rows)

fabrizio=# SELECT pg_normalize_query($$SELECT * FROM pg_proc WHERE proname = 'pg_normalize_query'$$);
            pg_normalize_query            
------------------------------------------
 SELECT * FROM pg_proc WHERE proname = $1
(1 row)

fabrizio=# SELECT pg_normalize_query($$SELECT oid, relname, relkind FROM pg_class WHERE relkind IN ('r', 'i') LIMIT 10$$);
                              pg_normalize_query                               
-------------------------------------------------------------------------------
 SELECT oid, relname, relkind FROM pg_class WHERE relkind IN ($1, $2) LIMIT $3
(1 row)
```
Please feel free to [open a PR](https://github.com/fabriziomello/pg_normalize_query/pull/new/master).

## Authors

- [Fabrízio de Royes Mello](mailto:fabriziomello@gmail.com)

## License

PostgreSQL server source code, used under the [PostgreSQL license](https://www.postgresql.org/about/licence/).<br>
Portions Copyright (c) 1996-2020, The PostgreSQL Global Development Group<br>
Portions Copyright (c) 1994, The Regents of the University of California

All other parts are licensed under the 3-clause BSD license, see LICENSE file for details.<br>
Copyright (c) 2020, Fabrízio de Royes Mello <fabriziomello@gmail.com>
