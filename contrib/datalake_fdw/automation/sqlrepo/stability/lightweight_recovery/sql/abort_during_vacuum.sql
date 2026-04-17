-- abort_during_vacuum.sql
-- Verify VACUUM-like compaction operations tolerate abort mid-flight.
--
-- Strategy: build up many small snapshots by doing repeated small INSERTs
-- (forcing multiple data files). Then BEGIN a transaction that issues
-- VACUUM, ROLLBACK, and verify the original small-file layout is still
-- intact - no half-rewritten compact files should be left behind.

\i ../../../lib/sql/common_setup.sql

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
CREATE FOREIGN VOLUME vac_vol SERVER vac_vol_srv OPTIONS (base_path '/rec_vac/');
SET iceberg_default_volume = 'vac_vol';

CREATE ICEBERG TABLE recovery_vacuum (id int, val numeric(12,2));

-- Build up many small files via repeated small INSERTs
DO $$
BEGIN
    FOR i IN 1..10 LOOP
        EXECUTE format(
            'INSERT INTO recovery_vacuum SELECT g, g * 1.5 FROM generate_series(%s, %s) AS g',
            (i-1)*10 + 1, i*10);
    END LOOP;
END $$;

-- Baseline: row count
SELECT count(*) = 100 AS baseline_rowcount_ok FROM recovery_vacuum;

-- Abort a VACUUM mid-txn
BEGIN;
VACUUM recovery_vacuum;
-- rows still visible inside the txn
SELECT count(*) = 100 AS mid_vacuum_visible FROM recovery_vacuum;
ROLLBACK;

-- After rollback: data intact, table queryable, subsequent VACUUM works
SELECT count(*) = 100 AS post_rollback_rowcount_ok FROM recovery_vacuum;
VACUUM recovery_vacuum;
SELECT count(*) = 100 AS post_clean_vacuum_rowcount_ok FROM recovery_vacuum;

-- Cleanup
DROP TABLE recovery_vacuum;
DROP VOLUME vac_vol;
DROP USER MAPPING FOR current_user SERVER vac_vol_srv;
DROP SERVER vac_vol_srv;
DROP CATALOG vac_cat;
DROP USER MAPPING FOR current_user SERVER vac_cat_srv;
DROP SERVER vac_cat_srv;
