-- Regression coverage for the Limit+Sort TopK optimization.
--
-- build_topk_node records the K value on VecSortState->topk_bound when
-- the new HTAB-based path decides to emit an Arrow TopKNode instead of
-- an OrderByNode.  show_sort_info surfaces it as
--     "Vec Sort Method:  TopK  K: N"
-- in EXPLAIN output.  The "Vec" prefix distinguishes this from the row
-- engine's "Sort Method:  top-N heapsort" so users can tell which
-- execution engine made the decision.
--
-- Each branch below leaves a distinct, diff-visible footprint:
--   (1) TopK active    : line appears with K = expected value
--   (2) K=0 guard      : line absent (compute_tuples_needed = 0)
--   (3) K>threshold    : line absent (tuples_needed > topk_bound_threshold)
--   (4) no LIMIT       : line absent (no Limit above the Sort)
--   (5) inner Sort     : line on the outer Sort only, not on aggregation's
--                        internal sort-by-key (guards Failure 1 regression)
--   (6) correctness    : SELECT result matches non-TopK baseline
--   (7) row engine plan: no "Vec Sort Method" line at all (prefix guard)

-- Pin to the Postgres planner so this test ships with a single expected
-- output file.  ORCA and the Postgres planner produce different plan
-- shapes for LIMIT 0 and for GroupAgg-over-redistributed-data; under
-- ORCA we would need a separate <name>_optimizer.out.  The TopK path
-- itself is planner-agnostic so pinning to PG is sufficient coverage.
SET optimizer = off;
SET vector.enable_vectorization = ON;

DROP TABLE IF EXISTS topk_t;
CREATE TABLE topk_t (a int, grp int) USING pax DISTRIBUTED BY (a);
INSERT INTO topk_t
  SELECT g,
         CASE WHEN g <= 50 THEN 1 WHEN g <= 80 THEN 2 ELSE 3 END
    FROM generate_series(1, 100) g;
ANALYZE topk_t;

-- (1) Baseline TopK path.
EXPLAIN (costs off) SELECT a FROM topk_t ORDER BY a LIMIT 3;
SELECT a FROM topk_t ORDER BY a LIMIT 3;
SELECT a FROM topk_t ORDER BY a DESC LIMIT 3;

-- (2) K=0 guard.  Must return zero rows and EXPLAIN must not show the
-- TopK method line (the guard is `tuples_needed > 0` in nodeLimit.c).
EXPLAIN (costs off) SELECT a FROM topk_t ORDER BY a LIMIT 0;
SELECT a FROM topk_t ORDER BY a LIMIT 0;

-- (3) K above threshold falls back to OrderBy + SelectK legacy path.
-- EXPLAIN must not show Vec Sort Method: TopK; result still correct.
SET vector.topk_bound_threshold = 5;
EXPLAIN (costs off) SELECT a FROM topk_t ORDER BY a LIMIT 20;
SELECT count(*) FROM (SELECT a FROM topk_t ORDER BY a LIMIT 20) q;
RESET vector.topk_bound_threshold;

-- (4) No LIMIT — the vec plan has a plain Vec Sort and no TopK line.
EXPLAIN (costs off) SELECT a FROM topk_t ORDER BY a;

-- (5) Aggregation + outer Sort+Limit.  Before the
-- ExecVecSetTupleBound early-return fix, the legacy make_topk_node
-- scan would target the GroupAgg's internal sort-by-key (the only
-- remaining OrderByNode after the outer Sort became a TopKNode) and
-- truncate it, collapsing COUNT(*) results.  The plan here must show
-- Vec Sort Method: TopK on the OUTER sort only; the aggregation's
-- internal sort must not have the line, and the results must match
-- the non-aggregated group counts.
EXPLAIN (costs off)
  SELECT grp, count(*) AS c FROM topk_t
    GROUP BY grp ORDER BY c DESC LIMIT 2;
SELECT grp, count(*) AS c FROM topk_t
    GROUP BY grp ORDER BY c DESC LIMIT 2;
SELECT grp, count(*) AS c FROM topk_t
    GROUP BY grp ORDER BY c DESC LIMIT 10;
SELECT grp, sum(a) AS s FROM topk_t
    GROUP BY grp ORDER BY s DESC LIMIT 3;

-- (6) Correctness vs non-vectorized baseline for the TopK path.
SET vector.enable_vectorization = OFF;
SELECT a FROM topk_t ORDER BY a LIMIT 3;
SET vector.enable_vectorization = ON;

-- (7) Row-engine (heap) plan must not carry the "Vec Sort Method" line
-- at all — the prefix guard prevents the extension's display from
-- bleeding into non-vectorized EXPLAIN output.
DROP TABLE IF EXISTS heap_t;
CREATE TABLE heap_t (a int) DISTRIBUTED BY (a);
INSERT INTO heap_t SELECT g FROM generate_series(1, 100) g;
ANALYZE heap_t;
EXPLAIN (costs off) SELECT a FROM heap_t ORDER BY a LIMIT 3;
DROP TABLE heap_t;

DROP TABLE topk_t;

RESET optimizer;
