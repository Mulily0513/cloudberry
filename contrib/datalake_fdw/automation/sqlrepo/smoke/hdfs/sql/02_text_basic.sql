-- Test 02: Text Format Basic
-- Purpose: Test basic read/write with TEXT format
-- Data: 10 rows (minimal)
-- Expected time: ~20s

-- Test 1: Simple read from single file
DROP EXTERNAL TABLE IF EXISTS test_text_read;
CREATE READABLE EXTERNAL TABLE test_text_read(id int, col text, col2 decimal(10,2))
LOCATION('gphdfs://test/basic/text/simple hdfs_cluster_name=paa_cluster')
FORMAT 'text';

SELECT * FROM test_text_read ORDER BY id LIMIT 5;
SELECT COUNT(*) FROM test_text_read;

DROP EXTERNAL TABLE test_text_read;

-- Test 2: Write and read back
DROP EXTERNAL TABLE IF EXISTS test_text_write;
CREATE WRITABLE EXTERNAL TABLE test_text_write(id int, col text, col2 decimal(10,2))
LOCATION('gphdfs://test/basic/text/write hdfs_cluster_name=paa_cluster')
FORMAT 'text' (delimiter ',');

INSERT INTO test_text_write SELECT i, 'name_' || i, i * 1.5 FROM generate_series(1, 10) i;

DROP EXTERNAL TABLE test_text_write;

-- Read back
DROP EXTERNAL TABLE IF EXISTS test_text_readback;
CREATE READABLE EXTERNAL TABLE test_text_readback(id int, col text, col2 decimal(10,2))
LOCATION('gphdfs://test/basic/text/write hdfs_cluster_name=paa_cluster')
FORMAT 'text' (delimiter ',');

SELECT COUNT(*) FROM test_text_readback;
SELECT SUM(col2) FROM test_text_readback;

DROP EXTERNAL TABLE test_text_readback;
