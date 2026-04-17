-- hive_server_setup.sql
-- Hive server configuration for datalake_fdw tests
-- Requires: common_setup.sql to be loaded first
--
-- This file creates the hive_server foreign server using the public.create_foreign_server() function
-- Default configuration uses Docker service names from docker-compose.yml

-- Create server and user mapping for hive
-- Parameters: server_name, user, wrapper, cluster_name
-- The public.create_foreign_server() function handles all necessary setup
SELECT public.create_foreign_server('hive_server', 'gpadmin', 'datalake_fdw', 'paa_cluster');

-- Log server creation
SELECT test_log('Hive server setup completed');
