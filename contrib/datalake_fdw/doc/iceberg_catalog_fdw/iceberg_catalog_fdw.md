# Iceberg Catalog FDW Documentation

## Overview

The Iceberg Catalog FDW (Foreign Data Wrapper) provides integration with Apache Iceberg table format through catalog and volume management. It enables creating, managing, and accessing Iceberg tables using PostgreSQL/Greenplum foreign data wrapper infrastructure.

## Architecture

The Iceberg Catalog FDW consists of two main components:

1. **Catalog Server**: Manages table metadata through various catalog implementations (Hive Metastore, etc.)
2. **Volume Server**: Handles data storage operations on different storage systems (S3, HDFS, etc.)

## Installation

```sql
CREATE EXTENSION IF NOT EXISTS datalake_fdw;
```

## Schema and Functions

### Iceberg Toolkit Schema

The extension creates an `iceberg_toolkit` schema containing utility functions:

```sql
CREATE SCHEMA iceberg_toolkit;
```

### Main Function: catalog_fdw

```sql
CREATE FUNCTION iceberg_toolkit.catalog_fdw(
    operation text,           -- Operation type: 'create_table', 'append', 'load'
    name_space text,         -- Target namespace/database
    table_name text,         -- Table name
    schema_json text,        -- Iceberg schema definition in JSON format
    catalog_server_name text, -- Foreign catalog server name
    catalog_table_name text,  -- Catalog table identifier
    volume_server_name text,  -- Foreign volume server name
    volume_table_name text    -- Volume table identifier
)
RETURNS text;
```

## Configuration

### 1. Catalog Server Setup

Create a foreign server for catalog operations:

```sql
CREATE SERVER example_catalog_server
FOREIGN DATA WRAPPER iceberg_catalog_fdw
OPTIONS (
    server_type 'hive',
    hive_metastore_uri 'thrift://hive-metastore:9083'
);
```

#### Catalog Server Options

| Option | Description | Example |
|--------|-------------|---------|
| `server_type` | Type of catalog server | `'hive'` |
| `hive_metastore_uri` | Hive Metastore connection URI | `'thrift://hive-metastore:9083'` |

### 2. User Mapping for Catalog

```sql
CREATE USER MAPPING FOR current_user
SERVER example_catalog_server
OPTIONS (
    username 'hive_user',
    auth_method 'simple'
);
```

### 3. Foreign Catalog Creation

```sql
CREATE FOREIGN CATALOG hive_production
SERVER example_catalog_server
OPTIONS (
    catalog_name 'hive_location',
    default_namespace 'default',
    enable_metadata_cache 'true',
    metadata_cache_ttl '300',
    auto_refresh_metadata 'true',
    warehouse_location_prefix 's3a://warehouse/hive_location/'
);
```

#### Foreign Catalog Options

| Option | Description | Default | Example |
|--------|-------------|---------|---------|
| `catalog_name` | Catalog identifier | - | `'hive_location'` |
| `default_namespace` | Default namespace | `'default'` | `'default'` |
| `enable_metadata_cache` | Enable metadata caching | `'false'` | `'true'` |
| `metadata_cache_ttl` | Cache TTL in seconds | `'300'` | `'300'` |
| `auto_refresh_metadata` | Auto refresh metadata | `'false'` | `'true'` |
| `warehouse_location_prefix` | Base warehouse path | - | `'s3a://warehouse/hive_location/'` |

### 4. Volume Server Setup

Create a foreign server for data storage operations:

```sql
CREATE SERVER s3_volume_server
FOREIGN DATA WRAPPER iceberg_volume_fdw
OPTIONS (
    server_type 's3',
    endpoint 'http://minio:9000',
    region 'us-east-1',
    bucket_name 'warehouse',
    path_style_access 'true'
);
```

#### Volume Server Options

| Option | Description | Example |
|--------|-------------|---------|
| `server_type` | Storage system type | `'s3'` |
| `endpoint` | Storage endpoint URL | `'http://minio:9000'` |
| `region` | AWS region | `'us-east-1'` |
| `bucket_name` | S3 bucket name | `'warehouse'` |
| `path_style_access` | Use path-style access | `'true'` |

