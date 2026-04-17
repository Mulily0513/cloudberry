-- Test 09: Edge Cases
-- Purpose: Test boundary conditions, NULLs, extreme values, and cross-format consistency
-- Expected time: ~50s

-- ============================================================
-- Test 1: Read pre-loaded edge case data (prepared by beeline, ORC)
-- Covers: empty string vs NULL, zero, extreme ints, decimal precision,
--         date boundaries, whitespace
-- ============================================================
DROP EXTERNAL TABLE IF EXISTS test_preload_edge;
CREATE READABLE EXTERNAL TABLE test_preload_edge(
    id int,
    empty_val text,
    null_val text,
    zero_int int,
    zero_float float8,
    dec_precise decimal(18,6),
    date_boundary date,
    ts_boundary timestamp
)
LOCATION('gphdfs://test/basic/edge/preload_orc hdfs_cluster_name=paa_cluster')
FORMAT 'orc';

SELECT * FROM test_preload_edge ORDER BY id;
SELECT id,
       empty_val IS NULL AS empty_is_null,
       null_val IS NULL AS null_is_null
FROM test_preload_edge ORDER BY id;
SELECT COUNT(*) FROM test_preload_edge;

DROP EXTERNAL TABLE test_preload_edge;

-- ============================================================
-- Test 2: NULL values in all types (parquet write + read)
-- ============================================================
DROP EXTERNAL TABLE IF EXISTS test_null_write;
CREATE WRITABLE EXTERNAL TABLE test_null_write(
    id int,
    int_val int,
    big_val bigint,
    float_val float8,
    dec_val decimal(10,2),
    text_val text,
    date_val date,
    ts_val timestamp,
    bool_val boolean
)
LOCATION('gphdfs://test/basic/edge/nulls hdfs_cluster_name=paa_cluster')
FORMAT 'parquet';

