-- Parallel WindowHashAgg spill regression test.
--
-- Separate session from window_hashagg_spill.sql so the concurrent-spill
-- path (SpillableBatchStore::Append / Evict on Arrow worker threads)
-- starts from a clean state — the arrow-side SpillMemoryManager carries
-- per-plan state across a single backend's queries, and mixing non-
-- spilling runs with an aggressive-budget spill run in one session has
-- been observed to hang on Interconnect (tracked separately).
--
-- winagg_spill_memory_mb (default 512 MB) is the sole spill-budget knob;
-- work_mem no longer influences it after the GUC rename. We set a 1 MB
-- budget so the 10000-row working set actually exceeds the budget and
-- eviction runs on worker threads concurrently with Append — the exact
-- path that used to deadlock / crash in GP's fd.c (VfdCache, LRU)
-- before the switch to LocalFileSystem + POSIX defaults.

SET vector.enable_vectorization = on;
SET optimizer = on;
SET optimizer_force_window_hash_agg = on;
SET default_table_access_method = pax;

DROP TABLE IF EXISTS winagg_spill_parallel_t1;
CREATE TABLE winagg_spill_parallel_t1 (
    id int,
    partition_col int,
    order_col int,
    value int
) DISTRIBUTED BY (id);

INSERT INTO winagg_spill_parallel_t1
SELECT
    i,
    i % 20 AS partition_col,
    i / 20 AS order_col,
    (i * 7 + 13) % 1000 AS value
FROM generate_series(1, 10000) i;

ANALYZE winagg_spill_parallel_t1;

-- First exercise the spill-disabled path (winagg_spill_memory_mb = 0):
-- BuildAggregatation routes WindowHashAggState through the plain
-- (non-spill) parallel-window options call -- no spill_dir, no mkdir.
-- Without this query the disable-spill branch in build_aggregatation_options
-- is never executed in CI.
SET vector.winagg_spill_memory_mb = 0;
SELECT count(*) AS rows_no_spill,
       sum(rn) AS sum_rn
FROM (
    SELECT row_number() OVER (PARTITION BY partition_col ORDER BY order_col) AS rn
    FROM winagg_spill_parallel_t1
) sub;
RESET vector.winagg_spill_memory_mb;

SET vector.pool_threads = 4;
SET vector.winagg_spill_memory_mb = 1;

-- One combined window-agg query covering row_number, rank, and the
-- GroupedMinMax path across concurrent evictions.
SELECT count(*) AS total_rows,
       count(DISTINCT partition_col) AS num_partitions,
       sum(CASE WHEN rn = 1 THEN 1 ELSE 0 END) AS first_rows,
       sum(CASE WHEN rnk <= 3 THEN 1 ELSE 0 END) AS top3_per_partition
FROM (
    SELECT
        partition_col,
        row_number() OVER (PARTITION BY partition_col ORDER BY order_col) AS rn,
        rank() OVER (PARTITION BY partition_col ORDER BY value) AS rnk
    FROM winagg_spill_parallel_t1
) sub;

RESET vector.winagg_spill_memory_mb;
RESET vector.pool_threads;

DROP TABLE winagg_spill_parallel_t1;

RESET optimizer_force_window_hash_agg;
RESET default_table_access_method;
RESET optimizer;
RESET vector.enable_vectorization;
