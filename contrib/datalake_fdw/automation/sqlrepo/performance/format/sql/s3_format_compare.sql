-- s3_format_compare.sql
-- Scan, filter, and aggregate the same logical row set on S3 foreign tables
-- in ORC vs Parquet format. Output goes to perf_results_summary so runners
-- can compare per-op timings across formats.

\i ../../../lib/sql/common_setup.sql
\i ../../../lib/sql/performance_helpers.sql

\set iterations `echo "${PERF_ITERATIONS:-3}"`
\set warmup     `echo "${PERF_WARMUP_RUNS:-1}"`

DROP SERVER IF EXISTS fmt_s3_server CASCADE;
CREATE SERVER fmt_s3_server
    FOREIGN DATA WRAPPER datalake_fdw
    OPTIONS (host 'lakehouse', protocol 's3', isvirtual 'false', ishttps 'false');
CREATE USER MAPPING FOR gpadmin
    SERVER fmt_s3_server
    OPTIONS (user 'gpadmin', accesskey 'admin', secretkey 'password');

-- Foreign tables pointing at the same logical dataset in each format
DROP FOREIGN TABLE IF EXISTS fmt_orc;
CREATE FOREIGN TABLE fmt_orc (id int, name text, value numeric(10,2))
SERVER fmt_s3_server
OPTIONS (filePath '/warehouse/perf-data/orc/10k/', format 'orc');

DROP FOREIGN TABLE IF EXISTS fmt_parquet;
CREATE FOREIGN TABLE fmt_parquet (id int, name text, value numeric(10,2))
SERVER fmt_s3_server
OPTIONS (filePath '/warehouse/perf-data/parquet/10k/', format 'parquet');

-- Full scan
SELECT perf_run_iterations('format_compare', 'orc_full_scan',
    'SELECT COUNT(*) FROM fmt_orc', :iterations, :warmup, 10000, 'ORC full scan 10K');
SELECT perf_run_iterations('format_compare', 'parquet_full_scan',
    'SELECT COUNT(*) FROM fmt_parquet', :iterations, :warmup, 10000, 'Parquet full scan 10K');

-- Filter (10% selectivity)
SELECT perf_run_iterations('format_compare', 'orc_filter_scan',
    'SELECT COUNT(*) FROM fmt_orc WHERE id < 1000',
    :iterations, :warmup, 1000, 'ORC filter 10pct');
SELECT perf_run_iterations('format_compare', 'parquet_filter_scan',
    'SELECT COUNT(*) FROM fmt_parquet WHERE id < 1000',
    :iterations, :warmup, 1000, 'Parquet filter 10pct');

-- Aggregation
SELECT perf_run_iterations('format_compare', 'orc_aggregation',
    'SELECT AVG(value), SUM(value) FROM fmt_orc',
    :iterations, :warmup, 10000, 'ORC aggregation');
SELECT perf_run_iterations('format_compare', 'parquet_aggregation',
    'SELECT AVG(value), SUM(value) FROM fmt_parquet',
    :iterations, :warmup, 10000, 'Parquet aggregation');

-- Cleanup
DROP FOREIGN TABLE IF EXISTS fmt_orc;
DROP FOREIGN TABLE IF EXISTS fmt_parquet;
DROP SERVER fmt_s3_server CASCADE;
