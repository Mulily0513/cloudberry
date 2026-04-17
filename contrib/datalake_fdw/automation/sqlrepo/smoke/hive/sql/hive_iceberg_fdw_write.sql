-- Hive Iceberg FDW Write Coverage Test
-- Purpose: Trigger dlproxy POST write path via Iceberg FDW
-- Target: dlproxy/protocol.c POST path (+132), dlproxy/libchurl.c upload (+48),
--         dlproxy/headers.c file_list headers (+77),
--         datalake_fdw.c EndForeignModify, fdwFunction.c insertModify/endModify

SET client_min_messages = ERROR;
DROP FOREIGN DATA WRAPPER IF EXISTS datalake_fdw CASCADE;
CREATE EXTENSION IF NOT EXISTS datalake_fdw;
CREATE EXTENSION IF NOT EXISTS hive_connector;
RESET client_min_messages;

CREATE FOREIGN DATA WRAPPER datalake_fdw
    HANDLER datalake_fdw_handler
    VALIDATOR datalake_fdw_validator
    OPTIONS (mpp_execute 'all segments');
SELECT public.create_foreign_server('hive_server', 'gpadmin', 'datalake_fdw', 'paa_cluster');
SET datestyle = ISO, MDY;

-- ============================================================
-- Test 1: INSERT into Iceberg FDW table (triggers POST write path)
-- ============================================================
DROP FOREIGN TABLE IF EXISTS iceberg_fdw_write_test;
CREATE FOREIGN TABLE iceberg_fdw_write_test (
    id bigint,
    name text,
    amount double precision,
    ts bigint
)
SERVER hive_server
OPTIONS (
    filePath 'default.iceberg_fdw_test',
    catalog_type 'hive',
    server_name 'hive_cluster',
    hdfs_cluster_name 'paa_cluster',
    table_identifier 'default.iceberg_fdw_test',
    format 'iceberg'
);

-- Read first to verify table exists
SELECT COUNT(*) FROM iceberg_fdw_write_test;

-- INSERT (triggers datalake_create_write_context_ -> build_uri_for_write
--         -> add_write_querydata_to_http_headers -> datalakeDoRPC POST)
INSERT INTO iceberg_fdw_write_test VALUES (100, 'fdw_insert_1', 999.99, 9000);
INSERT INTO iceberg_fdw_write_test VALUES (101, 'fdw_insert_2', 888.88, 9001),
                                          (102, 'fdw_insert_3', 777.77, 9002);

-- Verify data was written
SELECT COUNT(*) FROM iceberg_fdw_write_test;
SELECT * FROM iceberg_fdw_write_test WHERE id >= 100 ORDER BY id;

-- ============================================================
-- Test 2: Bulk INSERT (larger file list in POST)
-- ============================================================
INSERT INTO iceberg_fdw_write_test
SELECT i::bigint, 'bulk_' || i, (i * 1.1)::double precision, (i + 10000)::bigint
FROM generate_series(200, 250) i;

SELECT COUNT(*) FROM iceberg_fdw_write_test WHERE id >= 200;

-- ============================================================
-- Cleanup
-- ============================================================
DROP FOREIGN TABLE IF EXISTS iceberg_fdw_write_test;
