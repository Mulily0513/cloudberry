-- 02_subqueries.sql
-- Test subquery support

\i ../../../lib/sql/common_setup.sql
\i ../../../lib/sql/hive_server_setup.sql

-- Create test schema
DROP SCHEMA IF EXISTS feature_test CASCADE;
CREATE SCHEMA feature_test;

SELECT test_log('Feature Test: Subqueries');

-- Setup
SELECT public.sync_hive_table('hive_cluster','default','test_orc','paa_cluster', 'feature_test.test_table', 'hive_server');

-- Test 1: Subquery in WHERE clause
SELECT test_log('Test 1: Subquery in WHERE');
SELECT COUNT(*) as rows_above_avg
FROM feature_test.test_table
WHERE id > (SELECT AVG(id) FROM feature_test.test_table);

-- Test 2: Subquery with IN
SELECT test_log('Test 2: Subquery with IN');
SELECT COUNT(*) as matching_rows
FROM feature_test.test_table
WHERE id IN (SELECT id FROM feature_test.test_table WHERE id < 10);

-- Test 3: Correlated subquery
SELECT test_log('Test 3: Correlated subquery');
SELECT id, name,
    (SELECT COUNT(*) FROM feature_test.test_table t2 WHERE t2.id <= t1.id) as running_count
FROM feature_test.test_table t1
ORDER BY id LIMIT 5;

-- Test 4: Subquery in FROM clause
SELECT test_log('Test 4: Derived table');
SELECT AVG(cnt) as avg_count
FROM (
    SELECT id, COUNT(*) as cnt
    FROM feature_test.test_table
    GROUP BY id
) sub;

-- Cleanup
DROP SCHEMA feature_test CASCADE;
