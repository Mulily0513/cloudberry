-- common_teardown.sql
-- Standard cleanup for datalake_fdw tests
-- This file should be included at the end of test SQL files that need cleanup
--
-- Drops:
-- 1. Test schemas (pattern matching: %_test, %_smoke, %_perf, %_feature)
-- 2. Common servers (if they exist)
-- 3. Utility functions and tables

-- Drop test schemas (be careful with pattern matching)
DO $$
DECLARE
    schema_rec RECORD;
BEGIN
    FOR schema_rec IN
        SELECT nspname
        FROM pg_namespace
        WHERE nspname LIKE '%_test'
           OR nspname LIKE '%_smoke'
           OR nspname LIKE '%_perf'
           OR nspname LIKE '%_feature'
    LOOP
        EXECUTE 'DROP SCHEMA IF EXISTS ' || quote_ident(schema_rec.nspname) || ' CASCADE';
        RAISE NOTICE '[CLEANUP] Dropped schema: %', schema_rec.nspname;
    END LOOP;
END $$;

-- Drop common servers (if they exist)
-- Note: CASCADE will also drop user mappings and dependent objects
DO $$
BEGIN
    -- Drop Hive server
    IF EXISTS (SELECT 1 FROM pg_foreign_server WHERE srvname = 'hive_server') THEN
        DROP SERVER hive_server CASCADE;
        RAISE NOTICE '[CLEANUP] Dropped server: hive_server';
    END IF;

    -- Drop MinIO/S3 server
    IF EXISTS (SELECT 1 FROM pg_foreign_server WHERE srvname = 'minio_server') THEN
        DROP SERVER minio_server CASCADE;
        RAISE NOTICE '[CLEANUP] Dropped server: minio_server';
    END IF;

    -- Drop HDFS server
    IF EXISTS (SELECT 1 FROM pg_foreign_server WHERE srvname = 'hdfs_server') THEN
        DROP SERVER hdfs_server CASCADE;
        RAISE NOTICE '[CLEANUP] Dropped server: hdfs_server';
    END IF;

    -- Drop Iceberg catalog server
    IF EXISTS (SELECT 1 FROM pg_foreign_server WHERE srvname = 'iceberg_catalog_server') THEN
        DROP SERVER iceberg_catalog_server CASCADE;
        RAISE NOTICE '[CLEANUP] Dropped server: iceberg_catalog_server';
    END IF;

    -- Drop Iceberg volume server
    IF EXISTS (SELECT 1 FROM pg_foreign_server WHERE srvname = 'iceberg_volume_server') THEN
        DROP SERVER iceberg_volume_server CASCADE;
        RAISE NOTICE '[CLEANUP] Dropped server: iceberg_volume_server';
    END IF;
END $$;

-- Drop performance tables (if they exist)
DROP TABLE IF EXISTS public.perf_results CASCADE;
DROP TABLE IF EXISTS public.perf_timer_context CASCADE;

-- Drop utility functions (if they exist)
DROP FUNCTION IF EXISTS test_log(text) CASCADE;
DROP FUNCTION IF EXISTS perf_start_timer(text, text) CASCADE;
DROP FUNCTION IF EXISTS perf_end_timer(text, text, bigint, text) CASCADE;
DROP FUNCTION IF EXISTS perf_report(text) CASCADE;
DROP FUNCTION IF EXISTS perf_clear(text) CASCADE;

-- Drop iceberg_toolkit schema if empty
DROP SCHEMA IF EXISTS iceberg_toolkit CASCADE;

-- Log cleanup completion
RAISE NOTICE '[CLEANUP] Common teardown completed';
