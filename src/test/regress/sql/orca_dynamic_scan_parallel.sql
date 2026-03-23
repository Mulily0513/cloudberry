-- Tests for Parallel Dynamic Seq Scan
-- Verifies that DynamicSeqScan can run in parallel mode with ORCA.

-- Enable parallel query in Cloudberry
SET enable_parallel = on;
SET min_parallel_table_scan_size = 0;
SET parallel_setup_cost = 0;
SET parallel_tuple_cost = 0;
SET max_parallel_workers_per_gather = 2;

-- Create a range-partitioned table with multiple partitions
CREATE TABLE pdss_part (a int, b int, c text) PARTITION BY RANGE (a);
CREATE TABLE pdss_part_1 PARTITION OF pdss_part FOR VALUES FROM (1) TO (100);
CREATE TABLE pdss_part_2 PARTITION OF pdss_part FOR VALUES FROM (100) TO (200);
CREATE TABLE pdss_part_3 PARTITION OF pdss_part FOR VALUES FROM (200) TO (300);
CREATE TABLE pdss_part_4 PARTITION OF pdss_part FOR VALUES FROM (300) TO (400);
CREATE TABLE pdss_part_5 PARTITION OF pdss_part FOR VALUES FROM (400) TO (500);
CREATE TABLE pdss_part_6 PARTITION OF pdss_part FOR VALUES FROM (500) TO (600);
CREATE TABLE pdss_part_7 PARTITION OF pdss_part FOR VALUES FROM (600) TO (700);
CREATE TABLE pdss_part_8 PARTITION OF pdss_part FOR VALUES FROM (700) TO (800);

-- Set parallel_workers on partitions to hint parallel execution
ALTER TABLE pdss_part_1 SET (parallel_workers = 2);
ALTER TABLE pdss_part_2 SET (parallel_workers = 2);
ALTER TABLE pdss_part_3 SET (parallel_workers = 2);
ALTER TABLE pdss_part_4 SET (parallel_workers = 2);
ALTER TABLE pdss_part_5 SET (parallel_workers = 2);
ALTER TABLE pdss_part_6 SET (parallel_workers = 2);
ALTER TABLE pdss_part_7 SET (parallel_workers = 2);
ALTER TABLE pdss_part_8 SET (parallel_workers = 2);

-- Populate with data
INSERT INTO pdss_part SELECT i, i * 2, 'row_' || i FROM generate_series(1, 799) i;
ANALYZE pdss_part;

-- =============================================================
-- Basic: Verify EXPLAIN shows Parallel Dynamic Seq Scan
-- =============================================================
EXPLAIN (COSTS OFF) SELECT count(*) FROM pdss_part WHERE b > 100;
SELECT count(*) FROM pdss_part WHERE b > 100;
SELECT count(*) FROM pdss_part WHERE a BETWEEN 200 AND 500;
SELECT sum(b) FROM pdss_part WHERE a < 100;

-- =============================================================
-- Partition pruning: single, multi, empty
-- =============================================================
EXPLAIN (COSTS OFF) SELECT count(*) FROM pdss_part WHERE a >= 1 AND a < 100;
SELECT count(*) FROM pdss_part WHERE a >= 1 AND a < 100;

EXPLAIN (COSTS OFF) SELECT * FROM pdss_part WHERE a IN (50, 150, 350);
SELECT count(*) FROM pdss_part WHERE a IN (50, 150, 350);

EXPLAIN (COSTS OFF) SELECT count(*) FROM pdss_part WHERE a > 900;
SELECT count(*) FROM pdss_part WHERE a > 900;

-- =============================================================
-- Aggregation: min, max, avg
-- =============================================================
EXPLAIN (COSTS OFF) SELECT min(a), max(a), avg(b)::int FROM pdss_part WHERE a BETWEEN 100 AND 399;
SELECT min(a), max(a), avg(b)::int FROM pdss_part WHERE a BETWEEN 100 AND 399;

-- =============================================================
-- ORDER BY + LIMIT with parallel scan
-- =============================================================
EXPLAIN (COSTS OFF) SELECT a, b FROM pdss_part WHERE a IN (1, 400, 799) ORDER BY a;
SELECT a, b FROM pdss_part WHERE a IN (1, 400, 799) ORDER BY a;

EXPLAIN (COSTS OFF) SELECT a FROM pdss_part WHERE a >= 500 ORDER BY a LIMIT 5;
SELECT a FROM pdss_part WHERE a >= 500 ORDER BY a LIMIT 5;

