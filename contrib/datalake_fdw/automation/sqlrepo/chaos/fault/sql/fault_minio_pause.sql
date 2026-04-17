-- fault_minio_pause.sql
-- Pause the lakehouse container (MinIO + Hive) for 5 seconds, then verify
-- the database session recovers and can still query Iceberg tables.
--
-- The \! command invokes docker_pause.sh in the BACKGROUND so the SQL session
-- can attempt a query during the outage. The next query should either:
-- (a) block until MinIO recovers (ideal), or
-- (b) error out cleanly (acceptable), but NOT hang forever.

\i ../../../lib/sql/common_setup.sql

DROP SERVER IF EXISTS ft_cat_srv CASCADE;
CREATE SERVER ft_cat_srv FOREIGN DATA WRAPPER iceberg_catalog_fdw;
CREATE USER MAPPING FOR current_user SERVER ft_cat_srv;
CREATE FOREIGN CATALOG ft_cat SERVER ft_cat_srv;
SET iceberg_default_catalog = 'ft_cat';

DROP SERVER IF EXISTS ft_vol_srv CASCADE;
CREATE SERVER ft_vol_srv FOREIGN DATA WRAPPER iceberg_volume_fdw
OPTIONS (type 's3', endpoint 'http://lakehouse:9100', region 'us-east-1',
         bucket_name 'warehouse', path_style_access 'true');
CREATE USER MAPPING FOR current_user SERVER ft_vol_srv
OPTIONS (access_key_id 'admin', secret_access_key 'password');
CREATE FOREIGN VOLUME ft_vol SERVER ft_vol_srv OPTIONS (base_path '/ft_test/');
SET iceberg_default_volume = 'ft_vol';

CREATE ICEBERG TABLE fault_tbl (id int, val numeric(10,2));
INSERT INTO fault_tbl SELECT g, g*1.5 FROM generate_series(1, 50) AS g;
SELECT count(*) = 50 AS pre_fault_ok FROM fault_tbl;

-- Pause lakehouse for 3 seconds (background). The pause script has its own
-- cleanup trap. WARNING: This pauses MinIO + Hive + HDFS together.
\! bash ../../../../lib/scripts/chaos/docker_pause.sh lakehouse 3 &

-- Allow 1 second for the pause to take effect
\! sleep 1

-- This query runs while lakehouse is paused. Expected: either blocks and
-- completes after unpause, or errors cleanly.
SET statement_timeout = '15s';
SELECT count(*) AS during_fault FROM fault_tbl;
RESET statement_timeout;

-- After unpause: verify table is still usable
SELECT count(*) = 50 AS post_fault_ok FROM fault_tbl;

-- New write after fault should also work
INSERT INTO fault_tbl SELECT g, g*2.0 FROM generate_series(51, 60) AS g;
SELECT count(*) = 60 AS post_fault_write_ok FROM fault_tbl;

-- Cleanup
DROP TABLE fault_tbl;
DROP VOLUME ft_vol;
DROP USER MAPPING FOR current_user SERVER ft_vol_srv;
DROP SERVER ft_vol_srv;
DROP CATALOG ft_cat;
DROP USER MAPPING FOR current_user SERVER ft_cat_srv;
DROP SERVER ft_cat_srv;
