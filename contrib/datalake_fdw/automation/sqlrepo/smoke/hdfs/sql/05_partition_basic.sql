-- Test 05: Partition Table Basic
-- Purpose: Test basic partition table operations
-- Data: 3 partitions x 5 rows = 15 rows
-- Expected time: ~30s

-- Test 1: Create partitioned table via Hive
-- Assuming hive_connector extension is available
DROP EXTERNAL TABLE IF EXISTS test_partition_table;

-- Sync from Hive (if table exists) or create directly
-- For basic test, we'll use gphdfs with partition path

-- Write to partition p1
DROP EXTERNAL TABLE IF EXISTS test_part_write_p1;
CREATE WRITABLE EXTERNAL TABLE test_part_write_p1(id int, name text, col decimal(10,2))
LOCATION('gphdfs://test/basic/partition/data/p=1 hdfs_cluster_name=paa_cluster')
FORMAT 'text' (delimiter ',');

INSERT INTO test_part_write_p1 SELECT i, 'p1_' || i, i * 1.0 FROM generate_series(1, 5) i;
DROP EXTERNAL TABLE test_part_write_p1;

-- Write to partition p2
DROP EXTERNAL TABLE IF EXISTS test_part_write_p2;
CREATE WRITABLE EXTERNAL TABLE test_part_write_p2(id int, name text, col decimal(10,2))
LOCATION('gphdfs://test/basic/partition/data/p=2 hdfs_cluster_name=paa_cluster')
FORMAT 'text' (delimiter ',');

INSERT INTO test_part_write_p2 SELECT i, 'p2_' || i, i * 2.0 FROM generate_series(1, 5) i;
DROP EXTERNAL TABLE test_part_write_p2;

-- Write to partition p3
DROP EXTERNAL TABLE IF EXISTS test_part_write_p3;
CREATE WRITABLE EXTERNAL TABLE test_part_write_p3(id int, name text, col decimal(10,2))
LOCATION('gphdfs://test/basic/partition/data/p=3 hdfs_cluster_name=paa_cluster')
FORMAT 'text' (delimiter ',');

INSERT INTO test_part_write_p3 SELECT i, 'p3_' || i, i * 3.0 FROM generate_series(1, 5) i;
DROP EXTERNAL TABLE test_part_write_p3;

-- Test 2: Read all partitions
DROP EXTERNAL TABLE IF EXISTS test_part_read_all;
CREATE READABLE EXTERNAL TABLE test_part_read_all(id int, name text, col decimal(10,2))
LOCATION('gphdfs://test/basic/partition/data hdfs_cluster_name=paa_cluster')
FORMAT 'text' (delimiter ',');

SELECT COUNT(*) FROM test_part_read_all;
SELECT SUM(col) FROM test_part_read_all;

DROP EXTERNAL TABLE test_part_read_all;

-- Test 3: Read specific partition
DROP EXTERNAL TABLE IF EXISTS test_part_read_p2;
CREATE READABLE EXTERNAL TABLE test_part_read_p2(id int, name text, col decimal(10,2))
LOCATION('gphdfs://test/basic/partition/data/p=2 hdfs_cluster_name=paa_cluster')
FORMAT 'text' (delimiter ',');

SELECT COUNT(*) FROM test_part_read_p2;

DROP EXTERNAL TABLE test_part_read_p2;
