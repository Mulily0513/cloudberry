-- FDW Hive Sync Test
-- Purpose: Exercise sync_hive_table path (dlproxy, grammar_convert, partition_selector)
-- Target: datalake_fdw.c (FDW planner/scan), dlproxy/protocol.c, dlproxy/hive.c,
--         dlproxy/filters.c, grammar_convert.c, partition_selector.c, datalake_option.c,
--         datalake_fragment.c

-- Setup
CREATE EXTENSION IF NOT EXISTS datalake_fdw;
CREATE EXTENSION IF NOT EXISTS hive_connector;
CREATE FOREIGN DATA WRAPPER datalake_fdw
    HANDLER datalake_fdw_handler
    VALIDATOR datalake_fdw_validator
    OPTIONS (mpp_execute 'all segments');

SELECT public.create_foreign_server('hive_server', 'gpadmin', 'datalake_fdw', 'paa_cluster');

-- ============================================================
-- Test 1: Sync Parquet table from Hive
-- ============================================================
DROP FOREIGN TABLE IF EXISTS fdw_hive_parquet;
SELECT public.sync_hive_table('hive_cluster', 'fdw_coverage_test', 'test_parquet', 'paa_cluster', 'fdw_hive_parquet', 'hive_server');

SELECT * FROM fdw_hive_parquet ORDER BY id;
SELECT COUNT(*) FROM fdw_hive_parquet;

-- Filter pushdown tests on Hive foreign table
SELECT * FROM fdw_hive_parquet WHERE id = 2;
SELECT * FROM fdw_hive_parquet WHERE id > 1 ORDER BY id;
SELECT * FROM fdw_hive_parquet WHERE name = 'alice';

-- EXPLAIN to verify FDW scan
EXPLAIN (COSTS OFF) SELECT * FROM fdw_hive_parquet WHERE id = 1;

-- ANALYZE on foreign table
ANALYZE fdw_hive_parquet;

DROP FOREIGN TABLE fdw_hive_parquet;

-- ============================================================
-- Test 2: Sync ORC table from Hive
-- ============================================================
DROP FOREIGN TABLE IF EXISTS fdw_hive_orc;
SELECT public.sync_hive_table('hive_cluster', 'fdw_coverage_test', 'test_orc', 'paa_cluster', 'fdw_hive_orc', 'hive_server');

SELECT * FROM fdw_hive_orc ORDER BY id;
SELECT COUNT(*) FROM fdw_hive_orc;
SELECT * FROM fdw_hive_orc WHERE id >= 2 ORDER BY id;

DROP FOREIGN TABLE fdw_hive_orc;

-- ============================================================
-- Test 3: Sync Text table from Hive
-- ============================================================
DROP FOREIGN TABLE IF EXISTS fdw_hive_text;
SELECT public.sync_hive_table('hive_cluster', 'fdw_coverage_test', 'test_text', 'paa_cluster', 'fdw_hive_text', 'hive_server');

SELECT * FROM fdw_hive_text ORDER BY id;
SELECT COUNT(*) FROM fdw_hive_text;

DROP FOREIGN TABLE fdw_hive_text;

-- ============================================================
-- Test 4: Sync Text table with new text path enabled
-- ============================================================
SET datalake.external_table_new_text = on;

DROP FOREIGN TABLE IF EXISTS fdw_hive_text_new;
SELECT public.sync_hive_table('hive_cluster', 'fdw_coverage_test', 'test_text', 'paa_cluster', 'fdw_hive_text_new', 'hive_server');

SELECT * FROM fdw_hive_text_new ORDER BY id;
SELECT COUNT(*) FROM fdw_hive_text_new;

DROP FOREIGN TABLE fdw_hive_text_new;
SET datalake.external_table_new_text = off;

-- ============================================================
-- Test 5: Sync Partitioned table (exercises partition_selector)
-- ============================================================
DROP FOREIGN TABLE IF EXISTS fdw_hive_part;
SELECT public.sync_hive_table('hive_cluster', 'fdw_coverage_test', 'test_partitioned', 'paa_cluster', 'fdw_hive_part', 'hive_server');

-- Full scan
SELECT * FROM fdw_hive_part ORDER BY id;
SELECT COUNT(*) FROM fdw_hive_part;

-- Partition filter
SELECT * FROM fdw_hive_part WHERE category = 'A' ORDER BY id;
SELECT * FROM fdw_hive_part WHERE category = 'B' ORDER BY id;
SELECT COUNT(*) FROM fdw_hive_part WHERE category = 'A';
SELECT COUNT(*) FROM fdw_hive_part WHERE category IN ('A', 'C');

-- Non-partition filter
SELECT * FROM fdw_hive_part WHERE id > 3 ORDER BY id;

-- Combined partition + non-partition filter
SELECT * FROM fdw_hive_part WHERE category = 'A' AND id = 1;

DROP FOREIGN TABLE fdw_hive_part;

-- ============================================================
-- Test 6: Sync Avro table
-- ============================================================
DROP FOREIGN TABLE IF EXISTS fdw_hive_avro;
SELECT public.sync_hive_table('hive_cluster', 'fdw_coverage_test', 'test_avro', 'paa_cluster', 'fdw_hive_avro', 'hive_server');

SELECT * FROM fdw_hive_avro ORDER BY id;
SELECT COUNT(*) FROM fdw_hive_avro;

DROP FOREIGN TABLE fdw_hive_avro;

-- ============================================================
-- Test 7: GUC tests on FDW tables
-- ============================================================
DROP FOREIGN TABLE IF EXISTS fdw_hive_guc;
SELECT public.sync_hive_table('hive_cluster', 'fdw_coverage_test', 'test_parquet', 'paa_cluster', 'fdw_hive_guc', 'hive_server');

-- Debug mode
SET datalake.external_table_debug = on;
SELECT COUNT(*) FROM fdw_hive_guc;
SET datalake.external_table_debug = off;

-- Disable cache
SET datalake.disable_cache_file = on;
SELECT COUNT(*) FROM fdw_hive_guc;
SET datalake.disable_cache_file = off;

-- Disable filter pushdown
SET datalake.disable_filter_pushdown = on;
SELECT * FROM fdw_hive_guc WHERE id = 1;
SET datalake.disable_filter_pushdown = off;

-- Limit segments
SET datalake.external_table_limit_segment_num = 1;
SELECT COUNT(*) FROM fdw_hive_guc;
SET datalake.external_table_limit_segment_num = 0;

-- Enable list in master
SET datalake.enable_list_in_master = on;
SELECT COUNT(*) FROM fdw_hive_guc;
SET datalake.enable_list_in_master = off;

-- Ignore hidden files
SET datalake.external_table_ignore_hidden_file = on;
SELECT COUNT(*) FROM fdw_hive_guc;
SET datalake.external_table_ignore_hidden_file = off;

DROP FOREIGN TABLE fdw_hive_guc;

-- ============================================================
-- Cleanup
-- ============================================================
DROP USER MAPPING IF EXISTS FOR gpadmin SERVER hive_server;
DROP SERVER IF EXISTS hive_server;
