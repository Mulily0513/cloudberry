-- Hive Edge Case Coverage Test
-- Purpose: Cover remaining small-gap files
-- Target: textFileSimpleRead.cpp (+22), lineRecordReader.cpp (+145),
--         textFileReadPolicy.cpp (+27), textFileRead.cpp (+37),
--         textFileArchiveRead.cpp (+13), archiveRead.cpp (+8),
--         readPolicy.cpp (+30), providerWrapper.cpp (+53),
--         provider.cpp (+24), common.cpp (+27)

SET client_min_messages = ERROR;
DROP SCHEMA IF EXISTS edge_cov CASCADE;
DROP FOREIGN DATA WRAPPER IF EXISTS datalake_fdw CASCADE;
CREATE EXTENSION IF NOT EXISTS datalake_fdw;
CREATE EXTENSION IF NOT EXISTS hive_connector;
RESET client_min_messages;

CREATE FOREIGN DATA WRAPPER datalake_fdw
    HANDLER datalake_fdw_handler
    VALIDATOR datalake_fdw_validator
    OPTIONS (mpp_execute 'all segments');
SELECT public.create_foreign_server('hive_server', 'gpadmin', 'datalake_fdw', 'paa_cluster');

CREATE SCHEMA edge_cov;
SET datestyle = ISO, MDY;

-- ============================================================
-- Test 1: textFileSimpleRead via new text path + uncompressed text
-- ============================================================
SET datalake.external_table_new_text = on;

SELECT public.sync_hive_table('hive_cluster','default','test_text','paa_cluster', 'edge_cov.test_text', 'hive_server');
SELECT COUNT(*) FROM edge_cov.test_text;
SELECT * FROM edge_cov.test_text ORDER BY id;

-- Large uncompressed text (exercises buffer handling in textFileSimpleRead)
SELECT public.sync_hive_table('hive_cluster','default','test_text_large','paa_cluster', 'edge_cov.test_large', 'hive_server');
SELECT COUNT(*) FROM edge_cov.test_large;
SELECT id, length(data) FROM edge_cov.test_large WHERE id <= 5 ORDER BY id;
SELECT COUNT(*) FROM edge_cov.test_large WHERE id > 400;

-- Pipe delimiter (exercises lineRecordReader delimiter logic)
SELECT public.sync_hive_table('hive_cluster','default','test_text_pipe','paa_cluster', 'edge_cov.test_pipe', 'hive_server');
SELECT COUNT(*) FROM edge_cov.test_pipe;
SELECT * FROM edge_cov.test_pipe WHERE id <= 5 ORDER BY id;

-- Space/whitespace (exercises lineRecordReader edge case)
SELECT public.sync_hive_table('hive_cluster','default','test_space','paa_cluster', 'edge_cov.test_space', 'hive_server');
SELECT * FROM edge_cov.test_space ORDER BY id;

-- Re-read compressed with new_text ON (snappy/deflate already tested, verify path selection)
SELECT public.sync_hive_table('hive_cluster','default','test_text_snappy','paa_cluster', 'edge_cov.test_snappy2', 'hive_server');
SELECT COUNT(*) FROM edge_cov.test_snappy2;

SELECT public.sync_hive_table('hive_cluster','default','test_text_deflate','paa_cluster', 'edge_cov.test_deflate2', 'hive_server');
SELECT COUNT(*) FROM edge_cov.test_deflate2;

-- ============================================================
-- Test 2: old text path (archiveRead) with various text tables
-- ============================================================
SET datalake.external_table_new_text = off;

SELECT public.sync_hive_table('hive_cluster','default','test_text','paa_cluster', 'edge_cov.test_text_old', 'hive_server');
SELECT COUNT(*) FROM edge_cov.test_text_old;
SELECT * FROM edge_cov.test_text_old ORDER BY id;

SELECT public.sync_hive_table('hive_cluster','default','test_text_large','paa_cluster', 'edge_cov.test_large_old', 'hive_server');
SELECT COUNT(*) FROM edge_cov.test_large_old;
SELECT id, length(data) FROM edge_cov.test_large_old WHERE id <= 3 ORDER BY id;

