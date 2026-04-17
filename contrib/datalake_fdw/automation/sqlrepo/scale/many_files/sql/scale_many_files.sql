-- scale_many_files.sql
-- Scale test: Many small data files - measures scan startup and VACUUM compaction
-- Inserts 100 rows per iteration for SCALE_MANY_FILES (default 100) iterations,
-- producing ~100 small data files, then measures scan and compaction performance.

\i ../../../lib/sql/common_setup.sql
\i ../../../lib/sql/performance_helpers.sql

SELECT test_log('Scale Test: Many small files scan and compaction');

-- ============================================================
-- Read iteration count from env var, fall back to 100
-- ============================================================
\if :{?SCALE_MANY_FILES}
    \set iters :SCALE_MANY_FILES
\else
    \set iters 100
\endif

SET scale.many_files_iters = :'iters';

-- ============================================================
-- Setup: Catalog and volume
-- ============================================================
DROP SERVER IF EXISTS mf_catalog_server CASCADE;
CREATE SERVER mf_catalog_server FOREIGN DATA WRAPPER iceberg_catalog_fdw;
CREATE USER MAPPING FOR current_user SERVER mf_catalog_server;
CREATE FOREIGN CATALOG mf_catalog SERVER mf_catalog_server;
SET iceberg_default_catalog = 'mf_catalog';

CREATE SERVER mf_volume_server FOREIGN DATA WRAPPER iceberg_volume_fdw
    OPTIONS (type 's3', endpoint 'http://lakehouse:9100', region 'us-east-1',
             bucket_name 'warehouse', path_style_access 'true');
CREATE USER MAPPING FOR current_user SERVER mf_volume_server
    OPTIONS (access_key_id 'admin', secret_access_key 'password');
CREATE FOREIGN VOLUME mf_volume SERVER mf_volume_server
    OPTIONS (base_path '/mf_volume/');
SET iceberg_default_volume = 'mf_volume';

-- ============================================================
-- Create Iceberg table
-- ============================================================
CREATE ICEBERG TABLE mf_data (
    id      bigint,
    batch   int,
    val     numeric(10,2),
    label   text
);

-- ============================================================
-- Insert 100 rows per iteration to create many small data files
-- ============================================================
SELECT test_log('Creating many small data files');

DO $$
DECLARE
    v_iters int;
BEGIN
    v_iters := current_setting('scale.many_files_iters')::int;
    FOR i IN 1..v_iters LOOP
        INSERT INTO mf_data
        SELECT (i - 1) * 100 + g,
               i,
               (random() * 1000)::numeric(10,2),
               'batch_' || i || '_row_' || g
        FROM generate_series(1, 100) g;
    END LOOP;
    RAISE NOTICE '[TEST] Completed % insert iterations (~% data files)',
        v_iters, v_iters;
END
$$;

-- Verify total row count
SELECT COUNT(*) AS total_rows FROM mf_data;

-- ============================================================
-- Measure: Scan startup time (many small files)
-- ============================================================
SELECT test_log('Measuring scan startup time over many files');

SELECT perf_run_iterations(
    'scale_many_files', 'scan_startup',
    'SELECT COUNT(*) FROM mf_data',
    3,   -- iterations
    1,   -- warmup
    (current_setting('scale.many_files_iters')::bigint * 100),
    'COUNT(*) scan over ~' || current_setting('scale.many_files_iters') || ' data files'
);

SELECT perf_run_iterations(
    'scale_many_files', 'scan_with_filter',
    'SELECT COUNT(*), SUM(val) FROM mf_data WHERE batch <= 10',
    3,   -- iterations
    1,   -- warmup
    1000,
    'Filtered scan (first 10 batches) over many files'
);

-- ============================================================
-- Measure: VACUUM compaction time
-- VACUUM must be outside the DO block
-- ============================================================
SELECT test_log('Measuring VACUUM compaction time');

SELECT perf_start_timer('scale_many_files', 'vacuum_compaction');
VACUUM mf_data;
-- start_ignore
SELECT perf_end_timer('scale_many_files', 'vacuum_compaction',
    (current_setting('scale.many_files_iters')::bigint * 100),
    'VACUUM compaction of ~' || current_setting('scale.many_files_iters') || ' data files');

-- Verify data integrity after compaction
SELECT COUNT(*) AS post_vacuum_rows FROM mf_data;

-- ============================================================
-- Measure: Scan after compaction (should be faster)
-- ============================================================
SELECT test_log('Measuring scan after compaction');

SELECT perf_run_iterations(
    'scale_many_files', 'scan_after_vacuum',
    'SELECT COUNT(*) FROM mf_data',
    3,   -- iterations
    1,   -- warmup
    (current_setting('scale.many_files_iters')::bigint * 100),
    'COUNT(*) scan after VACUUM compaction'
);

-- ============================================================
-- Report
-- ============================================================
-- start_ignore
SELECT * FROM perf_summary_report('scale_many_files');
-- end_ignore
-- start_ignore
SELECT * FROM perf_report('scale_many_files');
-- end_ignore

-- ============================================================
-- Cleanup
-- ============================================================
DROP TABLE mf_data;
DROP VOLUME mf_volume;
DROP USER MAPPING FOR current_user SERVER mf_volume_server;
DROP SERVER mf_volume_server;
DROP CATALOG mf_catalog;
DROP USER MAPPING FOR current_user SERVER mf_catalog_server;
DROP SERVER mf_catalog_server;

SELECT test_log('Scale many files test completed');
