-- scale_nested_types.sql
-- Placeholder: Nested type (struct/array/map) support in Iceberg is not yet
-- mature. This test uses a regular (non-Iceberg) table with array and jsonb
-- columns as a stand-in until native nested type support lands.
--
-- When Iceberg nested types are available, this file should be rewritten to
-- use CREATE ICEBERG TABLE with the appropriate column types.

\i ../../../lib/sql/common_setup.sql
\i ../../../lib/sql/performance_helpers.sql

SELECT test_log('Scale Test: Nested types placeholder (array + jsonb)');

-- ============================================================
-- Setup: Catalog and volume (for future Iceberg nested types)
-- ============================================================
DROP SERVER IF EXISTS nt_catalog_server CASCADE;
CREATE SERVER nt_catalog_server FOREIGN DATA WRAPPER iceberg_catalog_fdw;
CREATE USER MAPPING FOR current_user SERVER nt_catalog_server;
CREATE FOREIGN CATALOG nt_catalog SERVER nt_catalog_server;
SET iceberg_default_catalog = 'nt_catalog';

CREATE SERVER nt_volume_server FOREIGN DATA WRAPPER iceberg_volume_fdw
    OPTIONS (type 's3', endpoint 'http://lakehouse:9100', region 'us-east-1',
             bucket_name 'warehouse', path_style_access 'true');
CREATE USER MAPPING FOR current_user SERVER nt_volume_server
    OPTIONS (access_key_id 'admin', secret_access_key 'password');
CREATE FOREIGN VOLUME nt_volume SERVER nt_volume_server
    OPTIONS (base_path '/nt_volume/');
SET iceberg_default_volume = 'nt_volume';

-- ============================================================
-- Create a regular table with nested-like columns
-- (placeholder until Iceberg nested type support matures)
-- ============================================================
SELECT test_log('Creating table with array and jsonb columns');

CREATE TABLE nt_nested (
    id        bigint,
    tags      text[],
    metadata  jsonb,
    scores    numeric(10,2)[]
);

-- ============================================================
-- Insert data with array and jsonb values
-- ============================================================
SELECT test_log('Inserting nested type data');

INSERT INTO nt_nested
SELECT
    g,
    ARRAY['tag_' || (g % 5), 'tag_' || (g % 10), 'tag_' || (g % 20)],
    jsonb_build_object(
        'name', 'item_' || g,
        'category', g % 10,
        'attrs', jsonb_build_object(
            'weight', (g * 1.5)::numeric(10,2),
            'color', CASE g % 3 WHEN 0 THEN 'red' WHEN 1 THEN 'green' ELSE 'blue' END
        ),
        'history', jsonb_build_array(g, g * 2, g * 3)
    ),
    ARRAY[(g * 1.1)::numeric(10,2), (g * 2.2)::numeric(10,2), (g * 3.3)::numeric(10,2)]
FROM generate_series(1, 1000) g;

-- Verify row count
SELECT COUNT(*) AS row_count FROM nt_nested;

-- ============================================================
-- Query: Array operations
-- ============================================================
SELECT test_log('Testing array operations');

SELECT id, tags
FROM nt_nested
WHERE 'tag_0' = ANY(tags)
ORDER BY id
LIMIT 5;

SELECT id, array_length(scores, 1) AS score_count
FROM nt_nested
ORDER BY id
LIMIT 5;

-- ============================================================
-- Query: JSONB operations
-- ============================================================
SELECT test_log('Testing jsonb operations');

SELECT id,
       metadata->>'name' AS name,
       metadata->'attrs'->>'color' AS color,
       (metadata->'attrs'->>'weight')::numeric(10,2) AS weight
FROM nt_nested
WHERE (metadata->>'category')::int = 5
ORDER BY id
LIMIT 5;

-- Aggregation on jsonb-extracted values
SELECT
    metadata->'attrs'->>'color' AS color,
    COUNT(*) AS cnt,
    AVG((metadata->'attrs'->>'weight')::numeric(10,2)) AS avg_weight
FROM nt_nested
GROUP BY metadata->'attrs'->>'color'
ORDER BY color;

-- ============================================================
-- Measure: Scan with array/jsonb columns
-- ============================================================
SELECT test_log('Measuring scan with nested-like columns');

SELECT perf_run_iterations(
    'scale_nested_types', 'full_scan',
    'SELECT COUNT(*) FROM nt_nested',
    3,   -- iterations
    1,   -- warmup
    1000,
    'Placeholder: regular table with array + jsonb columns'
);

SELECT perf_run_iterations(
    'scale_nested_types', 'jsonb_filter_scan',
    $$SELECT COUNT(*) FROM nt_nested WHERE (metadata->>'category')::int < 3$$,
    3,   -- iterations
    1,   -- warmup
    NULL,
    'Placeholder: jsonb filter scan'
);

-- ============================================================
-- Report
-- ============================================================
-- start_ignore
SELECT * FROM perf_summary_report('scale_nested_types');
-- end_ignore

-- ============================================================
-- Cleanup
-- ============================================================
DROP TABLE nt_nested;
DROP VOLUME nt_volume;
DROP USER MAPPING FOR current_user SERVER nt_volume_server;
DROP SERVER nt_volume_server;
DROP CATALOG nt_catalog;
DROP USER MAPPING FOR current_user SERVER nt_catalog_server;
DROP SERVER nt_catalog_server;

SELECT test_log('Scale nested types placeholder test completed');
