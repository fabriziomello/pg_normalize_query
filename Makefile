MODULES = pg_normalize_query

REGRESS = pg_normalize_query

EXTENSION = pg_normalize_query
DATA = pg_normalize_query--1.0.sql
PGFILEDESC = "pg_normalize_query - PostgreSQL extension to normalize a SQL query similar to pg_stat_statement"

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
