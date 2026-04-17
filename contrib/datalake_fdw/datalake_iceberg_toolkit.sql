-- Iceberg Toolkit Functions
-- Core functions for Iceberg catalog and volume operations

-- Create schema
CREATE SCHEMA IF NOT EXISTS iceberg_toolkit;

-- Catalog operations (create_table, get_fragment)
CREATE OR REPLACE FUNCTION iceberg_toolkit.catalog_fdw(
    operation text,
    name_space text,
    table_name text,
    catalog_server_name text,
    catalog_table_name text,
    volume_server_name text,
    volume_table_name text,
    append_json_string text
)
RETURNS text
AS '$libdir/datalake_fdw.so', 'iceberg_toolkit_catalog_fdw'
LANGUAGE C STRICT;

-- Volume operations (read data)
CREATE OR REPLACE FUNCTION iceberg_toolkit.volume_fdw(
    operation text,
    name_space text,
    table_name text,
    row_limit integer,
    catalog_server_name text,
    catalog_table_name text,
    volume_server_name text,
    volume_table_name text
)
RETURNS SETOF record
AS '$libdir/datalake_fdw.so', 'iceberg_toolkit_volume_fdw'
LANGUAGE C STRICT;

-- Convenience functions
CREATE OR REPLACE FUNCTION iceberg_toolkit.create_table(
    table_name text,
    name_space text,
    catalog_server text,
    catalog_table text,
    volume_server text,
    volume_table text
)
RETURNS text
AS $$
    SELECT iceberg_toolkit.catalog_fdw(
        'create_table', name_space, table_name,
        catalog_server, catalog_table, volume_server, volume_table, ""
    );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION iceberg_toolkit.get_fragments(
    table_name text,
    name_space text,
    catalog_server text,
    catalog_table text,
    volume_server text,
    volume_table text
)
RETURNS text
AS $$
    SELECT iceberg_toolkit.catalog_fdw(
        'get_fragment', name_space, table_name,
        catalog_server, catalog_table, volume_server, volume_table, ""
    );
$$ LANGUAGE SQL;

-- Permissions
GRANT USAGE ON SCHEMA iceberg_toolkit TO public;
GRANT EXECUTE ON ALL FUNCTIONS IN SCHEMA iceberg_toolkit TO public;
