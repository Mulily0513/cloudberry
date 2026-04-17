-- abort_during_insert.sql
-- Verify Iceberg tables tolerate BEGIN + INSERT + ROLLBACK without state drift.
--
-- What we check:
--   1. Row count in the table is identical before and after the aborted txn.
--   2. Catalog entry count (pg_class rows for the table) is stable.
--   3. Running a second, committed INSERT after the abort still works.

\i ../../../lib/sql/common_setup.sql
\i ../../../lib/sql/performance_helpers.sql

-- Catalog + volume setup (ignore errors if already exists from a prior run)
DROP SERVER IF EXISTS rec_cat_srv CASCADE;
CREATE SERVER rec_cat_srv FOREIGN DATA WRAPPER iceberg_catalog_fdw;
CREATE USER MAPPING FOR current_user SERVER rec_cat_srv;
CREATE FOREIGN CATALOG rec_cat SERVER rec_cat_srv;
SET iceberg_default_catalog = 'rec_cat';

DROP SERVER IF EXISTS rec_vol_srv CASCADE;
CREATE SERVER rec_vol_srv FOREIGN DATA WRAPPER iceberg_volume_fdw
OPTIONS (type 's3', endpoint 'http://lakehouse:9100', region 'us-east-1',
         bucket_name 'warehouse', path_style_access 'true');
CREATE USER MAPPING FOR current_user SERVER rec_vol_srv
OPTIONS (access_key_id 'admin', secret_access_key 'password');
CREATE FOREIGN VOLUME rec_vol SERVER rec_vol_srv OPTIONS (base_path '/rec_test/');
SET iceberg_default_volume = 'rec_vol';

-- Table + baseline data
CREATE ICEBERG TABLE recovery_insert (id int, val numeric(12,2));
INSERT INTO recovery_insert SELECT i, i * 1.5 FROM generate_series(1, 100) AS g(i);

-- Snapshot baseline counts
CREATE TEMP TABLE _rec_baseline AS
SELECT
    (SELECT count(*) FROM recovery_insert)            AS table_rows,
    (SELECT count(*) FROM pg_class
     WHERE relname IN ('recovery_insert'))            AS pg_class_rows;

-- Aborted INSERT
BEGIN;
INSERT INTO recovery_insert
SELECT i, i * 2.0 FROM generate_series(101, 500) AS g(i);
-- verify mid-txn we see new rows
SELECT count(*) = 500 AS mid_txn_visible FROM recovery_insert;
ROLLBACK;

-- Verify no drift
SELECT
    (SELECT count(*) FROM recovery_insert) AS current_table_rows,
    _rec_baseline.table_rows                AS baseline_table_rows,
    (SELECT count(*) FROM recovery_insert) = _rec_baseline.table_rows
                                            AS rows_unchanged,
    (SELECT count(*) FROM pg_class WHERE relname IN ('recovery_insert'))
      = _rec_baseline.pg_class_rows         AS catalog_unchanged
FROM _rec_baseline;

-- After rollback a subsequent committed INSERT still works
INSERT INTO recovery_insert
SELECT i, i * 1.1 FROM generate_series(101, 150) AS g(i);
SELECT count(*) AS post_recovery_rows FROM recovery_insert;

-- Cleanup
DROP TABLE recovery_insert;
DROP VOLUME rec_vol;
DROP USER MAPPING FOR current_user SERVER rec_vol_srv;
DROP SERVER rec_vol_srv;
DROP CATALOG rec_cat;
DROP USER MAPPING FOR current_user SERVER rec_cat_srv;
DROP SERVER rec_cat_srv;
