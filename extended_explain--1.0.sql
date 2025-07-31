/* extended_explain--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION extended_explain" to load this file. \quit

CREATE SCHEMA ee;

CREATE TABLE ee.result
(
	level integer,
	path_name bigint,
	path_type text,
	child_paths bigint[],
	startup_cost float,
	total_cost float,
	rows integer,
	is_del bool,
	rel_name text,
	indexoid oid
);

CREATE FUNCTION ee_func()
RETURNS SETOF RECORD
AS 'MODULE_PATHNAME', 'ee_func'
LANGUAGE C STRICT;