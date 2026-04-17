-- add_column.sql
-- ALTER TABLE ADD COLUMN on a populated Iceberg table. Verify:
--   1. Existing rows expose NULL (or the DEFAULT) for the new column.
--   2. New rows can populate the new column normally.
--   3. Snapshot ids advance but table stays queryable throughout.

\i ../../../lib/sql/common_setup.sql

DROP SERVER IF EXISTS ev_cat_srv CASCADE;
CREATE SERVER ev_cat_srv FOREIGN DATA WRAPPER iceberg_catalog_fdw;
CREATE USER MAPPING FOR current_user SERVER ev_cat_srv;
CREATE FOREIGN CATALOG ev_cat SERVER ev_cat_srv;
SET iceberg_default_catalog = 'ev_cat';

DROP SERVER IF EXISTS ev_vol_srv CASCADE;
CREATE SERVER ev_vol_srv FOREIGN DATA WRAPPER iceberg_volume_fdw
OPTIONS (type 's3', endpoint 'http://lakehouse:9100', region 'us-east-1',
         bucket_name 'warehouse', path_style_access 'true');
CREATE USER MAPPING FOR current_user SERVER ev_vol_srv
OPTIONS (access_key_id 'admin', secret_access_key 'password');
CREATE FOREIGN VOLUME ev_vol SERVER ev_vol_srv OPTIONS (base_path '/ev_add/');
SET iceberg_default_volume = 'ev_vol';

CREATE ICEBERG TABLE evo_add (id int, val numeric(10,2));
INSERT INTO evo_add SELECT g, g * 1.5 FROM generate_series(1, 20) AS g;

-- ADD COLUMN (nullable, no default)
ALTER TABLE evo_add ADD COLUMN note text;

-- Existing rows should see NULL for the new column
SELECT count(*) AS rows_with_null_note FROM evo_add WHERE note IS NULL;

-- New inserts can populate all three columns
INSERT INTO evo_add VALUES (21, 21.5, 'new row 21'), (22, 22.5, 'new row 22');
SELECT count(*) AS total, count(note) AS non_null_notes FROM evo_add;

-- Query subset with the new column
SELECT id, note FROM evo_add WHERE id >= 20 ORDER BY id;

-- Cleanup
DROP TABLE evo_add;
DROP VOLUME ev_vol;
DROP USER MAPPING FOR current_user SERVER ev_vol_srv;
DROP SERVER ev_vol_srv;
DROP CATALOG ev_cat;
DROP USER MAPPING FOR current_user SERVER ev_cat_srv;
DROP SERVER ev_cat_srv;
