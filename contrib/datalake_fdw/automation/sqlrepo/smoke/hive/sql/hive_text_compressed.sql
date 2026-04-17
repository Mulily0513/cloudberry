-- Hive Compressed Text Coverage Test
-- Purpose: Exercise textFile compression read paths
-- Target: textFileSnappyRead.cpp (+126), textFileDeflateRead.cpp (+94),
--         lineRecordReader.cpp (+145), textFileRead.cpp (+47),
--         textFileReadPolicy.cpp (+31), textFileSimpleRead.cpp,
--         archiveRead.cpp, readPolicy.cpp

SET client_min_messages = ERROR;
DROP SCHEMA IF EXISTS text_cov CASCADE;
DROP FOREIGN DATA WRAPPER IF EXISTS datalake_fdw CASCADE;

CREATE EXTENSION IF NOT EXISTS datalake_fdw;
CREATE EXTENSION IF NOT EXISTS hive_connector;
RESET client_min_messages;
CREATE FOREIGN DATA WRAPPER datalake_fdw
    HANDLER datalake_fdw_handler
    VALIDATOR datalake_fdw_validator
    OPTIONS (mpp_execute 'all segments');
SELECT public.create_foreign_server('hive_server', 'gpadmin', 'datalake_fdw', 'paa_cluster');

DROP SCHEMA IF EXISTS text_cov CASCADE;
CREATE SCHEMA text_cov;
SET datestyle = ISO, MDY;

-- Enable new text reader that supports snappy/deflate decompression
SET datalake.external_table_new_text = on;

-- ============================================================
-- Test 1: Snappy compressed text (textFileSnappyRead.cpp)
-- ============================================================
SELECT public.sync_hive_table('hive_cluster','default','test_text_snappy','paa_cluster', 'text_cov.test_text_snappy', 'hive_server');

SELECT COUNT(*) FROM text_cov.test_text_snappy;
SELECT * FROM text_cov.test_text_snappy WHERE id <= 5 ORDER BY id;
SELECT * FROM text_cov.test_text_snappy WHERE id > 45 ORDER BY id;
SELECT COUNT(*) FROM text_cov.test_text_snappy WHERE val > 50.0;

-- ============================================================
-- Test 2: Deflate compressed text (textFileDeflateRead.cpp)
-- ============================================================
SELECT public.sync_hive_table('hive_cluster','default','test_text_deflate','paa_cluster', 'text_cov.test_text_deflate', 'hive_server');

SELECT COUNT(*) FROM text_cov.test_text_deflate;
SELECT * FROM text_cov.test_text_deflate WHERE id <= 5 ORDER BY id;
SELECT * FROM text_cov.test_text_deflate WHERE id > 45 ORDER BY id;
SELECT COUNT(*) FROM text_cov.test_text_deflate WHERE val > 80.0;

-- ============================================================
-- Test 3: GZip compressed text (also deflate path internally)
-- ============================================================
SELECT public.sync_hive_table('hive_cluster','default','test_text_gzip','paa_cluster', 'text_cov.test_text_gzip', 'hive_server');

SELECT COUNT(*) FROM text_cov.test_text_gzip;
SELECT * FROM text_cov.test_text_gzip WHERE id <= 5 ORDER BY id;
SELECT COUNT(*) FROM text_cov.test_text_gzip WHERE val > 100.0;

-- ============================================================
-- Test 4: Pipe-delimited text (lineRecordReader custom delimiter)
-- ============================================================
SELECT public.sync_hive_table('hive_cluster','default','test_text_pipe','paa_cluster', 'text_cov.test_text_pipe', 'hive_server');

SELECT COUNT(*) FROM text_cov.test_text_pipe;
SELECT * FROM text_cov.test_text_pipe WHERE id <= 5 ORDER BY id;
SELECT * FROM text_cov.test_text_pipe WHERE name = 'pipe_row_10';

-- ============================================================
-- Test 5: Large text table (lineRecordReader buffer handling)
-- ============================================================
SELECT public.sync_hive_table('hive_cluster','default','test_text_large','paa_cluster', 'text_cov.test_text_large', 'hive_server');

SELECT COUNT(*) FROM text_cov.test_text_large;
SELECT id, length(data) FROM text_cov.test_text_large WHERE id <= 3 ORDER BY id;
SELECT COUNT(*) FROM text_cov.test_text_large WHERE id > 400;

-- ============================================================
-- Test 6: Multiple sequential reads (readPolicy distribution)
-- ============================================================
SELECT COUNT(*) FROM text_cov.test_text_snappy;
SELECT COUNT(*) FROM text_cov.test_text_deflate;
SELECT COUNT(*) FROM text_cov.test_text_gzip;
SELECT COUNT(*) FROM text_cov.test_text_pipe;
SELECT COUNT(*) FROM text_cov.test_text_large;

-- ============================================================
-- Test 7: Also test with old text reader (archiveRead path)
-- ============================================================
SET datalake.external_table_new_text = off;

SELECT COUNT(*) FROM text_cov.test_text_pipe;
SELECT COUNT(*) FROM text_cov.test_text_large;

RESET datalake.external_table_new_text;

-- ============================================================
-- Cleanup
-- ============================================================
DROP SCHEMA text_cov CASCADE;
