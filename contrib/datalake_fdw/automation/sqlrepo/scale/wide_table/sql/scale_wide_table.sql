-- scale_wide_table.sql
-- Scale test: Wide table with 50 columns
-- Creates an Iceberg table with 50 columns (rotating int/text/numeric pattern),
-- inserts 1000 rows, and measures full scan, single-column projection, and
-- CREATE TABLE time.

\i ../../../lib/sql/common_setup.sql
\i ../../../lib/sql/performance_helpers.sql

SELECT test_log('Scale Test: Wide table (50 columns)');

-- ============================================================
-- Setup: Catalog and volume
-- ============================================================
DROP SERVER IF EXISTS wt_catalog_server CASCADE;
CREATE SERVER wt_catalog_server FOREIGN DATA WRAPPER iceberg_catalog_fdw;
CREATE USER MAPPING FOR current_user SERVER wt_catalog_server;
CREATE FOREIGN CATALOG wt_catalog SERVER wt_catalog_server;
SET iceberg_default_catalog = 'wt_catalog';

CREATE SERVER wt_volume_server FOREIGN DATA WRAPPER iceberg_volume_fdw
    OPTIONS (type 's3', endpoint 'http://lakehouse:9100', region 'us-east-1',
             bucket_name 'warehouse', path_style_access 'true');
CREATE USER MAPPING FOR current_user SERVER wt_volume_server
    OPTIONS (access_key_id 'admin', secret_access_key 'password');
CREATE FOREIGN VOLUME wt_volume SERVER wt_volume_server
    OPTIONS (base_path '/wt_volume/');
SET iceberg_default_volume = 'wt_volume';

-- ============================================================
-- Create wide Iceberg table with 50 columns via dynamic SQL
-- Pattern: c1 int, c2 text, c3 numeric(10,2), c4 int, c5 text, ...
-- ============================================================
SELECT test_log('Creating wide table with 50 columns');

SELECT perf_start_timer('scale_wide_table', 'create_table');

DO $$
DECLARE
    v_cols text := '';
    v_type text;
BEGIN
    FOR i IN 1..50 LOOP
        CASE (i - 1) % 3
            WHEN 0 THEN v_type := 'int';
            WHEN 1 THEN v_type := 'text';
            WHEN 2 THEN v_type := 'numeric(10,2)';
        END CASE;
        IF i > 1 THEN
            v_cols := v_cols || ', ';
        END IF;
        v_cols := v_cols || 'c' || i || ' ' || v_type;
    END LOOP;
    EXECUTE 'CREATE ICEBERG TABLE wt_wide (' || v_cols || ')';
    RAISE NOTICE '[TEST] Created table with 50 columns';
END
$$;

-- start_ignore
SELECT perf_end_timer('scale_wide_table', 'create_table', NULL,
    'CREATE ICEBERG TABLE with 50 columns');

-- ============================================================
-- Insert 1000 rows with dynamic SQL
-- ============================================================
SELECT test_log('Inserting 1000 rows into wide table');

DO $$
DECLARE
    v_vals text := '';
    v_val  text;
BEGIN
    FOR i IN 1..50 LOOP
        CASE (i - 1) % 3
            WHEN 0 THEN v_val := 'g';                                -- int column
            WHEN 1 THEN v_val := '''val_'' || g || ''_c' || i || ''''; -- text column
            WHEN 2 THEN v_val := '(g * ' || i || ' * 0.01)::numeric(10,2)';  -- numeric column
        END CASE;
        IF i > 1 THEN
            v_vals := v_vals || ', ';
        END IF;
        v_vals := v_vals || v_val;
    END LOOP;
    EXECUTE 'INSERT INTO wt_wide SELECT ' || v_vals
         || ' FROM generate_series(1, 1000) g';
    RAISE NOTICE '[TEST] Inserted 1000 rows into wide table';
END
$$;

-- Verify row count
SELECT COUNT(*) AS row_count FROM wt_wide;

-- ============================================================
-- Measure: SELECT * full scan (all 50 columns)
-- ============================================================
SELECT test_log('Measuring SELECT * full scan');

SELECT perf_run_iterations(
    'scale_wide_table', 'full_scan_select_star',
    'SELECT COUNT(*) FROM (SELECT * FROM wt_wide) sub',
    3,   -- iterations
    1,   -- warmup
    1000,
    'Full scan of 50-column table (1000 rows)'
);

-- ============================================================
-- Measure: Single column projection (c1 only - projection pushdown)
-- ============================================================
SELECT test_log('Measuring single column projection (c1)');

SELECT perf_run_iterations(
    'scale_wide_table', 'single_col_projection',
    'SELECT COUNT(*) FROM (SELECT c1 FROM wt_wide) sub',
    3,   -- iterations
    1,   -- warmup
    1000,
    'Single column scan (c1 int) - projection pushdown'
);

-- ============================================================
-- Measure: Small subset of columns (c1, c2, c3)
-- ============================================================
SELECT test_log('Measuring 3-column projection');

SELECT perf_run_iterations(
    'scale_wide_table', 'three_col_projection',
    'SELECT COUNT(*) FROM (SELECT c1, c2, c3 FROM wt_wide) sub',
    3,   -- iterations
    1,   -- warmup
    1000,
    'Three column scan (c1 int, c2 text, c3 numeric) - projection pushdown'
);

-- ============================================================
-- Measure: Aggregation on wide table
-- ============================================================
SELECT test_log('Measuring aggregation on wide table');

SELECT perf_run_iterations(
    'scale_wide_table', 'wide_aggregation',
    'SELECT SUM(c1), AVG(c3), MIN(c1), MAX(c1) FROM wt_wide',
    3,   -- iterations
    1,   -- warmup
    1000,
    'Aggregation across numeric columns of wide table'
);

-- ============================================================
-- Report
-- ============================================================
-- start_ignore
SELECT * FROM perf_summary_report('scale_wide_table');
-- end_ignore

-- ============================================================
-- Cleanup
-- ============================================================
DROP TABLE wt_wide;
DROP VOLUME wt_volume;
DROP USER MAPPING FOR current_user SERVER wt_volume_server;
DROP SERVER wt_volume_server;
DROP CATALOG wt_catalog;
DROP USER MAPPING FOR current_user SERVER wt_catalog_server;
DROP SERVER wt_catalog_server;

SELECT test_log('Scale wide table test completed');
