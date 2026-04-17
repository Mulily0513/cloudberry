-- Test 07: Avro Format Basic
-- Purpose: Test basic read/write with AVRO format via HDFS
-- Data: 10 rows (minimal)
-- Expected time: ~20s

-- Test 1: Write Avro data
DROP EXTERNAL TABLE IF EXISTS test_avro_write;
CREATE WRITABLE EXTERNAL TABLE test_avro_write(
    id int,
    name varchar(50),
    col decimal(10,2),
    created_at timestamp
)
LOCATION('gphdfs://test/basic/avro/write hdfs_cluster_name=paa_cluster')
FORMAT 'avro';

INSERT INTO test_avro_write
SELECT i, 'name_' || i, i * 4.5, NOW()
FROM generate_series(1, 10) i;

DROP EXTERNAL TABLE test_avro_write;

-- Test 2: Read Avro data back
DROP EXTERNAL TABLE IF EXISTS test_avro_read;
CREATE READABLE EXTERNAL TABLE test_avro_read(
    id int,
    name varchar(50),
    col decimal(10,2),
    created_at timestamp
)
LOCATION('gphdfs://test/basic/avro/write hdfs_cluster_name=paa_cluster')
FORMAT 'avro';

SELECT COUNT(*) FROM test_avro_read;
SELECT SUM(col) FROM test_avro_read;
SELECT MIN(id), MAX(id) FROM test_avro_read;

DROP EXTERNAL TABLE test_avro_read;

-- Test 3: Avro with NULL values
DROP EXTERNAL TABLE IF EXISTS test_avro_null_write;
CREATE WRITABLE EXTERNAL TABLE test_avro_null_write(
    id int,
    name text,
    val int
)
LOCATION('gphdfs://test/basic/avro/nulls hdfs_cluster_name=paa_cluster')
FORMAT 'avro';

INSERT INTO test_avro_null_write VALUES (1, 'has_val', 100);
INSERT INTO test_avro_null_write VALUES (2, NULL, NULL);
INSERT INTO test_avro_null_write VALUES (3, 'zero', 0);

DROP EXTERNAL TABLE test_avro_null_write;

DROP EXTERNAL TABLE IF EXISTS test_avro_null_read;
CREATE READABLE EXTERNAL TABLE test_avro_null_read(
    id int,
    name text,
    val int
)
LOCATION('gphdfs://test/basic/avro/nulls hdfs_cluster_name=paa_cluster')
FORMAT 'avro';

SELECT * FROM test_avro_null_read ORDER BY id;
SELECT COUNT(*) FROM test_avro_null_read WHERE name IS NULL;
SELECT COUNT(*) FROM test_avro_null_read WHERE val IS NULL;

DROP EXTERNAL TABLE test_avro_null_read;
