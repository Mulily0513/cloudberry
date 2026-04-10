-- ============================================================
-- cagg_concurrent_l1_migration.sql (isolation2)
--
-- Tests the scenario where two different CAGGs on the same
-- source table have their TX1 (L1→L2 migration) overlapping.
--
-- Design doc reference: Q5 in Q&A section.
--
-- Scenario:
--   Session 1: REFRESH cagg_a → TX1 migrates L1, pauses before commit
--   Session 2: REFRESH cagg_b → TX1 tries to migrate the SAME L1 entries
--
-- Verified behavior (CBDB-specific):
--   CBDB's 2PC protocol causes segment workers to enter "prepared"
--   state after completing L1→L2 migration.  Prepared-state changes
--   are visible to new snapshots on the same segment.  Therefore:
--     1. Session 2 sees L1 as EMPTY (session 1's deletes visible)
--     2. Session 2 sees L2 entries (session 1's inserts visible)
--     3. Session 2's TX1 is a no-op; TX2 reads from L2 normally
--     4. No row lock conflicts, no duplicate L2, no errors
--     5. Both CAGGs end up with correct materialized data
--
-- This behavior differs from single-node PostgreSQL where
-- uncommitted changes are invisible and would cause row conflicts.
-- ============================================================

-- ============================================================
-- Setup
-- ============================================================

1: SET optimizer = off;
1: SET timezone = 'UTC';
1: DROP EXTENSION IF EXISTS time_series CASCADE;
1: CREATE EXTENSION time_series;
1: SET search_path TO public, time_series;

1: CREATE TABLE sensor (time TIMESTAMPTZ NOT NULL, device INT, val FLOAT8)
   DISTRIBUTED BY (device);

-- Initial data
1: INSERT INTO sensor
   SELECT '2024-01-01'::timestamptz + (i * interval '1 min'), (i % 3) + 1, i::float8
   FROM generate_series(1, 360) i;

-- Two CAGGs on the same source with different bucket widths
1: CREATE MATERIALIZED VIEW cagg_a WITH (time_series.continuous) AS
   SELECT time_bucket('1 hour'::interval, time) AS bucket, sum(val) AS total
   FROM sensor GROUP BY 1;

1: CREATE MATERIALIZED VIEW cagg_b WITH (time_series.continuous) AS
   SELECT time_bucket('2 hour'::interval, time) AS bucket, avg(val) AS avg_val
   FROM sensor GROUP BY 1;

-- Initial REFRESH both to establish watermarks
1: CALL time_series.refresh_continuous_aggregate('cagg_a', NULL, NULL);
1: CALL time_series.refresh_continuous_aggregate('cagg_b', NULL, NULL);

-- Verify baseline: both are correct
1: SELECT count(*) AS baseline_a FROM cagg_a;
1: SELECT count(*) AS baseline_b FROM cagg_b;

-- ============================================================
-- Test 1: Concurrent TX1 with fault injection
--
-- Insert new data to create L1 entries, then pause session 1's
-- TX1 after L1→L2 migration but before commit.  Session 2
-- tries to REFRESH the other CAGG concurrently.
-- ============================================================

-- Insert new data → triggers write L1 entries
1: INSERT INTO sensor
   SELECT '2024-06-01'::timestamptz + (i * interval '1 min'), (i % 3) + 1, i::float8
   FROM generate_series(1, 360) i;

-- Verify L1 has entries
1: SELECT count(*) > 0 AS l1_has_entries FROM time_series.cagg_invalidation_log;

-- Set fault: pause CAGG A's REFRESH after L1→L2 but before TX1 commit
1: SELECT gp_inject_fault('cagg_refresh_before_commit_and_chain', 'suspend', dbid)
   FROM gp_segment_configuration WHERE role = 'p' AND content = -1;

2: SET optimizer = off;
2: SET search_path TO public, time_series;

-- Session 1: start REFRESH cagg_a (will pause after L1→L2 migration)
1&: CALL time_series.refresh_continuous_aggregate('cagg_a', NULL, NULL);

-- Wait for session 1 to hit the fault point
-- At this point: L1→L2 migration done for ALL CAGGs, but NOT committed
2: SELECT gp_wait_until_triggered_fault('cagg_refresh_before_commit_and_chain', 1, dbid)
   FROM gp_segment_configuration WHERE role = 'p' AND content = -1;

-- Check: L1 entries should still exist (session 1's delete not committed)
2: SELECT count(*) > 0 AS l1_still_visible FROM time_series.cagg_invalidation_log;

-- Session 2: REFRESH cagg_b while session 1's TX1 is uncommitted
-- This will attempt L1→L2 migration on the SAME L1 entries.
-- Possible outcomes:
--   a) Session 2 blocks on L1 row locks (session 1 holds xmax)
--   b) Session 2 errors on simple_heap_delete (tuple already deleted)
--   c) Session 2 succeeds (race condition allows duplicate L2)
2&: CALL time_series.refresh_continuous_aggregate('cagg_b', NULL, NULL);

