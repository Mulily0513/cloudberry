-- ============================================================
-- cagg_watermark_concurrent_update.sql (isolation2)
-- Corresponds to TimescaleDB: tsl/test/isolation/specs/cagg_watermark_concurrent_update.spec
--
-- Tests that watermark updates from REFRESH are visible to other
-- sessions:
--   1. After REFRESH commits, other sessions see the new watermark
--   2. Real-time UNION ALL view uses the updated watermark
--   3. Multiple REFRESH cycles advance watermark monotonically
-- ============================================================

1: SET optimizer = off;
1: SET timezone = 'UTC';
1: DROP EXTENSION IF EXISTS time_series CASCADE;
1: CREATE EXTENSION time_series;
1: SET search_path TO public, time_series;

1: CREATE TABLE temperature (time TIMESTAMPTZ NOT NULL, val FLOAT8)
   DISTRIBUTED BY (time);

-- Initial data: year 2000
1: INSERT INTO temperature
   SELECT '2000-01-01'::timestamptz + (i * interval '1 min'), (i % 100)::float8
   FROM generate_series(1, 1440) i;

-- CAGG with real-time mode (materialized_only = false)
1: CREATE MATERIALIZED VIEW cagg WITH (time_series.continuous) AS
   SELECT time_bucket('4 hour'::interval, time) AS bucket, avg(val) AS avg_val
   FROM temperature GROUP BY 1;

1: CALL time_series.refresh_continuous_aggregate('cagg', NULL, NULL);

-- Add new data in 2020
1: INSERT INTO temperature
   SELECT '2020-01-01'::timestamptz + (i * interval '1 min'), (i % 100)::float8
   FROM generate_series(1, 1440) i;

2: SET optimizer = off;
2: SET search_path TO public, time_series;

-- ============================================================
-- Test 1: Session 2 reads watermark BEFORE and AFTER session 1
--         REFRESHes. The watermark should advance.
--
-- Corresponds to TSDB permutation:
--   "s1_prepare" "s2_prepare" "s3_lock_invalidation"
--   "s2_select" "s1_run_update" "s2_select"
--   "s3_release_invalidation" "s2_select" "s1_select"
-- ============================================================

-- Session 2: read watermark before REFRESH
2: SELECT bool_and(watermark < '2020-01-01'::timestamptz) AS wm_before
   FROM time_series.cagg_watermark;

-- Session 2: read from CAGG (real-time mode) — should see 2020 data
-- via direct_view branch even though mat table hasn't been refreshed
-- for 2020 range yet
2: SELECT count(*) AS rows_2020_before FROM cagg WHERE bucket >= '2020-01-01';

-- Session 1: REFRESH to materialize 2020 data
1: CALL time_series.refresh_continuous_aggregate('cagg', '2020-01-01', '2025-01-01');

-- Session 2: watermark should now have advanced
2: SELECT bool_and(watermark >= '2020-01-01'::timestamptz) AS wm_after
   FROM time_series.cagg_watermark;

-- Session 2: CAGG should still return correct data
2: SELECT count(*) AS rows_2020_after FROM cagg WHERE bucket >= '2020-01-01';

-- ============================================================
-- Test 2: Multiple REFRESH cycles — watermark advances
--         monotonically (never decreases).
-- ============================================================

-- Insert more data in 2021
1: INSERT INTO temperature VALUES ('2020-01-02 23:59:59+00', 42.0);

-- Save watermark before REFRESH, then verify it doesn't decrease after
1: CREATE TEMP TABLE wm_snap AS SELECT max(watermark) AS wm FROM time_series.cagg_watermark;

-- REFRESH
1: CALL time_series.refresh_continuous_aggregate('cagg', NULL, NULL);

-- Watermark should be >= saved snapshot (monotonically increasing)
1: SELECT bool_and(w.watermark >= s.wm) AS wm_monotonic
   FROM time_series.cagg_watermark w, wm_snap s;

1: DROP TABLE wm_snap;

-- ============================================================
-- Test 3: Session 1 holds advisory lock (simulating slow REFRESH).
--         Session 2 reads CAGG view — should see consistent watermark
--         in both UNION ALL branches (no gap).
-- ============================================================

-- Session 1: hold advisory lock
1: BEGIN;
1: SELECT pg_advisory_xact_lock(
     (SELECT cagg_id FROM time_series.continuous_agg WHERE user_view_name = 'cagg')
   );

-- Session 2: query CAGG — real-time view should work normally
-- (uses the COMMITTED watermark, not the in-progress one)
2: SELECT count(*) AS rows_during_lock FROM cagg;