### 5. User Mapping for Volume

```sql
CREATE USER MAPPING FOR current_user
SERVER s3_volume_server
OPTIONS (
    username 'gpadmin',
    aws_access_key_id 'admin',
    aws_secret_access_key 'password'
);
```

### 6. Foreign Volume Creation

```sql
CREATE FOREIGN VOLUME s3_warehouse
SERVER s3_volume_server
OPTIONS (
    base_path '/warehouse/',
    enable_caching 'true',
    allow_writes 'true'
);
```

#### Foreign Volume Options

| Option | Description | Default | Example |
|--------|-------------|---------|---------|
| `base_path` | Base storage path | `'/'` | `'/warehouse/'` |
| `enable_caching` | Enable data caching | `'false'` | `'true'` |
| `allow_writes` | Allow write operations | `'false'` | `'true'` |

## Usage Examples

### Creating an Iceberg Table

```sql
SELECT iceberg_toolkit.catalog_fdw(
    'create_table',
    'default',
    'my_iceberg_table',
    '{
        "type": "struct",
        "schema-id": 0,
        "fields": [
            {
                "id": 1,
                "name": "id",
                "type": "long",
                "required": true
            },
            {
                "id": 2,
                "name": "name",
                "type": "string",
                "required": true
            },
            {
                "id": 3,
                "name": "age",
                "type": "int",
                "required": false
            }
        ]
    }',
    'example_catalog_server',
    'hive_production',
    's3_volume_server',
    's3_warehouse'
);
```

### Schema Definition Format

The `schema_json` parameter follows the Iceberg schema specification:

```json
{
    "type": "struct",
    "schema-id": 0,
    "fields": [
        {
            "id": 1,
            "name": "column_name",
            "type": "data_type",
            "required": true|false
        }
    ]
}
```

#### Supported Data Types

| Iceberg Type | Description |
|--------------|-------------|
| `boolean` | Boolean values |
| `int` | 32-bit signed integer |
| `long` | 64-bit signed integer |
| `float` | 32-bit IEEE 754 floating point |
| `double` | 64-bit IEEE 754 floating point |
| `string` | UTF-8 encoded character string |
| `binary` | Arbitrary-length byte array |
| `date` | Calendar date without timezone |
| `time` | Time of day without timezone |
| `timestamp` | Timestamp with timezone |
| `timestamptz` | Timestamp without timezone |

## Operations

### Supported Operations

| Operation | Description |
|-----------|-------------|
| `create_table` | Create a new Iceberg table |
| `append` | Append data to existing table |
| `load` | Load data from table |

## Error Handling

The function returns JSON responses indicating success or failure:

### Success Response
```json
{
    "status": "success",
    "operation": "create_table",
    "table": "my_table",
    "message": "Table created successfully"
}
```

### Error Response
```json
{
    "status": "error",
    "operation": "create_table",
    "table": "my_table",
    "error": "Table already exists"
}
```

## Permissions

Grant necessary permissions to users:

```sql
GRANT USAGE ON SCHEMA iceberg_toolkit TO your_user;
GRANT EXECUTE ON FUNCTION iceberg_toolkit.catalog_fdw(text, text, text, text, text, text, text, text) TO your_user;
```

## Troubleshooting

### Common Issues

1. **Connection Errors**: Verify catalog and volume server configurations
2. **Authentication Failures**: Check user mappings and credentials
3. **Schema Validation**: Ensure JSON schema follows Iceberg specification
4. **Permission Denied**: Verify user has necessary grants

### Debugging

Enable detailed logging:
```sql
SET log_min_messages = DEBUG1;
```

## Limitations

- Currently supports Hive Metastore catalog
- S3-compatible storage systems supported
- Schema evolution operations limited
- Transaction isolation depends on underlying storage

## See Also

- [Apache Iceberg Documentation](https://iceberg.apache.org/)
- [Datalake FDW Main Documentation](./datalake_fdw.md)
- [PostgreSQL Foreign Data Wrapper Documentation](https://www.postgresql.org/docs/current/fdwhandler.html)