-- Give session 2 a moment to either block or proceed
3: SET optimizer = off;
3: SET search_path TO public, time_series;
3: SELECT pg_sleep(2);

-- Now release session 1's fault → TX1 commits, L1 entries become deleted
3: SELECT gp_inject_fault('cagg_refresh_before_commit_and_chain', 'resume', dbid)
   FROM gp_segment_configuration WHERE role = 'p' AND content = -1;

-- Collect results from both sessions
1<:
2<:

3: SELECT gp_inject_fault('cagg_refresh_before_commit_and_chain', 'reset', dbid)
   FROM gp_segment_configuration WHERE role = 'p' AND content = -1;

-- ============================================================
-- Verify: Check state after concurrent TX1
-- ============================================================

-- L1 should be empty (all entries consumed)
1: SELECT count(*) AS l1_remaining FROM time_series.cagg_invalidation_log;

-- Check L2: any leftover entries?
1: SELECT count(*) AS l2_remaining FROM time_series.cagg_materialization_log;

-- Check watermarks: did both advance?
1: SELECT w.cagg_id,
          c.user_view_name,
          bool_and(w.watermark > '2024-01-02'::timestamptz) AS wm_advanced
   FROM time_series.cagg_watermark w
   JOIN time_series.continuous_agg c ON w.cagg_id = c.cagg_id
   GROUP BY w.cagg_id, c.user_view_name
   ORDER BY w.cagg_id;

-- Check correctness: EXCEPT = 0 means mat data matches source
1: SELECT count(*) AS diff_a FROM (
     SELECT bucket, round(total::numeric, 6) FROM cagg_a EXCEPT
     SELECT time_bucket('1 hour'::interval, time), round(sum(val)::numeric, 6)
     FROM sensor GROUP BY 1
   ) x;

1: SELECT count(*) AS diff_b FROM (
     SELECT bucket, round(avg_val::numeric, 6) FROM cagg_b EXCEPT
     SELECT time_bucket('2 hour'::interval, time), round(avg(val)::numeric, 6)
     FROM sensor GROUP BY 1
   ) x;

-- ============================================================
-- Test 2: If session 2 failed above, retry and verify recovery
-- ============================================================

-- Retry REFRESH for both (idempotent, should always succeed)
1: CALL time_series.refresh_continuous_aggregate('cagg_a', NULL, NULL);
1: CALL time_series.refresh_continuous_aggregate('cagg_b', NULL, NULL);

-- Final correctness check
1: SELECT count(*) AS final_diff_a FROM (
     SELECT bucket, round(total::numeric, 6) FROM cagg_a EXCEPT
     SELECT time_bucket('1 hour'::interval, time), round(sum(val)::numeric, 6)
     FROM sensor GROUP BY 1
   ) x;

1: SELECT count(*) AS final_diff_b FROM (
     SELECT bucket, round(avg_val::numeric, 6) FROM cagg_b EXCEPT
     SELECT time_bucket('2 hour'::interval, time), round(avg(val)::numeric, 6)
     FROM sensor GROUP BY 1
   ) x;

-- Both should have June data
1: SELECT count(*) AS a_june FROM cagg_a WHERE bucket >= '2024-06-01';
1: SELECT count(*) AS b_june FROM cagg_b WHERE bucket >= '2024-06-01';

-- ============================================================
-- Cleanup
-- ============================================================
1: DROP TABLE sensor CASCADE;
1q:
2q:
3q:
