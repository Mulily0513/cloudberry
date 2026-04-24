-- 01_catalog_volume.sql
-- Bootstrap the iceberg builtin catalog and the S3 volume backed by the
-- singlecluster MinIO (lakehouse:9100, bucket "warehouse").

DROP EXTENSION IF EXISTS datalake_fdw CASCADE;
CREATE EXTENSION datalake_fdw;

DROP SERVER IF EXISTS am_scale_tpch_cat_srv CASCADE;
CREATE SERVER am_scale_tpch_cat_srv FOREIGN DATA WRAPPER iceberg_catalog_fdw;
CREATE USER MAPPING FOR current_user SERVER am_scale_tpch_cat_srv;
CREATE FOREIGN CATALOG am_scale_tpch_cat SERVER am_scale_tpch_cat_srv
    OPTIONS (default_namespace 'public');
SET iceberg_default_catalog = 'am_scale_tpch_cat';

DROP SERVER IF EXISTS am_scale_tpch_vol_srv CASCADE;
CREATE SERVER am_scale_tpch_vol_srv FOREIGN DATA WRAPPER iceberg_volume_fdw
    OPTIONS (
        type 's3',
        endpoint 'http://lakehouse:9100',
        region 'us-east-1',
        bucket_name 'warehouse',
        path_style_access 'true'
    );
CREATE USER MAPPING FOR current_user SERVER am_scale_tpch_vol_srv
    OPTIONS (access_key_id 'admin', secret_access_key 'password');
CREATE FOREIGN VOLUME am_scale_tpch_vol SERVER am_scale_tpch_vol_srv
    OPTIONS (base_path '/iceberg_am_scale_tpch/', allow_writes 'true');
SET iceberg_default_volume = 'am_scale_tpch_vol';

-- Persist as database-level defaults so subsequent psql connections (load,
-- query) inherit them without re-SET'ing.
ALTER DATABASE :"dbname" SET iceberg_default_catalog = 'am_scale_tpch_cat';
ALTER DATABASE :"dbname" SET iceberg_default_volume  = 'am_scale_tpch_vol';