INSERT INTO test_null_write VALUES (1, 100, 999999, 1.5, 99.99, 'not_null', '2024-01-01', '2024-01-01 12:00:00', true);
INSERT INTO test_null_write VALUES (2, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO test_null_write VALUES (3, 0, 0, 0.0, 0.00, '', '1970-01-01', '1970-01-01 00:00:00', false);

DROP EXTERNAL TABLE test_null_write;

DROP EXTERNAL TABLE IF EXISTS test_null_read;
CREATE READABLE EXTERNAL TABLE test_null_read(
    id int,
    int_val int,
    big_val bigint,
    float_val float8,
    dec_val decimal(10,2),
    text_val text,
    date_val date,
    ts_val timestamp,
    bool_val boolean
)
LOCATION('gphdfs://test/basic/edge/nulls hdfs_cluster_name=paa_cluster')
FORMAT 'parquet';

SELECT * FROM test_null_read ORDER BY id;
SELECT COUNT(*) AS total,
       COUNT(int_val) AS non_null_int,
       COUNT(big_val) AS non_null_big,
       COUNT(float_val) AS non_null_float,
       COUNT(dec_val) AS non_null_dec,
       COUNT(text_val) AS non_null_text,
       COUNT(date_val) AS non_null_date,
       COUNT(ts_val) AS non_null_ts,
       COUNT(bool_val) AS non_null_bool
FROM test_null_read;

DROP EXTERNAL TABLE test_null_read;

-- ============================================================
-- Test 3: Extreme integer values (parquet)
-- ============================================================
DROP EXTERNAL TABLE IF EXISTS test_extreme_write;
CREATE WRITABLE EXTERNAL TABLE test_extreme_write(
    id int,
    small_val smallint,
    int_val int,
    big_val bigint
)
LOCATION('gphdfs://test/basic/edge/extreme hdfs_cluster_name=paa_cluster')
FORMAT 'parquet';

INSERT INTO test_extreme_write VALUES (1, 32767, 2147483647, 9223372036854775807);
INSERT INTO test_extreme_write VALUES (2, -32768, -2147483648, -9223372036854775808);
INSERT INTO test_extreme_write VALUES (3, 0, 0, 0);
INSERT INTO test_extreme_write VALUES (4, 1, 1, 1);
INSERT INTO test_extreme_write VALUES (5, -1, -1, -1);

DROP EXTERNAL TABLE test_extreme_write;

DROP EXTERNAL TABLE IF EXISTS test_extreme_read;
CREATE READABLE EXTERNAL TABLE test_extreme_read(
    id int,
    small_val smallint,
    int_val int,
    big_val bigint
)
LOCATION('gphdfs://test/basic/edge/extreme hdfs_cluster_name=paa_cluster')
FORMAT 'parquet';

SELECT * FROM test_extreme_read ORDER BY id;
SELECT MIN(small_val), MAX(small_val) FROM test_extreme_read;
SELECT MIN(int_val), MAX(int_val) FROM test_extreme_read;
SELECT MIN(big_val), MAX(big_val) FROM test_extreme_read;

DROP EXTERNAL TABLE test_extreme_read;

-- ============================================================
-- Test 4: Decimal precision (parquet)
-- ============================================================
DROP EXTERNAL TABLE IF EXISTS test_decimal_write;
CREATE WRITABLE EXTERNAL TABLE test_decimal_write(
    id int,
    dec5_2 decimal(5,2),
    dec10_5 decimal(10,5),
    dec18_6 decimal(18,6)
)
LOCATION('gphdfs://test/basic/edge/decimal hdfs_cluster_name=paa_cluster')
FORMAT 'parquet';

INSERT INTO test_decimal_write VALUES (1, 123.45, 12345.67890, 123456789012.123456);
INSERT INTO test_decimal_write VALUES (2, 0.01, 0.00001, 0.000001);
INSERT INTO test_decimal_write VALUES (3, -999.99, -99999.99999, -999999999999.999999);
INSERT INTO test_decimal_write VALUES (4, 0.00, 0.00000, 0.000000);

DROP EXTERNAL TABLE test_decimal_write;

DROP EXTERNAL TABLE IF EXISTS test_decimal_read;
CREATE READABLE EXTERNAL TABLE test_decimal_read(
    id int,
    dec5_2 decimal(5,2),
    dec10_5 decimal(10,5),
    dec18_6 decimal(18,6)
)
LOCATION('gphdfs://test/basic/edge/decimal hdfs_cluster_name=paa_cluster')
FORMAT 'parquet';

SELECT * FROM test_decimal_read ORDER BY id;
SELECT SUM(dec5_2), SUM(dec10_5), SUM(dec18_6) FROM test_decimal_read;

DROP EXTERNAL TABLE test_decimal_read;

-- ============================================================
-- Test 5: Large batch insert (parquet, 1000 rows)
-- ============================================================
DROP EXTERNAL TABLE IF EXISTS test_large_write;
CREATE WRITABLE EXTERNAL TABLE test_large_write(
    id int,
    name text,
    val decimal(10,2)
)
LOCATION('gphdfs://test/basic/edge/large hdfs_cluster_name=paa_cluster')
FORMAT 'parquet';

INSERT INTO test_large_write
SELECT i, 'name_' || i, (i * 1.23)::decimal(10,2)
FROM generate_series(1, 1000) i;

DROP EXTERNAL TABLE test_large_write;

DROP EXTERNAL TABLE IF EXISTS test_large_read;
CREATE READABLE EXTERNAL TABLE test_large_read(
    id int,
    name text,
    val decimal(10,2)
)
LOCATION('gphdfs://test/basic/edge/large hdfs_cluster_name=paa_cluster')
FORMAT 'parquet';

SELECT COUNT(*) FROM test_large_read;
SELECT MIN(id), MAX(id) FROM test_large_read;
SELECT COUNT(DISTINCT name) FROM test_large_read;
SELECT SUM(val) FROM test_large_read;

DROP EXTERNAL TABLE test_large_read;

-- ============================================================
-- Test 6: Cross-format comparison
-- Write same data in parquet, ORC, text; verify all return same count
-- ============================================================

-- Parquet
DROP EXTERNAL TABLE IF EXISTS test_xfmt_pq_w;
CREATE WRITABLE EXTERNAL TABLE test_xfmt_pq_w(id int, name text, val decimal(10,2))
LOCATION('gphdfs://test/basic/edge/xfmt/parquet hdfs_cluster_name=paa_cluster')
FORMAT 'parquet';
INSERT INTO test_xfmt_pq_w SELECT i, 'test_' || i, i * 1.5 FROM generate_series(1, 20) i;
DROP EXTERNAL TABLE test_xfmt_pq_w;

-- ORC
DROP EXTERNAL TABLE IF EXISTS test_xfmt_orc_w;
CREATE WRITABLE EXTERNAL TABLE test_xfmt_orc_w(id int, name text, val decimal(10,2))
LOCATION('gphdfs://test/basic/edge/xfmt/orc hdfs_cluster_name=paa_cluster')
FORMAT 'orc';
INSERT INTO test_xfmt_orc_w SELECT i, 'test_' || i, i * 1.5 FROM generate_series(1, 20) i;
DROP EXTERNAL TABLE test_xfmt_orc_w;

-- Text
DROP EXTERNAL TABLE IF EXISTS test_xfmt_text_w;
CREATE WRITABLE EXTERNAL TABLE test_xfmt_text_w(id int, name text, val decimal(10,2))
LOCATION('gphdfs://test/basic/edge/xfmt/text hdfs_cluster_name=paa_cluster')
FORMAT 'text' (delimiter ',');
INSERT INTO test_xfmt_text_w SELECT i, 'test_' || i, i * 1.5 FROM generate_series(1, 20) i;
DROP EXTERNAL TABLE test_xfmt_text_w;

-- Avro
DROP EXTERNAL TABLE IF EXISTS test_xfmt_avro_w;
CREATE WRITABLE EXTERNAL TABLE test_xfmt_avro_w(id int, name text, val decimal(10,2))
LOCATION('gphdfs://test/basic/edge/xfmt/avro hdfs_cluster_name=paa_cluster')
FORMAT 'avro';
INSERT INTO test_xfmt_avro_w SELECT i, 'test_' || i, i * 1.5 FROM generate_series(1, 20) i;
DROP EXTERNAL TABLE test_xfmt_avro_w;

-- Read all and compare counts
DROP EXTERNAL TABLE IF EXISTS test_xfmt_pq_r;
CREATE READABLE EXTERNAL TABLE test_xfmt_pq_r(id int, name text, val decimal(10,2))
LOCATION('gphdfs://test/basic/edge/xfmt/parquet hdfs_cluster_name=paa_cluster')
FORMAT 'parquet';

DROP EXTERNAL TABLE IF EXISTS test_xfmt_orc_r;
CREATE READABLE EXTERNAL TABLE test_xfmt_orc_r(id int, name text, val decimal(10,2))
LOCATION('gphdfs://test/basic/edge/xfmt/orc hdfs_cluster_name=paa_cluster')
FORMAT 'orc';

DROP EXTERNAL TABLE IF EXISTS test_xfmt_text_r;
CREATE READABLE EXTERNAL TABLE test_xfmt_text_r(id int, name text, val decimal(10,2))
LOCATION('gphdfs://test/basic/edge/xfmt/text hdfs_cluster_name=paa_cluster')
FORMAT 'text' (delimiter ',');

DROP EXTERNAL TABLE IF EXISTS test_xfmt_avro_r;
CREATE READABLE EXTERNAL TABLE test_xfmt_avro_r(id int, name text, val decimal(10,2))
LOCATION('gphdfs://test/basic/edge/xfmt/avro hdfs_cluster_name=paa_cluster')
FORMAT 'avro';

SELECT 'parquet' AS format, COUNT(*) FROM test_xfmt_pq_r
UNION ALL
SELECT 'orc' AS format, COUNT(*) FROM test_xfmt_orc_r
UNION ALL
SELECT 'text' AS format, COUNT(*) FROM test_xfmt_text_r
UNION ALL
SELECT 'avro' AS format, COUNT(*) FROM test_xfmt_avro_r
ORDER BY format;

-- Verify aggregation consistency across formats
SELECT 'parquet' AS format, SUM(val) FROM test_xfmt_pq_r
UNION ALL
SELECT 'orc' AS format, SUM(val) FROM test_xfmt_orc_r
UNION ALL
SELECT 'text' AS format, SUM(val) FROM test_xfmt_text_r
UNION ALL
SELECT 'avro' AS format, SUM(val) FROM test_xfmt_avro_r
ORDER BY format;

DROP EXTERNAL TABLE test_xfmt_pq_r;
DROP EXTERNAL TABLE test_xfmt_orc_r;
DROP EXTERNAL TABLE test_xfmt_text_r;
DROP EXTERNAL TABLE test_xfmt_avro_r;

-- ============================================================
-- Test 7: WHERE clause and aggregation on HDFS data
-- ============================================================
DROP EXTERNAL TABLE IF EXISTS test_filter_write;
CREATE WRITABLE EXTERNAL TABLE test_filter_write(
    id int,
    category text,
    amount decimal(10,2)
)
LOCATION('gphdfs://test/basic/edge/filter hdfs_cluster_name=paa_cluster')
FORMAT 'parquet';

INSERT INTO test_filter_write
SELECT i,
       CASE WHEN i % 3 = 0 THEN 'A' WHEN i % 3 = 1 THEN 'B' ELSE 'C' END,
       (i * 10.5)::decimal(10,2)
FROM generate_series(1, 30) i;

DROP EXTERNAL TABLE test_filter_write;

DROP EXTERNAL TABLE IF EXISTS test_filter_read;
CREATE READABLE EXTERNAL TABLE test_filter_read(
    id int,
    category text,
    amount decimal(10,2)
)
LOCATION('gphdfs://test/basic/edge/filter hdfs_cluster_name=paa_cluster')
FORMAT 'parquet';

-- Filter tests
SELECT COUNT(*) FROM test_filter_read WHERE category = 'A';
SELECT COUNT(*) FROM test_filter_read WHERE category = 'B';
SELECT COUNT(*) FROM test_filter_read WHERE category = 'C';
SELECT COUNT(*) FROM test_filter_read WHERE id BETWEEN 10 AND 20;
SELECT COUNT(*) FROM test_filter_read WHERE amount > 200.00;

-- Aggregation tests
SELECT category, COUNT(*), SUM(amount), MIN(amount), MAX(amount)
FROM test_filter_read
GROUP BY category
ORDER BY category;

DROP EXTERNAL TABLE test_filter_read;
