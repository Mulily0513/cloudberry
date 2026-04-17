-- scale_volume.sql
-- Scale test: Large volume data ingestion and full-scan aggregation
-- Inserts SCALE_VOLUME_ROWS rows (default 100000) in batches and measures
-- full scan + aggregation throughput.

\i ../../../lib/sql/common_setup.sql
\i ../../../lib/sql/performance_helpers.sql

SELECT test_log('Scale Test: Volume data load and scan');

-- ============================================================
-- Read row count from env var (psql \set), fall back to 100000
-- ============================================================
\if :{?SCALE_VOLUME_ROWS}
    \set rows :SCALE_VOLUME_ROWS
\else
    \set rows 100000
\endif

-- Store in a GUC so the DO block can read it
SET scale.volume_rows = :'rows';

-- ============================================================
-- Setup: Catalog and volume
-- ============================================================
DROP SERVER IF EXISTS sv_catalog_server CASCADE;
CREATE SERVER sv_catalog_server FOREIGN DATA WRAPPER iceberg_catalog_fdw;
CREATE USER MAPPING FOR current_user SERVER sv_catalog_server;
CREATE FOREIGN CATALOG sv_catalog SERVER sv_catalog_server;
SET iceberg_default_catalog = 'sv_catalog';

CREATE SERVER sv_volume_server FOREIGN DATA WRAPPER iceberg_volume_fdw
    OPTIONS (type 's3', endpoint 'http://lakehouse:9100', region 'us-east-1',
             bucket_name 'warehouse', path_style_access 'true');
CREATE USER MAPPING FOR current_user SERVER sv_volume_server
    OPTIONS (access_key_id 'admin', secret_access_key 'password');
CREATE FOREIGN VOLUME sv_volume SERVER sv_volume_server
    OPTIONS (base_path '/sv_volume/');
SET iceberg_default_volume = 'sv_volume';

-- ============================================================
-- Create Iceberg table
-- ============================================================
CREATE ICEBERG TABLE sv_data (
    id        bigint,
    category  int,
    label     text,
    amount    numeric(10,2),
    ts        timestamp
);

-- ============================================================
-- Bulk insert in batches of 10000
-- ============================================================
SELECT test_log('Inserting rows in batches of 10000');

DO $$
DECLARE
    v_total   int;
    v_batch   int := 10000;
    v_offset  int := 0;
BEGIN
    v_total := current_setting('scale.volume_rows')::int;
    WHILE v_offset < v_total LOOP
        INSERT INTO sv_data
        SELECT g,
               (g % 20),
               'item_' || g,
               (random() * 9999)::numeric(10,2),
               timestamp '2024-01-01' + (g || ' seconds')::interval
        FROM generate_series(v_offset + 1, LEAST(v_offset + v_batch, v_total)) g;
        v_offset := v_offset + v_batch;
    END LOOP;
    RAISE NOTICE '[TEST] Inserted % rows total', v_total;
END
$$;

-- Verify row count
SELECT COUNT(*) AS row_count FROM sv_data;

-- ============================================================
-- Measure: Full scan + aggregation
-- ============================================================
SELECT test_log('Measuring full scan + aggregation');

SELECT perf_run_iterations(
    'scale_volume', 'full_scan_agg',
    'SELECT category, COUNT(*), SUM(amount), AVG(amount) FROM sv_data GROUP BY category ORDER BY category',
    3,   -- iterations
    1,   -- warmup
    current_setting('scale.volume_rows')::bigint,
    'Full scan with GROUP BY aggregation on ' || current_setting('scale.volume_rows') || ' rows'
);

-- ============================================================
-- Measure: COUNT(*) scan
-- ============================================================
SELECT perf_run_iterations(
    'scale_volume', 'count_star',
    'SELECT COUNT(*) FROM sv_data',
    3,   -- iterations
    1,   -- warmup
    current_setting('scale.volume_rows')::bigint,
    'Simple COUNT(*) on ' || current_setting('scale.volume_rows') || ' rows'
);

-- ============================================================
-- Report
-- ============================================================
-- start_ignore
SELECT * FROM perf_summary_report('scale_volume');
-- end_ignore

-- ============================================================
-- Cleanup
-- ============================================================
DROP TABLE sv_data;
DROP VOLUME sv_volume;
DROP USER MAPPING FOR current_user SERVER sv_volume_server;
DROP SERVER sv_volume_server;
DROP CATALOG sv_catalog;
DROP USER MAPPING FOR current_user SERVER sv_catalog_server;
DROP SERVER sv_catalog_server;

SELECT test_log('Scale volume test completed');
