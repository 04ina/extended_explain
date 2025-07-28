/* extended_explain--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION extended_explain" to load this file. \quit

CREATE FUNCTION ee_func()
RETURNS SETOF RECORD
AS 'MODULE_PATHNAME', 'ee_func'
LANGUAGE C STRICT;