-- iceberg_pushdown_compare.sql
-- Compare query timing with common planner toggles that affect pushdown
-- behaviour. The point isn't to measure absolute numbers (those vary); it's
-- to detect future regressions where a toggle STOPS mattering (because the
-- code path was accidentally removed or rewired).

\i ../../../lib/sql/common_setup.sql
\i ../../../lib/sql/performance_helpers.sql

\set iterations `echo "${PERF_ITERATIONS:-3}"`
\set warmup     `echo "${PERF_WARMUP_RUNS:-1}"`

DROP SERVER IF EXISTS pd_cat_srv CASCADE;
CREATE SERVER pd_cat_srv FOREIGN DATA WRAPPER iceberg_catalog_fdw;
CREATE USER MAPPING FOR current_user SERVER pd_cat_srv;
CREATE FOREIGN CATALOG pd_cat SERVER pd_cat_srv;
SET iceberg_default_catalog = 'pd_cat';

DROP SERVER IF EXISTS pd_vol_srv CASCADE;
CREATE SERVER pd_vol_srv FOREIGN DATA WRAPPER iceberg_volume_fdw
OPTIONS (type 's3', endpoint 'http://lakehouse:9100', region 'us-east-1',
         bucket_name 'warehouse', path_style_access 'true');
CREATE USER MAPPING FOR current_user SERVER pd_vol_srv
OPTIONS (access_key_id 'admin', secret_access_key 'password');
CREATE FOREIGN VOLUME pd_vol SERVER pd_vol_srv OPTIONS (base_path '/pd_test/');
SET iceberg_default_volume = 'pd_vol';

CREATE ICEBERG TABLE pd_tbl (id int, cat text, val numeric(10,2));
INSERT INTO pd_tbl SELECT g, 'cat_' || (g % 10), g*1.1
FROM generate_series(1, 5000) AS g;

-- Filter pushdown: equality on a high-selectivity column (id)
-- Baseline: planner defaults
RESET ALL;
SELECT perf_run_iterations('iceberg_pushdown', 'filter_eq_default',
    'SELECT COUNT(*) FROM pd_tbl WHERE id = 2500',
    :iterations, :warmup, 1, 'Filter equality, planner defaults');

-- Toggle: disable seqscan (forces optimizer to prefer other paths when possible)
SET enable_seqscan = off;
SELECT perf_run_iterations('iceberg_pushdown', 'filter_eq_no_seqscan',
    'SELECT COUNT(*) FROM pd_tbl WHERE id = 2500',
    :iterations, :warmup, 1, 'Filter equality, enable_seqscan=off');
RESET enable_seqscan;

-- Range filter (low selectivity)
SELECT perf_run_iterations('iceberg_pushdown', 'filter_range_default',
    'SELECT COUNT(*) FROM pd_tbl WHERE id BETWEEN 1000 AND 2000',
    :iterations, :warmup, 1001, 'Range filter, planner defaults');

-- Column projection (should avoid reading val when only id is needed)
SELECT perf_run_iterations('iceberg_pushdown', 'projection_narrow',
    'SELECT id FROM pd_tbl WHERE id < 100',
    :iterations, :warmup, 99, 'Narrow projection');
SELECT perf_run_iterations('iceberg_pushdown', 'projection_wide',
    'SELECT id, cat, val FROM pd_tbl WHERE id < 100',
    :iterations, :warmup, 99, 'Wide projection');

-- Cleanup
DROP TABLE pd_tbl;
DROP VOLUME pd_vol;
DROP USER MAPPING FOR current_user SERVER pd_vol_srv;
DROP SERVER pd_vol_srv;
DROP CATALOG pd_cat;
DROP USER MAPPING FOR current_user SERVER pd_cat_srv;
DROP SERVER pd_cat_srv;
