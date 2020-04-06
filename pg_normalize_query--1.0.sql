/* pg_normalize_quey/pg_normalize_query--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pg_normalize_query" to load this file. \quit

CREATE FUNCTION pg_normalize_query(query text)
RETURNS text
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;