-- =============================================================
-- Subquery filter
-- =============================================================
CREATE TABLE pdss_dim (id int, label text);
INSERT INTO pdss_dim VALUES (1, 'low'), (2, 'mid'), (3, 'high');
ANALYZE pdss_dim;

EXPLAIN (COSTS OFF) SELECT count(*) FROM pdss_part WHERE a > (SELECT min(id) * 100 FROM pdss_dim);
SELECT count(*) FROM pdss_part WHERE a > (SELECT min(id) * 100 FROM pdss_dim);

-- =============================================================
-- Multi-column filter: partition key + non-partition key
-- =============================================================
EXPLAIN (COSTS OFF) SELECT count(*) FROM pdss_part WHERE a BETWEEN 300 AND 499 AND b < 700 AND c LIKE 'row_3%';
SELECT count(*) FROM pdss_part WHERE a BETWEEN 300 AND 499 AND b < 700 AND c LIKE 'row_3%';

-- =============================================================
-- Rescan correctness: prepared statement with generic plan
-- After pruning compacts the partition array, a rescan with new
-- params must restore the full partition list before re-pruning.
-- First 6 executions use custom plans, 7th+ uses generic plan.
-- =============================================================
PREPARE pdss_rescan(int, int) AS SELECT a, count(*) FROM pdss_part WHERE a BETWEEN $1 AND $2 GROUP BY a ORDER BY a;
-- Custom plan phase: alternate between distant partitions
EXECUTE pdss_rescan(1, 5);
EXECUTE pdss_rescan(700, 705);
EXECUTE pdss_rescan(300, 305);
EXECUTE pdss_rescan(1, 5);
EXECUTE pdss_rescan(700, 705);
EXECUTE pdss_rescan(300, 305);
-- Generic plan phase: must still return correct results
EXECUTE pdss_rescan(1, 5);
EXECUTE pdss_rescan(700, 705);
EXECUTE pdss_rescan(300, 305);
DEALLOCATE pdss_rescan;

-- =============================================================
-- Parallel rescan via join
-- Each driver key hits a different partition, exercising parallel
-- scan and rescan across partitions.
-- =============================================================
CREATE TABLE pdss_driver (key int);
INSERT INTO pdss_driver VALUES (50), (250), (550), (750);
ANALYZE pdss_driver;

EXPLAIN (COSTS OFF)
SELECT d.key, p.a, p.b
FROM pdss_driver d JOIN pdss_part p ON p.a = d.key
ORDER BY d.key;

SELECT d.key, p.a, p.b
FROM pdss_driver d JOIN pdss_part p ON p.a = d.key
ORDER BY d.key;

SELECT d.key, count(*)
FROM pdss_driver d JOIN pdss_part p ON p.a = d.key
GROUP BY d.key
ORDER BY d.key;

-- =============================================================
-- List partitioning
-- =============================================================
CREATE TABLE pdss_list (region text, val int, info text) PARTITION BY LIST (region);
CREATE TABLE pdss_list_east PARTITION OF pdss_list FOR VALUES IN ('east');
CREATE TABLE pdss_list_west PARTITION OF pdss_list FOR VALUES IN ('west');
CREATE TABLE pdss_list_north PARTITION OF pdss_list FOR VALUES IN ('north');
CREATE TABLE pdss_list_south PARTITION OF pdss_list FOR VALUES IN ('south');

ALTER TABLE pdss_list_east SET (parallel_workers = 2);
ALTER TABLE pdss_list_west SET (parallel_workers = 2);
ALTER TABLE pdss_list_north SET (parallel_workers = 2);
ALTER TABLE pdss_list_south SET (parallel_workers = 2);

INSERT INTO pdss_list
SELECT (ARRAY['east','west','north','south'])[1 + (i % 4)], i, 'info_' || i
FROM generate_series(1, 400) i;
ANALYZE pdss_list;

EXPLAIN (COSTS OFF) SELECT count(*) FROM pdss_list WHERE region = 'east';
SELECT count(*) FROM pdss_list WHERE region = 'east';

EXPLAIN (COSTS OFF) SELECT count(*) FROM pdss_list WHERE region IN ('east', 'west');
SELECT count(*) FROM pdss_list WHERE region IN ('east', 'west');

EXPLAIN (COSTS OFF) SELECT region, sum(val) FROM pdss_list GROUP BY region ORDER BY region;
SELECT region, sum(val) FROM pdss_list GROUP BY region ORDER BY region;

