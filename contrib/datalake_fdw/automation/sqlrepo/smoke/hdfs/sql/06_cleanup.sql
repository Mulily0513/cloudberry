-- Test 06: Final Cleanup
-- Purpose: Clean up servers and resources created during the test session
-- Note: Runs last in REGRESS order to clean up objects from 01_connection.sql
-- Expected time: ~5s

-- Drop servers and cascading objects (created in 01_connection.sql)
DROP SERVER IF EXISTS hdfs_server CASCADE;
DROP SERVER IF EXISTS hive_server CASCADE;
DROP FOREIGN DATA WRAPPER IF EXISTS datalake_fdw CASCADE;

-- Verify cleanup
SELECT COUNT(*) AS remaining_servers
FROM pg_foreign_server
WHERE srvname IN ('hdfs_server', 'hive_server');
