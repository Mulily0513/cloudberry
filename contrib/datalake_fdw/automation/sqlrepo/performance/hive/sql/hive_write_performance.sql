-- hive_write_performance.sql
-- Performance tests for Hive table writes (INSERT operations)
-- NOTE: This requires writable Hive tables

\i ../../../lib/sql/common_setup.sql
\i ../../../lib/sql/hive_server_setup.sql
\i ../../../lib/sql/performance_helpers.sql

-- Create test schema
DROP SCHEMA IF EXISTS hive_write_perf CASCADE;
CREATE SCHEMA hive_write_perf;

SELECT test_log('Hive Write Performance Tests');
SELECT test_log('NOTE: Write tests require writable Hive tables');

\set iterations `echo "${PERF_ITERATIONS:-3}"`
\set warmup     `echo "${PERF_WARMUP_RUNS:-1}"`

-- Sync writable hive table (single-shot setup)
SELECT public.sync_hive_table('hive_cluster','default','perf_write_test','paa_cluster', 'hive_write_perf.perf_write_test', 'hive_server');

-- Note: actual INSERT against writable Hive tables is a placeholder pending
-- writable Hive support; for now measure SELECT-based read paths as a smoke
-- check that the synced table is queryable.
SELECT test_log('Performance Test 1: Read from writable hive table');
SELECT perf_run_iterations(
    'hive_write', 'read_writable_table',
    'SELECT COUNT(*) FROM hive_write_perf.perf_write_test',
    :iterations, :warmup, NULL, 'Read placeholder for writable hive table');

-- Cleanup
DROP SCHEMA hive_write_perf CASCADE;
