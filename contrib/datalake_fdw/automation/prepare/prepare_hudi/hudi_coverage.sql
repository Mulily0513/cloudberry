-- Hudi Coverage Data Preparation
-- Run via Spark SQL: creates additional tables for deep HUDI code coverage
-- Targets: hudi_logfile_block_reader.c, hudi_merged_logfile_record_reader.c,
--          hudi_btree_merger.c, hudi_hashtab_merger.c, hudi_deltalog_filter.c,
--          hudi_task_reader.c, provider/common/utils.c, avro_block_reader.cpp

-- ============================================================
-- Table 1: MOR with many delta logs (exercises merger paths)
-- ============================================================
DROP TABLE IF EXISTS hudi_mor_multi_delta;
CREATE TABLE hudi_mor_multi_delta
USING hudi
OPTIONS (
    type = 'mor',
    primaryKey = 'id',
    preCombineField = 'ts',
    hoodie.table.name = 'hudi_mor_multi_delta',
    hoodie.compact.inline = 'false'
)
AS SELECT * FROM (
    SELECT CAST(1 AS BIGINT) as id, 'init_1' as name, CAST(10.00 AS DOUBLE) as price, CAST(1 AS BIGINT) as ts
    UNION ALL SELECT 2, 'init_2', CAST(20.00 AS DOUBLE), 2
    UNION ALL SELECT 3, 'init_3', CAST(30.00 AS DOUBLE), 3
    UNION ALL SELECT 4, 'init_4', CAST(40.00 AS DOUBLE), 4
    UNION ALL SELECT 5, 'init_5', CAST(50.00 AS DOUBLE), 5
    UNION ALL SELECT 6, 'init_6', CAST(60.00 AS DOUBLE), 6
    UNION ALL SELECT 7, 'init_7', CAST(70.00 AS DOUBLE), 7
    UNION ALL SELECT 8, 'init_8', CAST(80.00 AS DOUBLE), 8
    UNION ALL SELECT 9, 'init_9', CAST(90.00 AS DOUBLE), 9
    UNION ALL SELECT 10, 'init_10', CAST(100.00 AS DOUBLE), 10
) t;

-- Multiple rounds of updates to create multiple delta log files
INSERT INTO hudi_mor_multi_delta
SELECT * FROM (
    SELECT CAST(1 AS BIGINT) as id, 'update1_1' as name, CAST(11.00 AS DOUBLE) as price, CAST(100 AS BIGINT) as ts
    UNION ALL SELECT 3, 'update1_3', CAST(33.00 AS DOUBLE), 101
    UNION ALL SELECT 5, 'update1_5', CAST(55.00 AS DOUBLE), 102
) t;

INSERT INTO hudi_mor_multi_delta
SELECT * FROM (
    SELECT CAST(2 AS BIGINT) as id, 'update2_2' as name, CAST(22.00 AS DOUBLE) as price, CAST(200 AS BIGINT) as ts
    UNION ALL SELECT 4, 'update2_4', CAST(44.00 AS DOUBLE), 201
    UNION ALL SELECT 6, 'update2_6', CAST(66.00 AS DOUBLE), 202
) t;

INSERT INTO hudi_mor_multi_delta
SELECT * FROM (
    SELECT CAST(1 AS BIGINT) as id, 'update3_1' as name, CAST(111.00 AS DOUBLE) as price, CAST(300 AS BIGINT) as ts
    UNION ALL SELECT 7, 'update3_7', CAST(77.00 AS DOUBLE), 301
    UNION ALL SELECT 8, 'update3_8', CAST(88.00 AS DOUBLE), 302
    UNION ALL SELECT 9, 'update3_9', CAST(99.00 AS DOUBLE), 303
) t;

