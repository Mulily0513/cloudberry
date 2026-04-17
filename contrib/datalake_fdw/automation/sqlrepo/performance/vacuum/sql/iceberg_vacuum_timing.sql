-- iceberg_vacuum_timing.sql
-- Build up many small data files via repeated small INSERTs, then time VACUUM
-- (compaction). Note: VACUUM cannot run inside PL/pgSQL, so we use bare SQL
-- + perf_start_timer/perf_end_timer (single-shot, not iterated) for the
-- VACUUM measurement itself. Build-up phase uses perf_run_iterations.

\i ../../../lib/sql/common_setup.sql
\i ../../../lib/sql/performance_helpers.sql

\set iterations `echo "${PERF_ITERATIONS:-3}"`
\set warmup     `echo "${PERF_WARMUP_RUNS:-1}"`

DROP SERVER IF EXISTS vac_cat_srv CASCADE;
CREATE SERVER vac_cat_srv FOREIGN DATA WRAPPER iceberg_catalog_fdw;
CREATE USER MAPPING FOR current_user SERVER vac_cat_srv;
CREATE FOREIGN CATALOG vac_cat SERVER vac_cat_srv;
SET iceberg_default_catalog = 'vac_cat';

DROP SERVER IF EXISTS vac_vol_srv CASCADE;
CREATE SERVER vac_vol_srv FOREIGN DATA WRAPPER iceberg_volume_fdw
OPTIONS (type 's3', endpoint 'http://lakehouse:9100', region 'us-east-1',
         bucket_name 'warehouse', path_style_access 'true');
CREATE USER MAPPING FOR current_user SERVER vac_vol_srv
OPTIONS (access_key_id 'admin', secret_access_key 'password');
CREATE FOREIGN VOLUME vac_vol SERVER vac_vol_srv OPTIONS (base_path '/vac_perf/');
SET iceberg_default_volume = 'vac_vol';

CREATE ICEBERG TABLE vac_timing (id int, val numeric(10,2));

-- Build 20 small files (20 separate INSERT snapshots)
DO $$
BEGIN
    FOR i IN 1..20 LOOP
        EXECUTE format(
            'INSERT INTO vac_timing SELECT g, g*1.1 FROM generate_series(%s, %s) AS g',
            (i-1)*50 + 1, i*50);
    END LOOP;
END $$;

-- Measure a scan over the many-files state (before compaction)
SELECT perf_run_iterations(
    'iceberg_vacuum', 'scan_before_compact',
    'SELECT COUNT(*) FROM vac_timing',
    :iterations, :warmup, 1000, 'Scan 1000 rows across ~20 files');

-- Time the VACUUM itself (single-shot; VACUUM can't iterate in PL/pgSQL)
-- start_ignore
SELECT perf_start_timer('iceberg_vacuum', 'compact');
VACUUM vac_timing;
SELECT perf_end_timer('iceberg_vacuum', 'compact', 1000, 'VACUUM compaction');
-- end_ignore

-- Post-compaction scan - should be faster or equivalent
SELECT perf_run_iterations(
    'iceberg_vacuum', 'scan_after_compact',
    'SELECT COUNT(*) FROM vac_timing',
    :iterations, :warmup, 1000, 'Scan 1000 rows after compaction');

-- Cleanup
DROP TABLE vac_timing;
DROP VOLUME vac_vol;
DROP USER MAPPING FOR current_user SERVER vac_vol_srv;
DROP SERVER vac_vol_srv;
DROP CATALOG vac_cat;
DROP USER MAPPING FOR current_user SERVER vac_cat_srv;
DROP SERVER vac_cat_srv;