SELECT public.sync_hive_table('hive_cluster','default','test_text_pipe','paa_cluster', 'edge_cov.test_pipe_old', 'hive_server');
SELECT COUNT(*) FROM edge_cov.test_pipe_old;

RESET datalake.external_table_new_text;

-- ============================================================
-- Test 3: ORC with various read patterns (orcFileReader, orcReadInterface)
-- ============================================================
SELECT public.sync_hive_table('hive_cluster','default','test_orc','paa_cluster', 'edge_cov.test_orc', 'hive_server');
SELECT COUNT(*) FROM edge_cov.test_orc;
SELECT * FROM edge_cov.test_orc ORDER BY id NULLS LAST, m;
-- Column projection
SELECT id FROM edge_cov.test_orc WHERE m = 1;
SELECT name FROM edge_cov.test_orc;

-- ORC with extreme values
SELECT public.sync_hive_table('hive_cluster','default','test_extreme','paa_cluster', 'edge_cov.test_extreme', 'hive_server');
SELECT * FROM edge_cov.test_extreme;

-- ORC with decimal
SELECT public.sync_hive_table('hive_cluster','default','test_decimal','paa_cluster', 'edge_cov.test_decimal', 'hive_server');
SELECT * FROM edge_cov.test_decimal ORDER BY id;

-- ORC with datetime
SELECT public.sync_hive_table('hive_cluster','default','test_datetime','paa_cluster', 'edge_cov.test_datetime', 'hive_server');
SELECT * FROM edge_cov.test_datetime;

-- ORC with zero
SELECT public.sync_hive_table('hive_cluster','default','test_zero','paa_cluster', 'edge_cov.test_zero', 'hive_server');
SELECT * FROM edge_cov.test_zero ORDER BY id;

-- ============================================================
-- Test 4: Parquet with various patterns
-- ============================================================
SELECT public.sync_hive_table('hive_cluster','default','test_parquet','paa_cluster', 'edge_cov.test_parquet', 'hive_server');
SELECT COUNT(*) FROM edge_cov.test_parquet;
SELECT * FROM edge_cov.test_parquet;

-- Parquet with empty string
SELECT public.sync_hive_table('hive_cluster','default','test_empty_str','paa_cluster', 'edge_cov.test_empty_str', 'hive_server');
SELECT id, empty_val IS NULL AS e_null, null_val IS NULL AS n_null FROM edge_cov.test_empty_str ORDER BY id;

-- Parquet with unicode
SELECT public.sync_hive_table('hive_cluster','default','test_unicode','paa_cluster', 'edge_cov.test_unicode', 'hive_server');
SELECT * FROM edge_cov.test_unicode ORDER BY id;

-- Parquet with scientific
SELECT public.sync_hive_table('hive_cluster','default','test_scientific','paa_cluster', 'edge_cov.test_scientific', 'hive_server');
SELECT * FROM edge_cov.test_scientific;

-- ============================================================
-- Test 5: Multi-partition scanning (readPolicy, partition_selector)
-- ============================================================
SELECT public.sync_hive_table('hive_cluster','default','test_multi_partition','paa_cluster', 'edge_cov.test_multi', 'hive_server');
SELECT COUNT(*) FROM edge_cov.test_multi;
SELECT * FROM edge_cov.test_multi ORDER BY year, month, day;
SELECT * FROM edge_cov.test_multi WHERE year = 2024 AND month = 1 ORDER BY day;
SELECT * FROM edge_cov.test_multi WHERE day > 1 ORDER BY year, month, day;
SELECT COUNT(*) FROM edge_cov.test_multi WHERE month = 12;

-- ============================================================
-- Test 6: Empty table (edge case in provider/readPolicy)
-- ============================================================
SELECT public.sync_hive_table('hive_cluster','default','test_empty','paa_cluster', 'edge_cov.test_empty', 'hive_server');
SELECT COUNT(*) FROM edge_cov.test_empty;
SELECT * FROM edge_cov.test_empty;

-- ============================================================
-- Cleanup
-- ============================================================
DROP SCHEMA edge_cov CASCADE;
