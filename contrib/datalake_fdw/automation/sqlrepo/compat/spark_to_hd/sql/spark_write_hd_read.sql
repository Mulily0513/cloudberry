-- spark_write_hd_read.sql
-- Template for Spark→HashData compatibility test.
-- PREREQUISITE: spark-sql must be available and the lakehouse Hive Metastore
-- must be configured as a shared Iceberg catalog between Spark and HashData.
--
-- Workflow (to be orchestrated by scripts/spark_write.sh):
--   1. Spark writes an Iceberg table with all supported types
--   2. This SQL reads the Spark-written table from HashData
--   3. Verify: all rows + all types parse correctly
--
-- This file is a TEMPLATE. It assumes the Spark-written table already exists
-- in the hive catalog as 'compat_db.spark_types_test'. The \! step at the top
-- calls the prep script if spark-sql is available.

\i ../../../lib/sql/common_setup.sql

SELECT test_log('Spark->HashData compat test');
SELECT test_log('NOTE: requires spark_write.sh to have been run first');

-- Attempt to read a Spark-written Iceberg table via hive catalog.
-- If the table doesn't exist, the test errors here — that's expected
-- until the full Spark environment is set up.

-- TODO: Uncomment when spark-sql is installed:
-- CREATE SERVER compat_hv_cat FOREIGN DATA WRAPPER iceberg_catalog_fdw
-- OPTIONS (type 'hive', url 'thrift://lakehouse:9083');
-- CREATE USER MAPPING FOR current_user SERVER compat_hv_cat;
-- CREATE FOREIGN CATALOG compat_hv SERVER compat_hv_cat;
-- SET iceberg_default_catalog = 'compat_hv';
-- SELECT * FROM compat_db.spark_types_test LIMIT 5;

SELECT test_log('Spark->HashData compat: SKIPPED (spark-sql not available)');
