-- Hive Smoke Test
-- Tests sync_hive_table and basic read operations on various Hive table formats.
-- All tables use PARQUET (or TEXTFILE) to avoid Hive 3.0 ORC compatibility issues.

CREATE EXTENSION IF NOT EXISTS datalake_fdw;
CREATE EXTENSION IF NOT EXISTS hive_connector;
CREATE FOREIGN DATA WRAPPER datalake_fdw
    HANDLER datalake_fdw_handler
    VALIDATOR datalake_fdw_validator
    OPTIONS (mpp_execute 'all segments');
SELECT public.create_foreign_server('hive_server', 'gpadmin', 'datalake_fdw', 'paa_cluster');

DROP SCHEMA IF EXISTS hive_smoke CASCADE;
CREATE SCHEMA hive_smoke;
SET datestyle = ISO, MDY;

-- Sync basic format tables (all PARQUET now)
SELECT public.sync_hive_table('hive_cluster','default','test_orc','paa_cluster', 'hive_smoke.test_orc', 'hive_server');
SELECT * FROM hive_smoke.test_orc ORDER BY id NULLS LAST, m;

SELECT public.sync_hive_table('hive_cluster','default','test_parquet','paa_cluster', 'hive_smoke.test_parquet', 'hive_server');
SELECT * FROM hive_smoke.test_parquet ORDER BY id;

SELECT public.sync_hive_table('hive_cluster','default','test_text','paa_cluster', 'hive_smoke.test_text', 'hive_server');
SELECT * FROM hive_smoke.test_text ORDER BY id;

SELECT public.sync_hive_table('hive_cluster','default','test_empty','paa_cluster', 'hive_smoke.test_empty', 'hive_server');
SELECT * FROM hive_smoke.test_empty;

-- Sync multi-level partition table
SELECT public.sync_hive_table('hive_cluster','default','test_multi_partition','paa_cluster', 'hive_smoke.test_multi_partition', 'hive_server');
SELECT * FROM hive_smoke.test_multi_partition ORDER BY year, month, day;
SELECT * FROM hive_smoke.test_multi_partition WHERE year = 2024 AND month = 1 ORDER BY day;

-- Sync edge case: extreme values
SELECT public.sync_hive_table('hive_cluster','default','test_extreme','paa_cluster', 'hive_smoke.test_extreme', 'hive_server');
SELECT * FROM hive_smoke.test_extreme;

-- Sync edge case: empty string vs NULL
SELECT public.sync_hive_table('hive_cluster','default','test_empty_str','paa_cluster', 'hive_smoke.test_empty_str', 'hive_server');
SELECT id, empty_val IS NULL as empty_is_null, null_val IS NULL as null_is_null FROM hive_smoke.test_empty_str ORDER BY id;

-- Sync edge case: unicode (ASCII-safe values to avoid encoding issues)
SELECT public.sync_hive_table('hive_cluster','default','test_unicode','paa_cluster', 'hive_smoke.test_unicode', 'hive_server');
SELECT * FROM hive_smoke.test_unicode ORDER BY id;

-- Sync edge case: zero values
SELECT public.sync_hive_table('hive_cluster','default','test_zero','paa_cluster', 'hive_smoke.test_zero', 'hive_server');
SELECT * FROM hive_smoke.test_zero ORDER BY id;

-- Sync edge case: decimal precision
SELECT public.sync_hive_table('hive_cluster','default','test_decimal','paa_cluster', 'hive_smoke.test_decimal', 'hive_server');
SELECT * FROM hive_smoke.test_decimal ORDER BY id;

-- Sync edge case: whitespace
SELECT public.sync_hive_table('hive_cluster','default','test_space','paa_cluster', 'hive_smoke.test_space', 'hive_server');
SELECT id, length(leading_val) as leading_len, length(trailing_val) as trailing_len FROM hive_smoke.test_space ORDER BY id;

-- Sync edge case: scientific notation
SELECT public.sync_hive_table('hive_cluster','default','test_scientific','paa_cluster', 'hive_smoke.test_scientific', 'hive_server');
SELECT * FROM hive_smoke.test_scientific ORDER BY id;

-- Sync edge case: date time boundary
SELECT public.sync_hive_table('hive_cluster','default','test_datetime','paa_cluster', 'hive_smoke.test_datetime', 'hive_server');
SELECT * FROM hive_smoke.test_datetime ORDER BY id;

-- Cleanup
DROP SCHEMA hive_smoke CASCADE;
