/* contrib/check_expiration/check_expiration--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION check_expiration" to load this file. \quit

CREATE FUNCTION check_expiration_date()
RETURNS boolean
AS 'MODULE_PATHNAME', 'check_expiration_date'
LANGUAGE C STRICT EXECUTE ON COORDINATOR;

COMMENT ON FUNCTION check_expiration_date()
IS 'Request postmaster to check database expiration date';
