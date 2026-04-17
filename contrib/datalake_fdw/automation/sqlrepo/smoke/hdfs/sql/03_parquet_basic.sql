-- Test 03: Parquet Format Basic
-- Purpose: Test basic read/write with PARQUET format
-- Data: 10 rows (minimal)
-- Expected time: ~20s

-- Test 1: Write parquet data
DROP EXTERNAL TABLE IF EXISTS test_parquet_write;
CREATE WRITABLE EXTERNAL TABLE test_parquet_write(
    id int,
    name varchar(50),
    col decimal(10,2),
    created_at timestamp
)
LOCATION('gphdfs://test/basic/parquet/write hdfs_cluster_name=paa_cluster')
FORMAT 'parquet';

INSERT INTO test_parquet_write
SELECT i, 'name_' || i, i * 2.5, NOW()
FROM generate_series(1, 10) i;

DROP EXTERNAL TABLE test_parquet_write;

-- Test 2: Read parquet data
DROP EXTERNAL TABLE IF EXISTS test_parquet_read;
CREATE READABLE EXTERNAL TABLE test_parquet_read(
    id int,
    name varchar(50),
    col decimal(10,2),
    created_at timestamp
)
LOCATION('gphdfs://test/basic/parquet/write hdfs_cluster_name=paa_cluster')
FORMAT 'parquet';

SELECT COUNT(*) FROM test_parquet_read;
SELECT SUM(col) FROM test_parquet_read;
SELECT MIN(id), MAX(id) FROM test_parquet_read;

DROP EXTERNAL TABLE test_parquet_read;
