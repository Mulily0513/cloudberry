-- Hudi Smoke Test Data Preparation
-- Run via Spark SQL (spark-sql or beeline connected to Spark Thrift Server)
-- Creates COW, MOR, partitioned, and NULL-handling test tables
-- Uses CTAS + OPTIONS (not TBLPROPERTIES) so Hudi DataSource receives the config

-- ============================================================
-- Table 1: COW (Copy-on-Write) table
-- ============================================================
DROP TABLE IF EXISTS hudi_cow_table;
CREATE TABLE hudi_cow_table
USING hudi
OPTIONS (
    type = 'cow',
    primaryKey = 'id',
    preCombineField = 'ts',
    hoodie.table.name = 'hudi_cow_table'
)
AS SELECT * FROM (
    SELECT CAST(1 AS BIGINT) as id, 'Alice' as name, CAST(100.00 AS DOUBLE) as price, CAST(1000 AS BIGINT) as ts
    UNION ALL SELECT 2, 'Bob', CAST(200.50 AS DOUBLE), 1001
    UNION ALL SELECT 3, 'Charlie', CAST(50.75 AS DOUBLE), 1002
    UNION ALL SELECT 4, 'David', CAST(300.00 AS DOUBLE), 1003
    UNION ALL SELECT 5, 'Eve', CAST(150.25 AS DOUBLE), 1004
) t;

-- ============================================================
-- Table 2: MOR (Merge-on-Read) table
-- ============================================================
DROP TABLE IF EXISTS hudi_mor_table;
CREATE TABLE hudi_mor_table
USING hudi
OPTIONS (
    type = 'mor',
    primaryKey = 'id',
    preCombineField = 'ts',
    hoodie.table.name = 'hudi_mor_table'
)
AS SELECT * FROM (
    SELECT CAST(1 AS BIGINT) as id, 'Laptop' as name, CAST(999.99 AS DOUBLE) as price, CAST(2000 AS BIGINT) as ts
    UNION ALL SELECT 2, 'Mouse', CAST(29.99 AS DOUBLE), 2001
    UNION ALL SELECT 3, 'Keyboard', CAST(79.99 AS DOUBLE), 2002
    UNION ALL SELECT 4, 'Monitor', CAST(399.99 AS DOUBLE), 2003
    UNION ALL SELECT 5, 'Headset', CAST(149.99 AS DOUBLE), 2004
) t;

-- Insert update (creates delta log for MOR)
INSERT INTO hudi_mor_table
SELECT * FROM (
    SELECT CAST(2 AS BIGINT) as id, 'Mouse Pro' as name, CAST(39.99 AS DOUBLE) as price, CAST(2010 AS BIGINT) as ts
    UNION ALL SELECT 4, 'Monitor 4K', CAST(499.99 AS DOUBLE), 2011
) t;

-- ============================================================
-- Table 3: Partitioned table
-- ============================================================
DROP TABLE IF EXISTS hudi_partition_table;
CREATE TABLE hudi_partition_table
USING hudi
OPTIONS (
    type = 'cow',
    primaryKey = 'id',
    preCombineField = 'id',
    hoodie.table.name = 'hudi_partition_table'
)
PARTITIONED BY (region)
AS SELECT * FROM (
    SELECT CAST(1 AS BIGINT) as id, 'Order-1' as name, CAST(100.00 AS DOUBLE) as amount, 'east' as region
    UNION ALL SELECT 2, 'Order-2', CAST(200.00 AS DOUBLE), 'east'
    UNION ALL SELECT 3, 'Order-3', CAST(150.00 AS DOUBLE), 'west'
    UNION ALL SELECT 4, 'Order-4', CAST(300.00 AS DOUBLE), 'west'
    UNION ALL SELECT 5, 'Order-5', CAST(250.00 AS DOUBLE), 'north'
) t;

-- ============================================================
-- Table 4: NULL value handling table
-- ============================================================
DROP TABLE IF EXISTS hudi_null_table;
CREATE TABLE hudi_null_table
USING hudi
OPTIONS (
    type = 'cow',
    primaryKey = 'id',
    preCombineField = 'ts',
    hoodie.table.name = 'hudi_null_table'
)
AS SELECT * FROM (
    SELECT CAST(1 AS BIGINT) as id, 'has_all' as name, 100 as val, CAST(3000 AS BIGINT) as ts
    UNION ALL SELECT 2, NULL, 200, 3001
    UNION ALL SELECT 3, 'no_val', NULL, 3002
    UNION ALL SELECT 4, NULL, NULL, 3003
) t;
