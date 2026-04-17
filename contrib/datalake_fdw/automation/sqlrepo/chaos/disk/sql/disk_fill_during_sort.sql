-- disk_fill_during_sort.sql
-- Fill /tmp to near capacity during a sort-heavy query, verify the query
-- either completes or fails cleanly (no corruption). Then free space and
-- verify normal operation resumes.

\i ../../../lib/sql/common_setup.sql

DROP SERVER IF EXISTS df_cat_srv CASCADE;
CREATE SERVER df_cat_srv FOREIGN DATA WRAPPER iceberg_catalog_fdw;
CREATE USER MAPPING FOR current_user SERVER df_cat_srv;
CREATE FOREIGN CATALOG df_cat SERVER df_cat_srv;
SET iceberg_default_catalog = 'df_cat';

DROP SERVER IF EXISTS df_vol_srv CASCADE;
CREATE SERVER df_vol_srv FOREIGN DATA WRAPPER iceberg_volume_fdw
OPTIONS (type 's3', endpoint 'http://lakehouse:9100', region 'us-east-1',
         bucket_name 'warehouse', path_style_access 'true');
CREATE USER MAPPING FOR current_user SERVER df_vol_srv
OPTIONS (access_key_id 'admin', secret_access_key 'password');
CREATE FOREIGN VOLUME df_vol SERVER df_vol_srv OPTIONS (base_path '/df_test/');
SET iceberg_default_volume = 'df_vol';

CREATE ICEBERG TABLE diskfill_tbl (id int, val numeric(10,2), data text);
INSERT INTO diskfill_tbl
SELECT g, g*1.5, repeat('x', 100)
FROM generate_series(1, 1000) AS g;

-- Fill /tmp with a 500MB placeholder (background, hold 10 seconds)
\! bash ../../../../lib/scripts/chaos/fill_disk.sh /tmp/chaos_fill_disk 500 10 &

-- Wait for fill to take effect
\! sleep 2

-- Run a sort-heavy query. It may or may not spill to /tmp.
-- If it fails, the error should be clean (not corruption).
SET statement_timeout = '30s';
SELECT count(*) AS sort_under_pressure FROM (
    SELECT * FROM diskfill_tbl ORDER BY data, val DESC
) sub;
RESET statement_timeout;

-- Wait for fill_disk to clean up
\! sleep 10

-- After /tmp freed: normal query should work
SELECT count(*) = 1000 AS post_recovery_ok FROM diskfill_tbl;

-- Cleanup
DROP TABLE diskfill_tbl;
DROP VOLUME df_vol;
DROP USER MAPPING FOR current_user SERVER df_vol_srv;
DROP SERVER df_vol_srv;
DROP CATALOG df_cat;
DROP USER MAPPING FOR current_user SERVER df_cat_srv;
DROP SERVER df_cat_srv;