-- =============================================================
-- Default partition
-- =============================================================
CREATE TABLE pdss_def (a int, b int) PARTITION BY RANGE (a);
CREATE TABLE pdss_def_p1 PARTITION OF pdss_def FOR VALUES FROM (1) TO (100);
CREATE TABLE pdss_def_p2 PARTITION OF pdss_def FOR VALUES FROM (100) TO (200);
CREATE TABLE pdss_def_default PARTITION OF pdss_def DEFAULT;

ALTER TABLE pdss_def_p1 SET (parallel_workers = 2);
ALTER TABLE pdss_def_p2 SET (parallel_workers = 2);
ALTER TABLE pdss_def_default SET (parallel_workers = 2);

INSERT INTO pdss_def SELECT i, i * 2 FROM generate_series(1, 300) i;
ANALYZE pdss_def;

EXPLAIN (COSTS OFF) SELECT count(*) FROM pdss_def WHERE a > 150;
SELECT count(*) FROM pdss_def WHERE a > 150;

EXPLAIN (COSTS OFF) SELECT count(*) FROM pdss_def WHERE a >= 200;
SELECT count(*) FROM pdss_def WHERE a >= 200;

-- =============================================================
-- NULL handling with partition key
-- =============================================================
INSERT INTO pdss_def VALUES (NULL, 999);
ANALYZE pdss_def;
EXPLAIN (COSTS OFF) SELECT count(*) FROM pdss_def WHERE a IS NULL;
SELECT count(*) FROM pdss_def WHERE a IS NULL;
SELECT count(*) FROM pdss_def WHERE a IS NOT NULL;

-- =============================================================
-- Create a second partitioned table for join tests
-- =============================================================
CREATE TABLE pdss_other (a int, b int, c text) PARTITION BY RANGE (a);
CREATE TABLE pdss_other_1 PARTITION OF pdss_other FOR VALUES FROM (1) TO (400);
CREATE TABLE pdss_other_2 PARTITION OF pdss_other FOR VALUES FROM (400) TO (800);
ALTER TABLE pdss_other_1 SET (parallel_workers = 2);
ALTER TABLE pdss_other_2 SET (parallel_workers = 2);
INSERT INTO pdss_other SELECT i, i * 3, 'other_' || i FROM generate_series(1, 799) i;
ANALYZE pdss_other;

-- =============================================================
-- Left outer join with partitioned tables
-- =============================================================
EXPLAIN (COSTS OFF)
SELECT count(*)
FROM pdss_part p LEFT JOIN pdss_other o ON p.a = o.a AND o.b > 1500
WHERE p.a BETWEEN 1 AND 300;

SELECT count(*)
FROM pdss_part p LEFT JOIN pdss_other o ON p.a = o.a AND o.b > 1500
WHERE p.a BETWEEN 1 AND 300;

-- =============================================================
-- Anti-join (NOT EXISTS) with parallel scan
-- Workaround: disable parallel append to avoid incorrect DPE
-- pruning on the outer side of anti-joins (known bug).
-- =============================================================
SET optimizer_enable_parallel_append = off;
EXPLAIN (COSTS OFF)
SELECT count(*)
FROM pdss_part p
WHERE NOT EXISTS (SELECT 1 FROM pdss_other o WHERE o.a = p.a AND o.b > 2000);

SELECT count(*)
FROM pdss_part p
WHERE NOT EXISTS (SELECT 1 FROM pdss_other o WHERE o.a = p.a AND o.b > 2000);
RESET optimizer_enable_parallel_append;

-- =============================================================
-- Semi-join (EXISTS) with parallel scan
-- =============================================================
EXPLAIN (COSTS OFF)
SELECT count(*)
FROM pdss_part p
WHERE EXISTS (SELECT 1 FROM pdss_other o WHERE o.a = p.a AND o.b < 500);

SELECT count(*)
FROM pdss_part p
WHERE EXISTS (SELECT 1 FROM pdss_other o WHERE o.a = p.a AND o.b < 500);

-- =============================================================
-- Window functions over parallel scan
-- =============================================================
EXPLAIN (COSTS OFF)
SELECT a, b, row_number() OVER (ORDER BY a) AS rn
FROM pdss_part
WHERE a BETWEEN 1 AND 10
ORDER BY a;

SELECT a, b, row_number() OVER (ORDER BY a) AS rn
FROM pdss_part
WHERE a BETWEEN 1 AND 10
ORDER BY a;

EXPLAIN (COSTS OFF)
SELECT a, sum(b) OVER (PARTITION BY a / 100) AS partition_sum
FROM pdss_part
WHERE a BETWEEN 1 AND 20
ORDER BY a;

