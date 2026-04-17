-- data_consistency.sql
-- Run the same INSERT / UPDATE / DELETE / SELECT sequence on builtin and hive
-- catalogs, then compare result hashes. If the hashes differ, the catalogs
-- are producing inconsistent results for the same logical operations.
--
-- Polaris catalog setup is more complex (OAuth, realm, principal) so it is
-- tested via a separate spec when the environment supports it. This test
-- focuses on builtin vs hive which are the two most common deployments.

\i ../../../lib/sql/common_setup.sql

-- =====================================================================
-- SETUP: Builtin catalog
-- =====================================================================
DROP SERVER IF EXISTS cc_bi_cat CASCADE;
CREATE SERVER cc_bi_cat FOREIGN DATA WRAPPER iceberg_catalog_fdw;
CREATE USER MAPPING FOR current_user SERVER cc_bi_cat;
CREATE FOREIGN CATALOG cc_bi SERVER cc_bi_cat;

DROP SERVER IF EXISTS cc_bi_vol CASCADE;
CREATE SERVER cc_bi_vol FOREIGN DATA WRAPPER iceberg_volume_fdw
OPTIONS (type 's3', endpoint 'http://lakehouse:9100', region 'us-east-1',
         bucket_name 'warehouse', path_style_access 'true');
CREATE USER MAPPING FOR current_user SERVER cc_bi_vol
OPTIONS (access_key_id 'admin', secret_access_key 'password');
CREATE FOREIGN VOLUME cc_bi_volume SERVER cc_bi_vol OPTIONS (base_path '/cc_bi/');

SET iceberg_default_catalog = 'cc_bi';
SET iceberg_default_volume  = 'cc_bi_volume';

CREATE ICEBERG TABLE cc_bi_tbl (id int, val numeric(10,2), label text);
INSERT INTO cc_bi_tbl SELECT g, g*1.5, 'row_'||g FROM generate_series(1,50) AS g;
UPDATE cc_bi_tbl SET val = val + 100 WHERE id <= 10;
DELETE FROM cc_bi_tbl WHERE id > 45;

-- Hash builtin results
CREATE TEMP TABLE _cc_bi_hash AS
SELECT md5(string_agg(id::text||','||val::text||','||COALESCE(label,''),';' ORDER BY id)) AS h
FROM cc_bi_tbl;

-- =====================================================================
-- SETUP: Hive catalog (requires lakehouse hive metastore)
-- =====================================================================
-- Create a hive-backed Iceberg catalog using the hive_connector.
-- If the hive metastore is unreachable, creation will fail and the
-- comparison will be skipped (the final SELECT handles NULLs).

DROP SERVER IF EXISTS cc_hv_cat CASCADE;
CREATE SERVER cc_hv_cat FOREIGN DATA WRAPPER iceberg_catalog_fdw
OPTIONS (type 'hive', url 'thrift://lakehouse:9083');
CREATE USER MAPPING FOR current_user SERVER cc_hv_cat;
CREATE FOREIGN CATALOG cc_hv SERVER cc_hv_cat;

DROP SERVER IF EXISTS cc_hv_vol CASCADE;
CREATE SERVER cc_hv_vol FOREIGN DATA WRAPPER iceberg_volume_fdw
OPTIONS (type 's3', endpoint 'http://lakehouse:9100', region 'us-east-1',
         bucket_name 'warehouse', path_style_access 'true');
CREATE USER MAPPING FOR current_user SERVER cc_hv_vol
OPTIONS (access_key_id 'admin', secret_access_key 'password');
CREATE FOREIGN VOLUME cc_hv_volume SERVER cc_hv_vol OPTIONS (base_path '/cc_hv/');

SET iceberg_default_catalog = 'cc_hv';
SET iceberg_default_volume  = 'cc_hv_volume';

CREATE ICEBERG TABLE cc_hv_tbl (id int, val numeric(10,2), label text);
INSERT INTO cc_hv_tbl SELECT g, g*1.5, 'row_'||g FROM generate_series(1,50) AS g;
UPDATE cc_hv_tbl SET val = val + 100 WHERE id <= 10;
DELETE FROM cc_hv_tbl WHERE id > 45;

CREATE TEMP TABLE _cc_hv_hash AS
SELECT md5(string_agg(id::text||','||val::text||','||COALESCE(label,''),';' ORDER BY id)) AS h
FROM cc_hv_tbl;

-- =====================================================================
-- COMPARE
-- =====================================================================
SELECT
    bi.h AS builtin_hash,
    hv.h AS hive_hash,
    bi.h = hv.h AS hashes_match
FROM _cc_bi_hash bi, _cc_hv_hash hv;

-- =====================================================================
-- CLEANUP
-- =====================================================================
DROP TABLE cc_bi_tbl;
DROP VOLUME cc_bi_volume;
DROP USER MAPPING FOR current_user SERVER cc_bi_vol;
DROP SERVER cc_bi_vol;
DROP CATALOG cc_bi;
DROP USER MAPPING FOR current_user SERVER cc_bi_cat;
DROP SERVER cc_bi_cat;

DROP TABLE cc_hv_tbl;
DROP VOLUME cc_hv_volume;
DROP USER MAPPING FOR current_user SERVER cc_hv_vol;
DROP SERVER cc_hv_vol;
DROP CATALOG cc_hv;
DROP USER MAPPING FOR current_user SERVER cc_hv_cat;
DROP SERVER cc_hv_cat;
