-- Test 04: ORC Format Basic
-- Purpose: Test basic read/write with ORC format
-- Data: 10 rows (minimal)
-- Expected time: ~20s

-- Test 1: Write ORC data
DROP EXTERNAL TABLE IF EXISTS test_orc_write;
CREATE WRITABLE EXTERNAL TABLE test_orc_write(
    id int,
    name varchar(50),
    col decimal(10,2),
    created_at timestamp
)
LOCATION('gphdfs://test/basic/orc/write hdfs_cluster_name=paa_cluster')
FORMAT 'orc';

INSERT INTO test_orc_write
SELECT i, 'name_' || i, i * 3.5, NOW()
FROM generate_series(1, 10) i;

DROP EXTERNAL TABLE test_orc_write;

-- Test 2: Read ORC data
DROP EXTERNAL TABLE IF EXISTS test_orc_read;
CREATE READABLE EXTERNAL TABLE test_orc_read(
    id int,
    name varchar(50),
    col decimal(10,2),
    created_at timestamp
)
LOCATION('gphdfs://test/basic/orc/write hdfs_cluster_name=paa_cluster')
FORMAT 'orc';

SELECT COUNT(*) FROM test_orc_read;
SELECT SUM(col) FROM test_orc_read;
SELECT MIN(id), MAX(id) FROM test_orc_read;

DROP EXTERNAL TABLE test_orc_read;
