-- version_compat.sql — Template. Tests reading Iceberg tables written by
-- different Spark/Iceberg runtime versions (spec v1 vs v2).
\i ../../../lib/sql/common_setup.sql
SELECT test_log('Iceberg version compat: SKIPPED (spark-sql not available)');
