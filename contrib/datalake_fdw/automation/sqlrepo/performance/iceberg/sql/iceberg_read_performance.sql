-- iceberg_read_performance.sql
-- Performance tests for Iceberg table read operations

\i ../../../lib/sql/common_setup.sql
\i ../../../lib/sql/performance_helpers.sql

-- Create builtin catalog and volume for testing
CREATE SERVER perf_catalog_server
FOREIGN DATA WRAPPER iceberg_catalog_fdw;
CREATE USER MAPPING FOR current_user SERVER perf_catalog_server;
CREATE FOREIGN CATALOG perf_catalog SERVER perf_catalog_server;
SET iceberg_default_catalog='perf_catalog';

CREATE SERVER perf_volume_server
FOREIGN DATA WRAPPER iceberg_volume_fdw
OPTIONS (
    type 's3',
    endpoint 'http://lakehouse:9100',
    region 'us-east-1',
    bucket_name 'warehouse',
    path_style_access 'true'
);
CREATE USER MAPPING FOR current_user
SERVER perf_volume_server
OPTIONS (
    access_key_id 'admin',
    secret_access_key 'password');
CREATE FOREIGN VOLUME perf_volume SERVER perf_volume_server OPTIONS(base_path '/perf_volume/');
SET iceberg_default_volume='perf_volume';

SELECT test_log('Iceberg Read Performance Tests');

-- Iteration / warmup counts come from psql variables which the runner sets
-- from PERF_ITERATIONS / PERF_WARMUP_RUNS env vars (see test_config.env).
-- Defaults match CI profile if vars are absent.
\set iterations `echo "${PERF_ITERATIONS:-3}"`
\set warmup     `echo "${PERF_WARMUP_RUNS:-1}"`

-- Setup: Create and populate test table (single-shot, not iterated)
CREATE ICEBERG TABLE iceberg_perf_read (
    id bigint,
    category text,
    value numeric(12,2),
    created_at timestamp
);

INSERT INTO iceberg_perf_read
SELECT
    generate_series(1, 5000) as id,
    'category_' || (generate_series(1, 5000) % 10) as category,
    (generate_series(1, 5000) * 1.5)::numeric(12,2) as value,
    NOW() as created_at;

-- Read tests: idempotent SELECTs run via perf_run_iterations.
-- Per-iteration timings go into perf_results; aggregated p50/min/max
-- into perf_results_summary. The runner (compare_baseline.sh) reads the
-- summary table after the test completes.

SELECT test_log('Performance Test 1: Full Table Scan');
SELECT perf_run_iterations(
    'iceberg_read', 'full_scan_5k',
    'SELECT COUNT(*) FROM iceberg_perf_read',
    :iterations, :warmup, 5000, 'Full table scan 5000 rows');

SELECT test_log('Performance Test 2: Filtered Scan');
SELECT perf_run_iterations(
    'iceberg_read', 'filtered_scan_10pct',
    'SELECT COUNT(*) FROM iceberg_perf_read WHERE id < 500',
    :iterations, :warmup, 500, 'Filtered scan 10% selectivity');

SELECT test_log('Performance Test 3: Aggregation Query');
SELECT perf_run_iterations(
    'iceberg_read', 'aggregation',
    'SELECT category, COUNT(*), AVG(value), SUM(value) FROM iceberg_perf_read GROUP BY category ORDER BY category',
    :iterations, :warmup, NULL, 'Aggregation with GROUP BY');

SELECT test_log('Performance Test 4: Point Query');
SELECT perf_run_iterations(
    'iceberg_read', 'point_query',
    'SELECT * FROM iceberg_perf_read WHERE id = 2500',
    :iterations, :warmup, 1, 'Point query single row');

SELECT test_log('Performance Test 5: Range Query');
SELECT perf_run_iterations(
    'iceberg_read', 'range_query',
    'SELECT COUNT(*) FROM iceberg_perf_read WHERE id BETWEEN 1000 AND 2000',
    :iterations, :warmup, 1001, 'Range query 1000 rows');

-- Capture EXPLAIN for a representative query (plan stability check)
SELECT perf_capture_explain(
    'iceberg_read', 'aggregation',
    'SELECT category, COUNT(*), AVG(value), SUM(value) FROM iceberg_perf_read GROUP BY category ORDER BY category');

-- Note: perf_summary_report() output intentionally NOT printed here - timings
-- vary across runs and would break pg_regress diff. Runner queries the
-- perf_results_summary table after test completion to generate reports.

-- Cleanup
DROP TABLE iceberg_perf_read;
DROP VOLUME perf_volume;
DROP USER MAPPING FOR current_user SERVER perf_volume_server;
DROP SERVER perf_volume_server;
DROP CATALOG perf_catalog;
DROP USER MAPPING FOR current_user SERVER perf_catalog_server;
DROP SERVER perf_catalog_server;
