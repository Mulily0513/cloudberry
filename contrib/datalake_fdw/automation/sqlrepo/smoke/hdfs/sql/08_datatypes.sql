-- Test 08: Data Types
-- Purpose: Test various data types with different storage formats on HDFS
-- Expected time: ~40s

-- ============================================================
-- Test 1: Read pre-loaded parquet data (prepared by beeline)
-- Covers: tinyint, smallint, int, bigint, float, double, decimal,
--         string, boolean, date, timestamp
-- ============================================================
DROP EXTERNAL TABLE IF EXISTS test_preload_datatypes;
CREATE READABLE EXTERNAL TABLE test_preload_datatypes(
    id int,
    tiny_val smallint,
    small_val smallint,
    int_val int,
    big_val bigint,
    float_val float4,
    double_val float8,
    dec_val decimal(10,2),
    str_val text,
    bool_val boolean,
    date_val date,
    ts_val timestamp
)
LOCATION('gphdfs://test/basic/datatypes/preload_parquet hdfs_cluster_name=paa_cluster')
FORMAT 'parquet';

SELECT * FROM test_preload_datatypes ORDER BY id;
SELECT COUNT(*) FROM test_preload_datatypes;
SELECT COUNT(*) AS null_rows FROM test_preload_datatypes WHERE int_val IS NULL;

DROP EXTERNAL TABLE test_preload_datatypes;

-- ============================================================
-- Test 2: Integer types - write and read back (parquet)
-- ============================================================
DROP EXTERNAL TABLE IF EXISTS test_int_types_write;
CREATE WRITABLE EXTERNAL TABLE test_int_types_write(
    id int,
    small_val smallint,
    int_val int,
    big_val bigint
)
LOCATION('gphdfs://test/basic/datatypes/int_types hdfs_cluster_name=paa_cluster')
FORMAT 'parquet';

INSERT INTO test_int_types_write VALUES
    (1, 1, 100, 10000000000),
    (2, -1, -100, -10000000000),
    (3, 0, 0, 0),
    (4, 32767, 2147483647, 9223372036854775807),
    (5, -32768, -2147483648, -9223372036854775808);

DROP EXTERNAL TABLE test_int_types_write;

DROP EXTERNAL TABLE IF EXISTS test_int_types_read;
CREATE READABLE EXTERNAL TABLE test_int_types_read(
    id int,
    small_val smallint,
    int_val int,
    big_val bigint
)
LOCATION('gphdfs://test/basic/datatypes/int_types hdfs_cluster_name=paa_cluster')
FORMAT 'parquet';

SELECT * FROM test_int_types_read ORDER BY id;
SELECT COUNT(*) FROM test_int_types_read;

DROP EXTERNAL TABLE test_int_types_read;

-- ============================================================
-- Test 3: Float / Decimal types - write and read back (parquet)
-- ============================================================
DROP EXTERNAL TABLE IF EXISTS test_float_types_write;
CREATE WRITABLE EXTERNAL TABLE test_float_types_write(
    id int,
    float_val float4,
    double_val float8,
    dec_val decimal(10,5)
)
LOCATION('gphdfs://test/basic/datatypes/float_types hdfs_cluster_name=paa_cluster')
FORMAT 'parquet';

INSERT INTO test_float_types_write VALUES
    (1, 1.5, 1.123456789012345, 12345.67890),
    (2, -1.5, -1.123456789012345, -12345.67890),
    (3, 0.0, 0.0, 0.00000),
    (4, 3.14159, 3.141592653589793, 99999.99999),
    (5, 1.23e10, 1.23e100, 0.00001);

DROP EXTERNAL TABLE test_float_types_write;

DROP EXTERNAL TABLE IF EXISTS test_float_types_read;
CREATE READABLE EXTERNAL TABLE test_float_types_read(
    id int,
    float_val float4,
    double_val float8,
    dec_val decimal(10,5)
)
LOCATION('gphdfs://test/basic/datatypes/float_types hdfs_cluster_name=paa_cluster')
FORMAT 'parquet';

SELECT * FROM test_float_types_read ORDER BY id;
SELECT COUNT(*) FROM test_float_types_read;

DROP EXTERNAL TABLE test_float_types_read;

-- ============================================================
-- Test 4: String types - write and read back (parquet)
-- ============================================================
DROP EXTERNAL TABLE IF EXISTS test_string_types_write;
CREATE WRITABLE EXTERNAL TABLE test_string_types_write(
    id int,
    text_val text,
    varchar_val varchar(100)
)
LOCATION('gphdfs://test/basic/datatypes/string_types hdfs_cluster_name=paa_cluster')
FORMAT 'parquet';

INSERT INTO test_string_types_write VALUES
    (1, 'hello world', 'varchar_test'),
    (2, '', ''),
    (3, 'a', 'b'),
    (4, repeat('x', 100), repeat('y', 50)),
    (5, 'special: !@#$%^&*()', 'quotes: "double" and ''single''');

DROP EXTERNAL TABLE test_string_types_write;

