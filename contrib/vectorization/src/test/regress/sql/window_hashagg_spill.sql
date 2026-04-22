-- Test WindowHashAgg spill-to-disk functionality
-- This test verifies that the vectorized WindowHashAgg operator correctly
-- spills to disk when work_mem is insufficient to hold all partition data.

-- Enable vectorization and WindowHashAgg plan generation
SET vector.enable_vectorization = on;
SET optimizer = on;
SET optimizer_force_window_hash_agg = on;

-- Use PAX storage (required for vectorization)
SET default_table_access_method = pax;

-- ============================================================================
-- Setup: Create test tables with enough data to trigger spill
-- ============================================================================

CREATE TABLE winagg_spill_t1 (
    id int,
    partition_col int,
    order_col int,
    value int
) DISTRIBUTED BY (id);

-- Insert enough data across multiple partitions to trigger spill under low work_mem
-- 10000 rows across 20 partitions = 500 rows per partition
INSERT INTO winagg_spill_t1
SELECT
    i,
    i % 20 AS partition_col,
    i / 20 AS order_col,
    (i * 7 + 13) % 1000 AS value
FROM generate_series(1, 10000) i;

ANALYZE winagg_spill_t1;

-- ============================================================================
-- Test 1: Baseline - window functions work with normal work_mem
-- ============================================================================
-- Verify results are correct without spill pressure

SELECT
    partition_col,
    count(*) AS cnt,
    sum(min_val) AS sum_min,
    sum(max_val) AS sum_max
FROM (
    SELECT
        partition_col,
        order_col,
        min(value) OVER (PARTITION BY partition_col) AS min_val,
        max(value) OVER (PARTITION BY partition_col) AS max_val
    FROM winagg_spill_t1
) sub
GROUP BY partition_col
ORDER BY partition_col;

-- ============================================================================
-- Test 2: Spill under low work_mem
-- ============================================================================
-- Reduce work_mem to force spill. With 64KB, the 10000 rows of int data
-- (~40KB per partition × 20 partitions = ~800KB) should trigger spilling.

SET work_mem = '64kB';

SELECT
    partition_col,
    count(*) AS cnt,
    sum(min_val) AS sum_min,
    sum(max_val) AS sum_max
FROM (
    SELECT
        partition_col,
        order_col,
        min(value) OVER (PARTITION BY partition_col) AS min_val,
        max(value) OVER (PARTITION BY partition_col) AS max_val
    FROM winagg_spill_t1
) sub
GROUP BY partition_col
ORDER BY partition_col;

RESET work_mem;

-- ============================================================================
-- Test 3: row_number() with spill - verify ordering is preserved
-- ============================================================================

SET work_mem = '64kB';

SELECT count(*), min(rn), max(rn)
FROM (
    SELECT
        row_number() OVER (PARTITION BY partition_col ORDER BY order_col) AS rn
    FROM winagg_spill_t1
) sub;

RESET work_mem;

-- ============================================================================
-- Test 4: rank() with spill - verify rank computation
-- ============================================================================

SET work_mem = '64kB';

SELECT partition_col, max_rank
FROM (
    SELECT
        partition_col,
        max(rnk) AS max_rank
    FROM (
        SELECT
            partition_col,
            rank() OVER (PARTITION BY partition_col ORDER BY order_col) AS rnk
        FROM winagg_spill_t1
    ) inner_sub
    GROUP BY partition_col
) sub
ORDER BY partition_col;

RESET work_mem;

-- ============================================================================
-- Test 5: sum() with ORDER BY (cumulative aggregation) and spill
-- ============================================================================

SET work_mem = '64kB';

-- Verify cumulative sum across a small subset for readability
SELECT partition_col, order_col, value, cum_sum
FROM (
    SELECT
        partition_col,
        order_col,
        value,
        sum(value) OVER (PARTITION BY partition_col ORDER BY order_col) AS cum_sum
    FROM winagg_spill_t1
    WHERE partition_col = 0
) sub
ORDER BY order_col
LIMIT 10;

RESET work_mem;

-- ============================================================================
-- Test 6: Multiple window functions in same query with spill
-- ============================================================================

SET work_mem = '64kB';

SELECT count(*) AS total_rows,
       count(DISTINCT partition_col) AS num_partitions,
       sum(CASE WHEN rn = 1 THEN 1 ELSE 0 END) AS first_rows,
       sum(CASE WHEN rnk <= 3 THEN 1 ELSE 0 END) AS top3_per_partition
FROM (
    SELECT
        partition_col,
        row_number() OVER (PARTITION BY partition_col ORDER BY order_col) AS rn,
        rank() OVER (PARTITION BY partition_col ORDER BY value) AS rnk
    FROM winagg_spill_t1
) sub;

RESET work_mem;

-- ============================================================================
-- Cleanup
-- ============================================================================

DROP TABLE winagg_spill_t1;

RESET optimizer_force_window_hash_agg;
RESET default_table_access_method;
RESET optimizer;
RESET vector.enable_vectorization;
