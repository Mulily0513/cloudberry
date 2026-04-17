-- 01_joins.sql
-- Test JOIN operations across Hive tables

\i ../../../lib/sql/common_setup.sql
\i ../../../lib/sql/hive_server_setup.sql

-- Create test schema
DROP SCHEMA IF EXISTS feature_test CASCADE;
CREATE SCHEMA feature_test;

SELECT test_log('Feature Test: JOIN Operations');

-- Setup: Sync two tables for joining
SELECT test_log('Setup: Sync test tables');
SELECT public.sync_hive_table('hive_cluster','default','test_orc','paa_cluster', 'feature_test.table_a', 'hive_server');
SELECT public.sync_hive_table('hive_cluster','default','test_parquet','paa_cluster', 'feature_test.table_b', 'hive_server');

-- Test 1: INNER JOIN
SELECT test_log('Test 1: INNER JOIN');
SELECT COUNT(*) as inner_join_count
FROM feature_test.table_a a
INNER JOIN feature_test.table_b b ON a.id = b.id;

-- Test 2: LEFT JOIN
SELECT test_log('Test 2: LEFT JOIN');
SELECT COUNT(*) as left_join_count,
       COUNT(b.id) as matched_count
FROM feature_test.table_a a
LEFT JOIN feature_test.table_b b ON a.id = b.id;

-- Test 3: Multiple JOIN conditions
SELECT test_log('Test 3: Multiple JOIN conditions');
SELECT COUNT(*) as multi_condition_join
FROM feature_test.table_a a
INNER JOIN feature_test.table_b b
    ON a.id = b.id AND a.name = b.name;

-- Test 4: Self JOIN
SELECT test_log('Test 4: Self JOIN');
SELECT COUNT(*) as self_join_count
FROM feature_test.table_a a1
INNER JOIN feature_test.table_a a2
    ON a1.id < a2.id;

-- Cleanup
DROP SCHEMA feature_test CASCADE;