DROP EXTERNAL TABLE IF EXISTS test_string_types_read;
CREATE READABLE EXTERNAL TABLE test_string_types_read(
    id int,
    text_val text,
    varchar_val varchar(100)
)
LOCATION('gphdfs://test/basic/datatypes/string_types hdfs_cluster_name=paa_cluster')
FORMAT 'parquet';

SELECT * FROM test_string_types_read ORDER BY id;
SELECT COUNT(*) FROM test_string_types_read;
SELECT length(text_val) AS text_len, length(varchar_val) AS varchar_len
FROM test_string_types_read ORDER BY id;

DROP EXTERNAL TABLE test_string_types_read;

-- ============================================================
-- Test 5: Date/Time types - write and read back (parquet)
-- ============================================================
DROP EXTERNAL TABLE IF EXISTS test_datetime_write;
CREATE WRITABLE EXTERNAL TABLE test_datetime_write(
    id int,
    date_val date,
    ts_val timestamp
)
LOCATION('gphdfs://test/basic/datatypes/datetime_types hdfs_cluster_name=paa_cluster')
FORMAT 'parquet';

INSERT INTO test_datetime_write VALUES
    (1, '2024-01-01', '2024-01-01 12:00:00'),
    (2, '1970-01-01', '1970-01-01 00:00:00'),
    (3, '2099-12-31', '2099-12-31 23:59:59'),
    (4, '2000-02-29', '2000-02-29 12:30:45'),
    (5, '1999-12-31', '1999-12-31 23:59:59');

DROP EXTERNAL TABLE test_datetime_write;

DROP EXTERNAL TABLE IF EXISTS test_datetime_read;
CREATE READABLE EXTERNAL TABLE test_datetime_read(
    id int,
    date_val date,
    ts_val timestamp
)
LOCATION('gphdfs://test/basic/datatypes/datetime_types hdfs_cluster_name=paa_cluster')
FORMAT 'parquet';

SELECT * FROM test_datetime_read ORDER BY id;
SELECT COUNT(*) FROM test_datetime_read;

DROP EXTERNAL TABLE test_datetime_read;

-- ============================================================
-- Test 6: Boolean type - write and read back (parquet)
-- ============================================================
DROP EXTERNAL TABLE IF EXISTS test_bool_write;
CREATE WRITABLE EXTERNAL TABLE test_bool_write(
    id int,
    bool_val boolean
)
LOCATION('gphdfs://test/basic/datatypes/bool_types hdfs_cluster_name=paa_cluster')
FORMAT 'parquet';

INSERT INTO test_bool_write VALUES
    (1, true),
    (2, false),
    (3, true),
    (4, false);

DROP EXTERNAL TABLE test_bool_write;

DROP EXTERNAL TABLE IF EXISTS test_bool_read;
CREATE READABLE EXTERNAL TABLE test_bool_read(
    id int,
    bool_val boolean
)
LOCATION('gphdfs://test/basic/datatypes/bool_types hdfs_cluster_name=paa_cluster')
FORMAT 'parquet';

SELECT * FROM test_bool_read ORDER BY id;
SELECT COUNT(*) AS total,
       COUNT(CASE WHEN bool_val THEN 1 END) AS true_count,
       COUNT(CASE WHEN NOT bool_val THEN 1 END) AS false_count
FROM test_bool_read;

DROP EXTERNAL TABLE test_bool_read;

-- ============================================================
-- Test 7: Mixed types via ORC format
-- Verify same types work with ORC, not just parquet
-- ============================================================
DROP EXTERNAL TABLE IF EXISTS test_orc_mixed_write;
CREATE WRITABLE EXTERNAL TABLE test_orc_mixed_write(
    id int,
    int_val bigint,
    float_val float8,
    dec_val decimal(10,2),
    str_val text,
    date_val date,
    ts_val timestamp
)
LOCATION('gphdfs://test/basic/datatypes/orc_mixed hdfs_cluster_name=paa_cluster')
FORMAT 'orc';

INSERT INTO test_orc_mixed_write VALUES
    (1, 9999999999, 3.14, 100.50, 'orc_test', '2024-06-15', '2024-06-15 10:30:00'),
    (2, -9999999999, -3.14, -100.50, 'negative', '1970-01-01', '1970-01-01 00:00:00'),
    (3, 0, 0.0, 0.00, '', '2000-01-01', '2000-01-01 12:00:00');

DROP EXTERNAL TABLE test_orc_mixed_write;

DROP EXTERNAL TABLE IF EXISTS test_orc_mixed_read;
CREATE READABLE EXTERNAL TABLE test_orc_mixed_read(
    id int,
    int_val bigint,
    float_val float8,
    dec_val decimal(10,2),
    str_val text,
    date_val date,
    ts_val timestamp
)
LOCATION('gphdfs://test/basic/datatypes/orc_mixed hdfs_cluster_name=paa_cluster')
FORMAT 'orc';

SELECT * FROM test_orc_mixed_read ORDER BY id;
SELECT COUNT(*) FROM test_orc_mixed_read;

DROP EXTERNAL TABLE test_orc_mixed_read;
