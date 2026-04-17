-- hdfs_throughput_performance.sql
-- Performance tests for HDFS throughput

\i ../../../lib/sql/common_setup.sql
\i ../../../lib/sql/performance_helpers.sql

-- Create HDFS server
DROP SERVER IF EXISTS hdfs_perf_server CASCADE;
CREATE SERVER hdfs_perf_server
    FOREIGN DATA WRAPPER datalake_fdw
    OPTIONS (
        protocol 'hdfs',
        hdfs_namenodes 'lakehouse',
        hdfs_port '8020',
        hdfs_auth_method 'simple',
        hadoop_rpc_protection 'authentication'
    );
CREATE USER MAPPING FOR gpadmin
        SERVER hdfs_perf_server
        OPTIONS (user 'gpadmin');

SELECT test_log('HDFS Throughput Performance Tests');

\set iterations `echo "${PERF_ITERATIONS:-3}"`
\set warmup     `echo "${PERF_WARMUP_RUNS:-1}"`

-- Foreign-table DDL is single-shot (not iterated)
DROP FOREIGN TABLE IF EXISTS hdfs_perf_text;
CREATE FOREIGN TABLE hdfs_perf_text (id int, data text)
SERVER hdfs_perf_server
OPTIONS (filePath 'hdfs://lakehouse:8020/perf-data/text/large.txt', format 'text');

DROP FOREIGN TABLE IF EXISTS hdfs_perf_orc;
CREATE FOREIGN TABLE hdfs_perf_orc (id int, name text, value numeric)
SERVER hdfs_perf_server
OPTIONS (filePath 'hdfs://lakehouse:8020/perf-data/orc/large/', format 'orc');

DROP FOREIGN TABLE IF EXISTS hdfs_perf_parquet;
CREATE FOREIGN TABLE hdfs_perf_parquet (id int, name text, value numeric)
SERVER hdfs_perf_server
OPTIONS (filePath 'hdfs://lakehouse:8020/perf-data/parquet/large/', format 'parquet');

SELECT test_log('Performance Test 1: Sequential Read (Text Format)');
SELECT perf_run_iterations(
    'hdfs_throughput', 'sequential_read_text',
    'SELECT COUNT(*) FROM hdfs_perf_text',
    :iterations, :warmup, NULL, 'Sequential text file read');

SELECT test_log('Performance Test 2: Sequential Read (ORC Format)');
SELECT perf_run_iterations(
    'hdfs_throughput', 'sequential_read_orc',
    'SELECT COUNT(*) FROM hdfs_perf_orc',
    :iterations, :warmup, NULL, 'Sequential ORC file read');

SELECT test_log('Performance Test 3: Sequential Read (Parquet Format)');
SELECT perf_run_iterations(
    'hdfs_throughput', 'sequential_read_parquet',
    'SELECT COUNT(*) FROM hdfs_perf_parquet',
    :iterations, :warmup, NULL, 'Sequential Parquet file read');

SELECT test_log('Performance Test 4: Aggregation Throughput');
SELECT perf_run_iterations(
    'hdfs_throughput', 'aggregation',
    'SELECT AVG(value), SUM(value), COUNT(*) FROM hdfs_perf_orc',
    :iterations, :warmup, NULL, 'Aggregation query throughput');

-- Cleanup
DROP FOREIGN TABLE IF EXISTS hdfs_perf_text;
DROP FOREIGN TABLE IF EXISTS hdfs_perf_orc;
DROP FOREIGN TABLE IF EXISTS hdfs_perf_parquet;
DROP SERVER hdfs_perf_server CASCADE;
