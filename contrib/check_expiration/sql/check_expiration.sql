-- Test check_expiration extension
CREATE EXTENSION check_expiration;

-- Test that the function exists and returns boolean
SELECT check_expiration_date();

-- Test that non-superuser cannot call it
SET ROLE pg_monitor;
SELECT check_expiration_date();
RESET ROLE;

DROP EXTENSION check_expiration;