-- ============================================================
-- Table 2: MOR with bulk data (exercises block reader with larger data)
-- ============================================================
DROP TABLE IF EXISTS hudi_mor_bulk;
CREATE TABLE hudi_mor_bulk
USING hudi
OPTIONS (
    type = 'mor',
    primaryKey = 'id',
    preCombineField = 'ts',
    hoodie.table.name = 'hudi_mor_bulk',
    hoodie.compact.inline = 'false'
)
AS
SELECT
    id,
    CONCAT('name_', CAST(id AS STRING)) as name,
    CAST(id * 1.5 AS DOUBLE) as price,
    CAST(id AS BIGINT) as ts
FROM (
    SELECT explode(sequence(CAST(1 AS BIGINT), CAST(100 AS BIGINT))) as id
) t;

-- Bulk update (even IDs get updated)
INSERT INTO hudi_mor_bulk
SELECT
    id,
    CONCAT('updated_', CAST(id AS STRING)) as name,
    CAST(id * 2.5 AS DOUBLE) as price,
    CAST(id + 1000 AS BIGINT) as ts
FROM (
    SELECT explode(sequence(CAST(2 AS BIGINT), CAST(100 AS BIGINT), CAST(2 AS BIGINT))) as id
) t;

-- ============================================================
-- Table 3: COW with many types (exercises type handling in HUDI)
-- ============================================================
DROP TABLE IF EXISTS hudi_cow_types;
CREATE TABLE hudi_cow_types
USING hudi
OPTIONS (
    type = 'cow',
    primaryKey = 'id',
    preCombineField = 'ts',
    hoodie.table.name = 'hudi_cow_types'
)
AS SELECT * FROM (
    SELECT CAST(1 AS BIGINT) as id,
        'text_val' as str_col,
        CAST(123456789 AS BIGINT) as big_col,
        CAST(3.14 AS DOUBLE) as dbl_col,
        CAST(true AS BOOLEAN) as bool_col,
        CAST('2024-06-15' AS DATE) as date_col,
        CAST(1718448600 AS BIGINT) as ts_col,
        CAST(1 AS BIGINT) as ts
    UNION ALL
    SELECT
        2,
        'another_text',
        CAST(-987654321 AS BIGINT),
        CAST(-2.718 AS DOUBLE),
        CAST(false AS BOOLEAN),
        CAST('1970-01-01' AS DATE),
        CAST(0 AS BIGINT),
        CAST(2 AS BIGINT)
    UNION ALL
    SELECT
        3,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        CAST(3 AS BIGINT)
) t;

-- ============================================================
-- Table 4: MOR partitioned with deltas per partition
-- ============================================================
DROP TABLE IF EXISTS hudi_mor_part_delta;
CREATE TABLE hudi_mor_part_delta
USING hudi
OPTIONS (
    type = 'mor',
    primaryKey = 'id',
    preCombineField = 'ts',
    hoodie.table.name = 'hudi_mor_part_delta',
    hoodie.compact.inline = 'false'
)
PARTITIONED BY (region)
AS SELECT * FROM (
    SELECT CAST(1 AS BIGINT) as id, 'item_1' as name, CAST(100 AS DOUBLE) as price, CAST(1 AS BIGINT) as ts, 'east' as region
    UNION ALL SELECT 2, 'item_2', CAST(200 AS DOUBLE), 2, 'east'
    UNION ALL SELECT 3, 'item_3', CAST(300 AS DOUBLE), 3, 'west'
    UNION ALL SELECT 4, 'item_4', CAST(400 AS DOUBLE), 4, 'west'
    UNION ALL SELECT 5, 'item_5', CAST(500 AS DOUBLE), 5, 'north'
) t;

-- Update records in each partition (creates delta logs per partition)
INSERT INTO hudi_mor_part_delta
SELECT * FROM (
    SELECT CAST(1 AS BIGINT) as id, 'updated_1' as name, CAST(150 AS DOUBLE) as price, CAST(100 AS BIGINT) as ts, 'east' as region
    UNION ALL SELECT 3, 'updated_3', CAST(350 AS DOUBLE), 101, 'west'
    UNION ALL SELECT 5, 'updated_5', CAST(550 AS DOUBLE), 102, 'north'
) t;
