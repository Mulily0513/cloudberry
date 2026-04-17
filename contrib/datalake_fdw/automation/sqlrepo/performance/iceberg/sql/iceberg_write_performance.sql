-- iceberg_write_performance.sql
-- Performance tests for Iceberg table write operations

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

SELECT test_log('Iceberg Write Performance Tests');

\set iterations `echo "${PERF_ITERATIONS:-3}"`
\set warmup     `echo "${PERF_WARMUP_RUNS:-1}"`

-- Write tests are non-idempotent (DDL / INSERT / UPDATE / DELETE change state).
-- We use perf_run_iterations only when the operation can be safely repeated
-- (here: by including TRUNCATE in the SQL for INSERT tests). For DDL we keep
-- the legacy perf_start_timer / perf_end_timer single-shot pattern.

-- Test 1: Create table (single-shot, DDL not iterated)
-- start_ignore
SELECT test_log('Performance Test 1: Create Iceberg Table');
SELECT perf_start_timer('iceberg_write', 'create_table');
CREATE ICEBERG TABLE iceberg_perf_write (
    id bigint,
    name text,
    value numeric(12,2),
    created_at timestamp
);
SELECT perf_end_timer('iceberg_write', 'create_table', NULL, 'Create Iceberg table');
-- end_ignore

-- Test 2: Single row insert (idempotent via TRUNCATE in setup_sql)
SELECT test_log('Performance Test 2: Single Row Insert');
SELECT perf_run_iterations(
    'iceberg_write', 'single_insert',
    'INSERT INTO iceberg_perf_write VALUES (1, ''test_1'', 100.50, NOW())',
    :iterations, :warmup, 1, 'Single row insert',
    'TRUNCATE iceberg_perf_write');

-- Test 3: Batch insert (100 rows)
SELECT test_log('Performance Test 3: Batch Insert (100 rows)');
SELECT perf_run_iterations(
    'iceberg_write', 'batch_insert_100',
    'INSERT INTO iceberg_perf_write SELECT generate_series(1, 100), ''test_'' || generate_series(1, 100), generate_series(1, 100) * 1.5, NOW()',
    :iterations, :warmup, 100, 'Batch insert 100 rows',
    'TRUNCATE iceberg_perf_write');

-- Test 4: Large batch insert (1000 rows)
SELECT test_log('Performance Test 4: Large Batch Insert (1000 rows)');
SELECT perf_run_iterations(
    'iceberg_write', 'batch_insert_1000',
    'INSERT INTO iceberg_perf_write SELECT generate_series(1, 1000), ''test_'' || generate_series(1, 1000), generate_series(1, 1000) * 1.5, NOW()',
    :iterations, :warmup, 1000, 'Batch insert 1000 rows',
    'TRUNCATE iceberg_perf_write');

-- Populate baseline state for UPDATE/DELETE tests (single-shot)
TRUNCATE iceberg_perf_write;
INSERT INTO iceberg_perf_write
SELECT generate_series(1, 1000), 'test_' || generate_series(1, 1000),
       generate_series(1, 1000) * 1.5, NOW();

-- start_ignore
-- Test 5: UPDATE operation (single-shot, mutates state)
SELECT test_log('Performance Test 5: UPDATE operation');
SELECT perf_start_timer('iceberg_write', 'update_operation');
UPDATE iceberg_perf_write SET value = value * 2 WHERE id <= 10;
SELECT perf_end_timer('iceberg_write', 'update_operation', 10, 'Update 10 rows');

-- Test 6: DELETE operation (single-shot, mutates state)
SELECT test_log('Performance Test 6: DELETE operation');
SELECT perf_start_timer('iceberg_write', 'delete_operation');
DELETE FROM iceberg_perf_write WHERE id > 100;
SELECT perf_end_timer('iceberg_write', 'delete_operation', 900, 'Delete 900 rows');
-- end_ignore

-- Note: report rendering deferred to runner (compare_baseline.sh)

-- Cleanup
DROP TABLE iceberg_perf_write;
DROP VOLUME perf_volume;
DROP USER MAPPING FOR current_user SERVER perf_volume_server;
DROP SERVER perf_volume_server;
DROP CATALOG perf_catalog;
DROP USER MAPPING FOR current_user SERVER perf_catalog_server;
DROP SERVER perf_catalog_server;
