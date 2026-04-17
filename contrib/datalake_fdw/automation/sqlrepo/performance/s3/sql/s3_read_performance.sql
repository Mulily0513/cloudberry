-- s3_read_performance.sql
-- Performance tests for S3 table read operations

\i ../../../lib/sql/common_setup.sql
\i ../../../lib/sql/performance_helpers.sql

-- Create S3 server
DROP SERVER IF EXISTS s3_perf_server CASCADE;
CREATE SERVER s3_perf_server
    FOREIGN DATA WRAPPER datalake_fdw
    OPTIONS (host 'lakehouse', protocol 's3', isvirtual 'false', ishttps 'false');
CREATE USER MAPPING FOR gpadmin
    SERVER s3_perf_server
    OPTIONS (user 'gpadmin', accesskey 'admin', secretkey 'password');

SELECT test_log('S3 Read Performance Tests');

\set iterations `echo "${PERF_ITERATIONS:-3}"`
\set warmup     `echo "${PERF_WARMUP_RUNS:-1}"`

-- Foreign-table DDL is single-shot
DROP FOREIGN TABLE IF EXISTS s3_perf_orc;
CREATE FOREIGN TABLE s3_perf_orc (id int, name text, value numeric)
SERVER s3_perf_server
OPTIONS (filePath '/warehouse/perf-data/orc/10k/', format 'orc');

DROP FOREIGN TABLE IF EXISTS s3_perf_parquet;
CREATE FOREIGN TABLE s3_perf_parquet (id int, name text, value numeric)
SERVER s3_perf_server
OPTIONS (filePath '/warehouse/perf-data/parquet/10k/', format 'parquet');

DROP FOREIGN TABLE IF EXISTS s3_perf_multifile;
CREATE FOREIGN TABLE s3_perf_multifile (id int, data text)
SERVER s3_perf_server
OPTIONS (filePath '/warehouse/perf-data/multifile/', format 'orc');

SELECT test_log('Performance Test 1: ORC Format Scan');
SELECT perf_run_iterations(
    's3_read', 'orc_scan_10k',
    'SELECT COUNT(*) FROM s3_perf_orc',
    :iterations, :warmup, 10000, 'ORC format full scan 10K rows');

SELECT test_log('Performance Test 2: Parquet Format Scan');
SELECT perf_run_iterations(
    's3_read', 'parquet_scan_10k',
    'SELECT COUNT(*) FROM s3_perf_parquet',
    :iterations, :warmup, 10000, 'Parquet format full scan 10K rows');

SELECT test_log('Performance Test 3: Filtered ORC Scan');
SELECT perf_run_iterations(
    's3_read', 'orc_filtered_scan',
    'SELECT COUNT(*) FROM s3_perf_orc WHERE id < 1000',
    :iterations, :warmup, 1000, 'ORC filtered scan 10% selectivity');

SELECT test_log('Performance Test 4: Parquet Aggregation');
SELECT perf_run_iterations(
    's3_read', 'parquet_aggregation',
    'SELECT AVG(value), SUM(value), MIN(id), MAX(id) FROM s3_perf_parquet',
    :iterations, :warmup, NULL, 'Parquet aggregation query');

SELECT test_log('Performance Test 5: Multi-file Scan');
SELECT perf_run_iterations(
    's3_read', 'multifile_scan',
    'SELECT COUNT(*) FROM s3_perf_multifile',
    :iterations, :warmup, NULL, 'Multi-file directory scan');

-- Cleanup
DROP FOREIGN TABLE IF EXISTS s3_perf_orc;
DROP FOREIGN TABLE IF EXISTS s3_perf_parquet;
DROP FOREIGN TABLE IF EXISTS s3_perf_multifile;
DROP SERVER s3_perf_server CASCADE;