-- Session 2: EXCEPT should be 0 even while lock is held
2: SELECT count(*) AS diff_during_lock FROM (
     SELECT bucket, round(avg_val::numeric, 6) FROM cagg EXCEPT
     SELECT time_bucket('4 hour'::interval, time), round(avg(val)::numeric, 6)
     FROM temperature GROUP BY 1
   ) x;

-- Release
1: COMMIT;

-- Final EXCEPT = 0
1: SELECT count(*) AS diff_final FROM (
     SELECT bucket, round(avg_val::numeric, 6) FROM cagg EXCEPT
     SELECT time_bucket('4 hour'::interval, time), round(avg(val)::numeric, 6)
     FROM temperature GROUP BY 1
   ) x;

-- ============================================================
-- Test 4: Fault injection — pause before watermark advance
--         Verify session 2 sees OLD watermark during pause,
--         then NEW watermark after resume.
--         (TSDB watermark perm 1: intermediate-state verification)
-- ============================================================

1: INSERT INTO temperature
   SELECT '2024-01-01'::timestamptz + (i * interval '1 min'), i::float8
   FROM generate_series(1, 100) i;

-- Pause REFRESH before watermark update
1: SELECT gp_inject_fault('cagg_refresh_before_watermark_advance', 'suspend', dbid)
   FROM gp_segment_configuration WHERE role = 'p' AND content = -1;

1&: CALL time_series.refresh_continuous_aggregate('cagg', NULL, NULL);

2: SELECT gp_wait_until_triggered_fault('cagg_refresh_before_watermark_advance', 1, dbid)
   FROM gp_segment_configuration WHERE role = 'p' AND content = -1;

-- REFRESH paused: mat data written but watermark NOT advanced.
-- Session 2 should still see OLD watermark (pre-2024 data range).
2: SELECT bool_and(watermark < '2024-01-01'::timestamptz) AS wm_still_old
   FROM time_series.cagg_watermark;

-- Resume
2: SELECT gp_inject_fault('cagg_refresh_before_watermark_advance', 'resume', dbid)
   FROM gp_segment_configuration WHERE role = 'p' AND content = -1;
1<:

2: SELECT gp_inject_fault('cagg_refresh_before_watermark_advance', 'reset', dbid)
   FROM gp_segment_configuration WHERE role = 'p' AND content = -1;

-- Now watermark should have advanced past 2024
2: SELECT bool_and(watermark >= '2024-01-01'::timestamptz) AS wm_now_new
   FROM time_series.cagg_watermark;

-- Final correctness
1: SELECT count(*) AS diff_wm_fault FROM (
     SELECT bucket, round(avg_val::numeric, 6) FROM cagg EXCEPT
     SELECT time_bucket('4 hour'::interval, time), round(avg(val)::numeric, 6)
     FROM temperature GROUP BY 1
   ) x;

-- ============================================================
-- Test 5: Two REFRESH cycles with INSERT between them
--         Watermark advances each cycle, reader sees update
--         (TSDB watermark perm 2: two successive cycles)
-- ============================================================

-- Cycle 1: REFRESH materializes 2024 data
1: INSERT INTO temperature
   SELECT '2025-01-01'::timestamptz + (i * interval '1 min'), i::float8
   FROM generate_series(1, 100) i;

1: CALL time_series.refresh_continuous_aggregate('cagg', NULL, NULL);

-- Record watermark after cycle 1
1: CREATE TEMP TABLE wm_c1 AS SELECT max(watermark) AS wm FROM time_series.cagg_watermark;

-- Session 2 reads — should see all data including 2025
2: SELECT count(*) AS rows_after_c1 FROM cagg WHERE bucket >= '2025-01-01';

-- Cycle 2: INSERT more data, REFRESH again
1: INSERT INTO temperature VALUES ('2025-06-01 12:00:00+00', 999.0);
1: CALL time_series.refresh_continuous_aggregate('cagg', NULL, NULL);

-- Watermark should have advanced further
1: SELECT bool_and(w.watermark >= c.wm) AS wm_monotonic_c2
   FROM time_series.cagg_watermark w, wm_c1 c;

-- Session 2 sees the new data
2: SELECT count(*) AS rows_after_c2 FROM cagg WHERE bucket >= '2025-06-01';

1: DROP TABLE wm_c1;

-- Final correctness
1: SELECT count(*) AS diff_cycles FROM (
     SELECT bucket, round(avg_val::numeric, 6) FROM cagg EXCEPT
     SELECT time_bucket('4 hour'::interval, time), round(avg(val)::numeric, 6)
     FROM temperature GROUP BY 1
   ) x;

-- Cleanup
1: DROP TABLE temperature CASCADE;
1q:
2q:
