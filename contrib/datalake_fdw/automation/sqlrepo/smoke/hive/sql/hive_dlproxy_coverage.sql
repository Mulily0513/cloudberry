-- Hive DLProxy Coverage Test
-- Purpose: Exercise dlproxy code paths via Hive foreign tables
-- Target: dlproxy/filters.c (+384), dlproxy/protocol.c (+149),
--         dlproxy/libchurl.c (+136), dlproxy/headers.c (+77),
--         dlproxy/iceberg.c (+60), dlproxy/icebergConfig.c (+112),
--         dlproxy/iceberg_fragment_cache.c (+42)

SET client_min_messages = ERROR;
DROP SCHEMA IF EXISTS dlproxy_cov CASCADE;
DROP FOREIGN DATA WRAPPER IF EXISTS datalake_fdw CASCADE;
RESET client_min_messages;

CREATE EXTENSION IF NOT EXISTS datalake_fdw;
CREATE EXTENSION IF NOT EXISTS hive_connector;
CREATE FOREIGN DATA WRAPPER datalake_fdw
    HANDLER datalake_fdw_handler
    VALIDATOR datalake_fdw_validator
    OPTIONS (mpp_execute 'all segments');
SELECT public.create_foreign_server('hive_server', 'gpadmin', 'datalake_fdw', 'paa_cluster');

DROP SCHEMA IF EXISTS dlproxy_cov CASCADE;
CREATE SCHEMA dlproxy_cov;
SET datestyle = ISO, MDY;

-- ============================================================
-- Sync Hive tables (creates foreign tables that use dlproxy path)
-- ============================================================
SELECT public.sync_hive_table('hive_cluster','default','test_orc','paa_cluster', 'dlproxy_cov.test_orc', 'hive_server');
SELECT public.sync_hive_table('hive_cluster','default','test_parquet','paa_cluster', 'dlproxy_cov.test_parquet', 'hive_server');
SELECT public.sync_hive_table('hive_cluster','default','test_text','paa_cluster', 'dlproxy_cov.test_text', 'hive_server');
SELECT public.sync_hive_table('hive_cluster','default','test_multi_partition','paa_cluster', 'dlproxy_cov.test_multi_partition', 'hive_server');
SELECT public.sync_hive_table('hive_cluster','default','test_extreme','paa_cluster', 'dlproxy_cov.test_extreme', 'hive_server');
SELECT public.sync_hive_table('hive_cluster','default','test_decimal','paa_cluster', 'dlproxy_cov.test_decimal', 'hive_server');
SELECT public.sync_hive_table('hive_cluster','default','test_zero','paa_cluster', 'dlproxy_cov.test_zero', 'hive_server');
SELECT public.sync_hive_table('hive_cluster','default','test_datetime','paa_cluster', 'dlproxy_cov.test_datetime', 'hive_server');
SELECT public.sync_hive_table('hive_cluster','default','test_scientific','paa_cluster', 'dlproxy_cov.test_scientific', 'hive_server');
SELECT public.sync_hive_table('hive_cluster','default','test_unicode','paa_cluster', 'dlproxy_cov.test_unicode', 'hive_server');

-- ============================================================
-- Test 1: WHERE clause filter pushdown (dlproxy/filters.c)
-- Each operator type triggers a different serialization branch
-- ============================================================

-- Equality (=)
SELECT * FROM dlproxy_cov.test_orc WHERE id = 1;

-- Not-equal (!=)
SELECT COUNT(*) FROM dlproxy_cov.test_orc WHERE id != 1;

-- Greater than (>)
SELECT * FROM dlproxy_cov.test_parquet WHERE id > 0;

-- Less than (<)
SELECT COUNT(*) FROM dlproxy_cov.test_orc WHERE id < 2;

-- Greater or equal (>=)
SELECT COUNT(*) FROM dlproxy_cov.test_parquet WHERE id >= 1;

-- Less or equal (<=)
SELECT COUNT(*) FROM dlproxy_cov.test_orc WHERE id <= 1;

-- IS NULL / IS NOT NULL
SELECT COUNT(*) FROM dlproxy_cov.test_orc WHERE name IS NULL;
SELECT COUNT(*) FROM dlproxy_cov.test_orc WHERE name IS NOT NULL;