SELECT a, sum(b) OVER (PARTITION BY a / 100) AS partition_sum
FROM pdss_part
WHERE a BETWEEN 1 AND 20
ORDER BY a;

-- =============================================================
-- DISTINCT with parallel scan
-- =============================================================
EXPLAIN (COSTS OFF)
SELECT DISTINCT c FROM pdss_part WHERE a BETWEEN 1 AND 5 ORDER BY c;

SELECT DISTINCT c FROM pdss_part WHERE a BETWEEN 1 AND 5 ORDER BY c;

-- =============================================================
-- GROUP BY + HAVING with parallel scan
-- =============================================================
EXPLAIN (COSTS OFF)
SELECT a / 100 AS bucket, count(*), sum(b)
FROM pdss_part
GROUP BY a / 100
HAVING count(*) > 50
ORDER BY bucket;

SELECT a / 100 AS bucket, count(*), sum(b)
FROM pdss_part
GROUP BY a / 100
HAVING count(*) > 50
ORDER BY bucket;

-- =============================================================
-- CTE with parallel scan
-- =============================================================
EXPLAIN (COSTS OFF)
WITH part_stats AS (
    SELECT a / 100 AS bucket, count(*) AS cnt, sum(b) AS total
    FROM pdss_part
    GROUP BY a / 100
)
SELECT bucket, cnt, total
FROM part_stats
WHERE cnt > 50
ORDER BY bucket;

WITH part_stats AS (
    SELECT a / 100 AS bucket, count(*) AS cnt, sum(b) AS total
    FROM pdss_part
    GROUP BY a / 100
)
SELECT bucket, cnt, total
FROM part_stats
WHERE cnt > 50
ORDER BY bucket;

-- =============================================================
-- UNION ALL across partitioned tables
-- =============================================================
EXPLAIN (COSTS OFF)
SELECT 'part' AS src, count(*) FROM pdss_part WHERE a < 100
UNION ALL
SELECT 'other' AS src, count(*) FROM pdss_other WHERE a < 100
ORDER BY src;

SELECT 'part' AS src, count(*) FROM pdss_part WHERE a < 100
UNION ALL
SELECT 'other' AS src, count(*) FROM pdss_other WHERE a < 100
ORDER BY src;

-- =============================================================
-- optimizer_disable_dynamic_table_scan: no parallel when disabled
-- Regression guard for the duplicate-rows bug fix
-- =============================================================
SET optimizer_disable_dynamic_table_scan = on;
EXPLAIN (COSTS OFF) SELECT count(*) FROM pdss_part WHERE b > 100;
SELECT count(*) FROM pdss_part WHERE b > 100;
SET optimizer_disable_dynamic_table_scan = off;

-- =============================================================
-- Mixed partition: heap + external table via EXCHANGE PARTITION
-- Parallel Dynamic Seq Scan does not support foreign partitions,
-- so mixed tables should fall back to Append.
-- =============================================================
CREATE TABLE pdss_mixed (col1 int, col2 int, col3 text)
DISTRIBUTED BY (col1)
PARTITION BY LIST(col2) (
  PARTITION part1 VALUES(1,2,3,4,5),
  PARTITION part2 VALUES(6,7,8,9,10)
);
INSERT INTO pdss_mixed SELECT i, i, 'heap_row' FROM generate_series(1,10) i;

CREATE READABLE EXTERNAL WEB TABLE pdss_ext_swap (col1 int, col2 int, col3 text)
EXECUTE 'echo "1,1,ext_row
2,2,ext_row
3,3,ext_row
4,4,ext_row
5,5,ext_row"' ON COORDINATOR FORMAT 'csv';

ALTER TABLE pdss_mixed EXCHANGE PARTITION part1 WITH TABLE pdss_ext_swap WITHOUT VALIDATION;
ANALYZE pdss_mixed;

EXPLAIN (COSTS OFF) SELECT * FROM pdss_mixed;
SELECT count(*) FROM pdss_mixed;

DROP TABLE pdss_mixed CASCADE;

-- =============================================================
-- Cleanup
-- =============================================================
DROP TABLE pdss_driver;
DROP TABLE pdss_part;
DROP TABLE pdss_dim;
DROP TABLE pdss_list;
DROP TABLE pdss_def;
DROP TABLE pdss_other;
RESET enable_parallel;
RESET min_parallel_table_scan_size;
RESET parallel_setup_cost;
RESET parallel_tuple_cost;
RESET max_parallel_workers_per_gather;
