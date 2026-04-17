-- iceberg_scaling.sql
-- Scan & aggregate on tables of increasing size. Writes 3 operations
-- per test_name ('scan' / 'agg') at each scale. Post-run, user can plot
-- (size, p50_ms) to visually check for super-linear growth.

\i ../../../lib/sql/common_setup.sql
\i ../../../lib/sql/performance_helpers.sql

\set iterations `echo "${PERF_ITERATIONS:-3}"`
\set warmup     `echo "${PERF_WARMUP_RUNS:-1}"`

DROP SERVER IF EXISTS sc_cat_srv CASCADE;
CREATE SERVER sc_cat_srv FOREIGN DATA WRAPPER iceberg_catalog_fdw;
CREATE USER MAPPING FOR current_user SERVER sc_cat_srv;
CREATE FOREIGN CATALOG sc_cat SERVER sc_cat_srv;
SET iceberg_default_catalog = 'sc_cat';

DROP SERVER IF EXISTS sc_vol_srv CASCADE;
CREATE SERVER sc_vol_srv FOREIGN DATA WRAPPER iceberg_volume_fdw
OPTIONS (type 's3', endpoint 'http://lakehouse:9100', region 'us-east-1',
         bucket_name 'warehouse', path_style_access 'true');
CREATE USER MAPPING FOR current_user SERVER sc_vol_srv
OPTIONS (access_key_id 'admin', secret_access_key 'password');
CREATE FOREIGN VOLUME sc_vol SERVER sc_vol_srv OPTIONS (base_path '/sc_test/');
SET iceberg_default_volume = 'sc_vol';

-- Small: 1000 rows
CREATE ICEBERG TABLE sc_small (id int, val numeric(10,2));
INSERT INTO sc_small SELECT g, g*1.1 FROM generate_series(1, 1000) AS g;

SELECT perf_run_iterations('iceberg_scaling', 'scan_1k',
    'SELECT COUNT(*) FROM sc_small', :iterations, :warmup, 1000, '1k rows scan');
SELECT perf_run_iterations('iceberg_scaling', 'agg_1k',
    'SELECT SUM(val), AVG(val) FROM sc_small', :iterations, :warmup, 1000, '1k rows agg');

-- Medium: 10k rows
CREATE ICEBERG TABLE sc_medium (id int, val numeric(10,2));
INSERT INTO sc_medium SELECT g, g*1.1 FROM generate_series(1, 10000) AS g;

SELECT perf_run_iterations('iceberg_scaling', 'scan_10k',
    'SELECT COUNT(*) FROM sc_medium', :iterations, :warmup, 10000, '10k rows scan');
SELECT perf_run_iterations('iceberg_scaling', 'agg_10k',
    'SELECT SUM(val), AVG(val) FROM sc_medium', :iterations, :warmup, 10000, '10k rows agg');

-- Large: 100k rows (CI kept small; nightly can override via PERF_DATA_SIZE)
CREATE ICEBERG TABLE sc_large (id int, val numeric(10,2));
INSERT INTO sc_large SELECT g, g*1.1 FROM generate_series(1, 100000) AS g;

SELECT perf_run_iterations('iceberg_scaling', 'scan_100k',
    'SELECT COUNT(*) FROM sc_large', :iterations, :warmup, 100000, '100k rows scan');
SELECT perf_run_iterations('iceberg_scaling', 'agg_100k',
    'SELECT SUM(val), AVG(val) FROM sc_large', :iterations, :warmup, 100000, '100k rows agg');

-- Cleanup
DROP TABLE sc_small; DROP TABLE sc_medium; DROP TABLE sc_large;
DROP VOLUME sc_vol;
DROP USER MAPPING FOR current_user SERVER sc_vol_srv;
DROP SERVER sc_vol_srv;
DROP CATALOG sc_cat;
DROP USER MAPPING FOR current_user SERVER sc_cat_srv;
DROP SERVER sc_cat_srv;
