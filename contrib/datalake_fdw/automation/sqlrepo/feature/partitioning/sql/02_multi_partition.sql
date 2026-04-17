-- 02_multi_partition.sql
-- Test multi-level partition columns functionality

\i ../../../lib/sql/common_setup.sql
\i ../../../lib/sql/hive_server_setup.sql

-- Create test schema
DROP SCHEMA IF EXISTS feature_test CASCADE;
CREATE SCHEMA feature_test;

SELECT test_log('Feature Test: Multi-Level Partitions');

-- Test 1: Sync multi-level partition table (year/month/day)
SELECT test_log('Test 1: Sync multi-level partition table');
SELECT public.sync_hive_table('hive_cluster','default','test_multi_partition','paa_cluster', 'feature_test.multi_part', 'hive_server');

-- Verify table structure
\d feature_test.multi_part

-- Test 2: Query all data
SELECT test_log('Test 2: Query all partitions');
SELECT COUNT(*) as total_rows FROM feature_test.multi_part;

-- Test 3: Query specific partition combination
SELECT test_log('Test 3: Query specific partition (year=2024, month=1)');
SELECT COUNT(*) as jan_2024_rows
FROM feature_test.multi_part
WHERE year = 2024 AND month = 1;

-- Test 4: Query with partial partition filter
SELECT test_log('Test 4: Query with partial partition filter (year=2024)');
SELECT month, day, COUNT(*) as row_count
FROM feature_test.multi_part
WHERE year = 2024
GROUP BY month, day
ORDER BY month, day;

-- Test 5: Verify partition pruning on multi-level partitions
SELECT test_log('Test 5: Verify multi-level partition pruning');
EXPLAIN (COSTS OFF)
SELECT * FROM feature_test.multi_part
WHERE year = 2024 AND month = 1 AND day = 15;

-- Test 6: Test partition ordering
SELECT test_log('Test 6: Test partition column ordering');
SELECT year, month, day, COUNT(*) as row_count
FROM feature_test.multi_part
GROUP BY year, month, day
ORDER BY year DESC, month DESC, day DESC
LIMIT 10;

-- Cleanup
DROP SCHEMA feature_test CASCADE;
