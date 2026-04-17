-- hd_write_spark_read.sql — Template. HashData writes Iceberg table,
-- then scripts/verify_round_trip.sh uses spark-sql to read back and compare.
\i ../../../lib/sql/common_setup.sql
SELECT test_log('HashData->Spark compat: SKIPPED (spark-sql not available)');
