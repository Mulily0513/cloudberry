-- hive_scan_performance.sql
-- Performance tests for Hive table scans (ORC and Parquet)

\i ../../../lib/sql/common_setup.sql
\i ../../../lib/sql/hive_server_setup.sql
\i ../../../lib/sql/performance_helpers.sql

-- Create test schema
DROP SCHEMA IF EXISTS hive_perf CASCADE;
CREATE SCHEMA hive_perf;

\set iterations `echo "${PERF_ITERATIONS:-3}"`
\set warmup     `echo "${PERF_WARMUP_RUNS:-1}"`

-- Setup: sync hive tables (single-shot)
SELECT public.sync_hive_table('hive_cluster','default','perf_orc_10k','paa_cluster', 'hive_perf.perf_orc_10k', 'hive_server');
SELECT public.sync_hive_table('hive_cluster','default','perf_parquet_10k','paa_cluster', 'hive_perf.perf_parquet_10k', 'hive_server');

SELECT test_log('Performance Test 1: ORC Full Table Scan (10K rows)');
SELECT perf_run_iterations(
    'hive_scan_orc', 'full_table_scan_10k',
    'SELECT COUNT(*) FROM hive_perf.perf_orc_10k',
    :iterations, :warmup, 10000, 'Full table scan on 10K row ORC table');

SELECT test_log('Performance Test 2: ORC Filtered Scan (10% selectivity)');
SELECT perf_run_iterations(
    'hive_scan_orc', 'filtered_scan_10pct',
    'SELECT COUNT(*) FROM hive_perf.perf_orc_10k WHERE id < 1000',
    :iterations, :warmup, 1000, 'Filtered scan with 10% selectivity');

SELECT test_log('Performance Test 3: ORC Aggregation');
SELECT perf_run_iterations(
    'hive_scan_orc', 'aggregation',
    'SELECT category, COUNT(*), AVG(value), SUM(value) FROM hive_perf.perf_orc_10k GROUP BY category ORDER BY category',
    :iterations, :warmup, NULL, 'Aggregation with GROUP BY');

SELECT test_log('Performance Test 4: Parquet Full Table Scan (10K rows)');
SELECT perf_run_iterations(
    'hive_scan_parquet', 'full_table_scan_10k',
    'SELECT COUNT(*) FROM hive_perf.perf_parquet_10k',
    :iterations, :warmup, 10000, 'Full table scan on 10K row Parquet table');

SELECT test_log('Performance Test 5: Join Query');
SELECT perf_run_iterations(
    'hive_scan', 'join_query',
    'SELECT COUNT(*) FROM hive_perf.perf_orc_10k o JOIN hive_perf.perf_parquet_10k p ON o.id = p.id',
    :iterations, :warmup, 10000, 'Join between ORC and Parquet tables');

-- Cleanup
DROP SCHEMA hive_perf CASCADE;
