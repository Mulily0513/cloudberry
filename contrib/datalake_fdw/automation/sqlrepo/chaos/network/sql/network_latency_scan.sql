-- network_latency_scan.sql
-- Inject 500ms network latency via tc netem, run an Iceberg scan, verify it
-- completes (slower but correct), then remove the latency and verify normal speed.

\i ../../../lib/sql/common_setup.sql
\i ../../../lib/sql/performance_helpers.sql

\set iterations `echo "${PERF_ITERATIONS:-2}"`

DROP SERVER IF EXISTS nl_cat_srv CASCADE;
CREATE SERVER nl_cat_srv FOREIGN DATA WRAPPER iceberg_catalog_fdw;
CREATE USER MAPPING FOR current_user SERVER nl_cat_srv;
CREATE FOREIGN CATALOG nl_cat SERVER nl_cat_srv;
SET iceberg_default_catalog = 'nl_cat';

DROP SERVER IF EXISTS nl_vol_srv CASCADE;
CREATE SERVER nl_vol_srv FOREIGN DATA WRAPPER iceberg_volume_fdw
OPTIONS (type 's3', endpoint 'http://lakehouse:9100', region 'us-east-1',
         bucket_name 'warehouse', path_style_access 'true');
CREATE USER MAPPING FOR current_user SERVER nl_vol_srv
OPTIONS (access_key_id 'admin', secret_access_key 'password');
CREATE FOREIGN VOLUME nl_vol SERVER nl_vol_srv OPTIONS (base_path '/nl_test/');
SET iceberg_default_volume = 'nl_vol';

CREATE ICEBERG TABLE netlag_tbl (id int, val numeric(10,2));
INSERT INTO netlag_tbl SELECT g, g*1.5 FROM generate_series(1, 500) AS g;

-- Baseline scan (no latency)
SELECT perf_run_iterations('chaos_network', 'scan_baseline',
    'SELECT COUNT(*) FROM netlag_tbl', :iterations, 0, 500, 'No latency baseline');

-- Inject 500ms latency on eth0
\! bash ../../../../lib/scripts/chaos/network_inject.sh add eth0 500

-- Scan under latency - should complete, just slower
SET statement_timeout = '60s';
SELECT perf_run_iterations('chaos_network', 'scan_500ms_latency',
    'SELECT COUNT(*) FROM netlag_tbl', :iterations, 0, 500, 'With 500ms latency');
RESET statement_timeout;

-- Remove latency
\! bash ../../../../lib/scripts/chaos/network_inject.sh del eth0

-- Post-recovery scan
SELECT perf_run_iterations('chaos_network', 'scan_post_recovery',
    'SELECT COUNT(*) FROM netlag_tbl', :iterations, 0, 500, 'After latency removed');

-- Cleanup
DROP TABLE netlag_tbl;
DROP VOLUME nl_vol;
DROP USER MAPPING FOR current_user SERVER nl_vol_srv;
DROP SERVER nl_vol_srv;
DROP CATALOG nl_cat;
DROP USER MAPPING FOR current_user SERVER nl_cat_srv;
DROP SERVER nl_cat_srv;
