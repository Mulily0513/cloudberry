-- mixed_schema_files.sql
-- The critical schema-evolution invariant: after ALTER, a single SELECT
-- correctly reads files written BEFORE and AFTER the schema change.
-- If Iceberg field-id mapping is wrong, old files will show garbage or NULL
-- in the wrong column, or an outright parse error.

\i ../../../lib/sql/common_setup.sql

DROP SERVER IF EXISTS msf_cat_srv CASCADE;
CREATE SERVER msf_cat_srv FOREIGN DATA WRAPPER iceberg_catalog_fdw;
CREATE USER MAPPING FOR current_user SERVER msf_cat_srv;
CREATE FOREIGN CATALOG msf_cat SERVER msf_cat_srv;
SET iceberg_default_catalog = 'msf_cat';

DROP SERVER IF EXISTS msf_vol_srv CASCADE;
CREATE SERVER msf_vol_srv FOREIGN DATA WRAPPER iceberg_volume_fdw
OPTIONS (type 's3', endpoint 'http://lakehouse:9100', region 'us-east-1',
         bucket_name 'warehouse', path_style_access 'true');
CREATE USER MAPPING FOR current_user SERVER msf_vol_srv
OPTIONS (access_key_id 'admin', secret_access_key 'password');
CREATE FOREIGN VOLUME msf_vol SERVER msf_vol_srv OPTIONS (base_path '/ev_msf/');
SET iceberg_default_volume = 'msf_vol';

-- Schema v1: (id, val)
CREATE ICEBERG TABLE evo_mixed (id int, val numeric(10,2));

-- Write batch 1 using v1 schema
INSERT INTO evo_mixed SELECT g, g * 1.0 FROM generate_series(1, 10) AS g;

-- Verify v1 data is present
SELECT count(*) = 10 AS batch1_rows_ok FROM evo_mixed;

-- Evolve: add a new column
ALTER TABLE evo_mixed ADD COLUMN category text;

-- Write batch 2 using v2 schema (populating the new column)
INSERT INTO evo_mixed VALUES
    (11, 11.0, 'cat_A'),
    (12, 12.0, 'cat_B'),
    (13, 13.0, 'cat_A');

-- Critical test: single SELECT reads both batches correctly
SELECT count(*) AS total_rows,
       count(category) AS rows_with_category
FROM evo_mixed;

-- v1 rows should have NULL category, v2 rows should have the value
SELECT
    (SELECT count(*) FROM evo_mixed WHERE id <= 10  AND category IS NULL) AS v1_null_ok,
    (SELECT count(*) FROM evo_mixed WHERE id BETWEEN 11 AND 13 AND category IS NOT NULL) AS v2_set_ok;

-- Aggregation across both schemas should yield correct totals
SELECT SUM(val) AS total_val, SUM(id) AS total_id FROM evo_mixed;

-- Category filter: should only match v2 rows
SELECT id, category FROM evo_mixed WHERE category IS NOT NULL ORDER BY id;

-- Cleanup
DROP TABLE evo_mixed;
DROP VOLUME msf_vol;
DROP USER MAPPING FOR current_user SERVER msf_vol_srv;
DROP SERVER msf_vol_srv;
DROP CATALOG msf_cat;
DROP USER MAPPING FOR current_user SERVER msf_cat_srv;
DROP SERVER msf_cat_srv;
