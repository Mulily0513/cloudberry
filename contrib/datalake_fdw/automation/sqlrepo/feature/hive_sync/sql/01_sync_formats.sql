-- 01_sync_formats.sql
-- Test Hive sync across different table formats

\i ../../../lib/sql/common_setup.sql
\i ../../../lib/sql/hive_server_setup.sql

DROP SCHEMA IF EXISTS feature_test CASCADE;
CREATE SCHEMA feature_test;

SELECT test_log('Feature Test: Hive Sync Formats');

-- ============================================================
-- Test 1: Sync Parquet table
-- ============================================================
SELECT test_log('Test 1: Sync test_parquet');

SELECT public.sync_hive_table('hive_cluster', 'default', 'test_parquet', 'paa_cluster', 'feature_test.sync_parquet', 'hive_server');
SELECT * FROM feature_test.sync_parquet ORDER BY 1 LIMIT 10;
SELECT COUNT(*) AS parquet_count FROM feature_test.sync_parquet;

-- ============================================================
-- Test 2: Sync ORC table
-- ============================================================
SELECT test_log('Test 2: Sync test_orc');

SELECT public.sync_hive_table('hive_cluster', 'default', 'test_orc', 'paa_cluster', 'feature_test.sync_orc', 'hive_server');
SELECT * FROM feature_test.sync_orc ORDER BY 1 LIMIT 10;
SELECT COUNT(*) AS orc_count FROM feature_test.sync_orc;

-- ============================================================
-- Test 3: Sync Text table
-- ============================================================
SELECT test_log('Test 3: Sync test_text');

SELECT public.sync_hive_table('hive_cluster', 'default', 'test_text', 'paa_cluster', 'feature_test.sync_text', 'hive_server');
SELECT * FROM feature_test.sync_text ORDER BY 1 LIMIT 10;
SELECT COUNT(*) AS text_count FROM feature_test.sync_text;

-- ============================================================
-- Test 4: Sync empty table
-- ============================================================
SELECT test_log('Test 4: Sync test_empty');

SELECT public.sync_hive_table('hive_cluster', 'default', 'test_empty', 'paa_cluster', 'feature_test.sync_empty', 'hive_server');
SELECT COUNT(*) AS empty_count FROM feature_test.sync_empty;

-- ============================================================
-- Test 5: Table structure of synced table
-- ============================================================
SELECT test_log('Test 5: Table structure');

\d feature_test.sync_parquet

-- ============================================================
-- Test 6: Re-sync same table to new alias
-- ============================================================
SELECT test_log('Test 6: Re-sync test_parquet to new alias');

SELECT public.sync_hive_table('hive_cluster', 'default', 'test_parquet', 'paa_cluster', 'feature_test.sync_parquet_v2', 'hive_server');
SELECT COUNT(*) AS resync_count FROM feature_test.sync_parquet_v2;

-- Verify data matches original
SELECT (SELECT COUNT(*) FROM feature_test.sync_parquet) = (SELECT COUNT(*) FROM feature_test.sync_parquet_v2) AS counts_match;

-- ============================================================
-- Test 7: Aggregates on synced table
-- ============================================================
SELECT test_log('Test 7: Aggregates on synced table');

SELECT COUNT(*) AS cnt FROM feature_test.sync_parquet;

-- ============================================================
-- Cleanup
-- ============================================================
DROP SCHEMA feature_test CASCADE;