-- AND compound filter
SELECT * FROM dlproxy_cov.test_orc WHERE id = 1 AND name = 'test';

-- OR compound filter
SELECT COUNT(*) FROM dlproxy_cov.test_orc WHERE id = 1 OR name IS NULL;

-- NOT filter
SELECT COUNT(*) FROM dlproxy_cov.test_orc WHERE NOT (id = 1);

-- ============================================================
-- Test 2: Filter on different data types (filters.c type branches)
-- ============================================================

-- Integer filter
SELECT * FROM dlproxy_cov.test_extreme WHERE int_max > 0;
SELECT * FROM dlproxy_cov.test_extreme WHERE tiny_min < 0;

-- Decimal filter
SELECT * FROM dlproxy_cov.test_decimal WHERE dec1 > 0;
SELECT * FROM dlproxy_cov.test_decimal WHERE dec2 > 100;

-- Float/double filter
SELECT * FROM dlproxy_cov.test_zero WHERE zero_float = 0.0;
SELECT * FROM dlproxy_cov.test_scientific WHERE small_val > 0;
SELECT * FROM dlproxy_cov.test_scientific WHERE large_val > 1E50;

-- Date filter
SELECT * FROM dlproxy_cov.test_datetime WHERE old_date = '1970-01-01';

-- Timestamp filter
SELECT * FROM dlproxy_cov.test_datetime WHERE ts >= '1970-01-01 00:00:00';

-- Text filter
SELECT * FROM dlproxy_cov.test_unicode WHERE val = 'ascii_ok';
SELECT COUNT(*) FROM dlproxy_cov.test_text WHERE name = 'hello';

-- ============================================================
-- Test 3: Partition filter (multi-level partition scanning)
-- ============================================================
SELECT * FROM dlproxy_cov.test_multi_partition WHERE year = 2024;
SELECT * FROM dlproxy_cov.test_multi_partition WHERE year = 2024 AND month = 1;
SELECT * FROM dlproxy_cov.test_multi_partition WHERE year = 2024 AND month = 1 AND day = 1;
SELECT * FROM dlproxy_cov.test_multi_partition WHERE month > 6;
SELECT COUNT(*) FROM dlproxy_cov.test_multi_partition WHERE day >= 2;

-- ============================================================
-- Test 4: Column projection (headers.c projection paths)
-- ============================================================

-- Single column projection
SELECT id FROM dlproxy_cov.test_orc;
SELECT name FROM dlproxy_cov.test_parquet;

-- Subset of columns
SELECT id, name FROM dlproxy_cov.test_orc WHERE m = 1;

-- Aggregate (may or may not push down)
SELECT COUNT(*) FROM dlproxy_cov.test_parquet;
SELECT COUNT(name) FROM dlproxy_cov.test_orc;

-- ============================================================
-- Test 5: Different format reads (headers.c format-specific paths)
-- ============================================================

-- ORC format
SELECT * FROM dlproxy_cov.test_orc ORDER BY id NULLS LAST, m;

-- Parquet format
SELECT * FROM dlproxy_cov.test_parquet ORDER BY id;

-- Text format
SELECT * FROM dlproxy_cov.test_text ORDER BY id;

-- ============================================================
-- Test 6: Type-specific header encoding (headers.c type modifier paths)
-- ============================================================

-- Decimal precision/scale in headers
SELECT * FROM dlproxy_cov.test_decimal ORDER BY id;

-- Integer boundary values in headers
SELECT * FROM dlproxy_cov.test_extreme;

-- Date/timestamp in headers
SELECT * FROM dlproxy_cov.test_datetime;

-- Float/scientific in headers
SELECT * FROM dlproxy_cov.test_scientific;

-- ============================================================
-- Test 7: Multiple sequential reads (protocol.c connection reuse)
-- ============================================================
SELECT COUNT(*) FROM dlproxy_cov.test_orc;
SELECT COUNT(*) FROM dlproxy_cov.test_parquet;
SELECT COUNT(*) FROM dlproxy_cov.test_text;
SELECT COUNT(*) FROM dlproxy_cov.test_orc;
SELECT COUNT(*) FROM dlproxy_cov.test_parquet;

-- ============================================================
-- Cleanup
-- ============================================================
DROP SCHEMA dlproxy_cov CASCADE;
