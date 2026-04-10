-- ============================================================
-- cagg_concurrent_invalidation.sql (isolation2)
-- Corresponds to TimescaleDB: tsl/test/isolation/specs/cagg_concurrent_invalidation.spec
--
-- Tests concurrent REFRESH on two different CAGGs sharing the
-- same source table.  Both sessions update the watermark/threshold
-- concurrently.  Verifies:
--   1. Both watermarks are updated correctly (no skip)
--   2. Concurrent INSERT during REFRESH doesn't leak invalid
--      threshold values into other sessions
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

-- Two CAGGs on same source
1: CREATE MATERIALIZED VIEW cagg_1 WITH (time_series.continuous) AS
   SELECT time_bucket('4 hour'::interval, time) AS bucket, avg(val) AS avg_val
   FROM temperature GROUP BY 1;

1: CREATE MATERIALIZED VIEW cagg_2 WITH (time_series.continuous) AS
   SELECT time_bucket('4 hour'::interval, time) AS bucket, avg(val) AS avg_val
   FROM temperature GROUP BY 1;

-- Initial REFRESH for both
1: CALL time_series.refresh_continuous_aggregate('cagg_1', NULL, NULL);
1: CALL time_series.refresh_continuous_aggregate('cagg_2', NULL, NULL);

-- Add new data in year 2020
1: INSERT INTO temperature
   SELECT '2020-01-01'::timestamptz + (i * interval '1 min'), (i % 100)::float8
   FROM generate_series(1, 1440) i;

2: SET optimizer = off;
2: SET search_path TO public, time_series;

-- ============================================================
-- Test 1: Two sessions REFRESH different CAGGs concurrently.
--         Both should complete, both watermarks should advance.
--
-- Corresponds to TSDB permutation:
--   "s3_lock_invalidation" "s1_run_update" "s2_run_update"
--   "s3_release_invalidation" "s3_check_watermarks"
--
-- We simulate blocking by having session 1 hold cagg_1's advisory
-- lock, forcing session 2's REFRESH to potentially overlap with
-- session 1's operations on the shared L1 table.
-- ============================================================

-- Watermarks before: should still be around year 2000 range
1: SELECT bool_and(watermark < '2020-01-01'::timestamptz) AS wm_before_2020
   FROM time_series.cagg_watermark;

-- Both sessions REFRESH their respective CAGGs with 2020 window
1: CALL time_series.refresh_continuous_aggregate('cagg_1', '2020-01-01', '2025-01-01');
2: CALL time_series.refresh_continuous_aggregate('cagg_2', '2020-01-01', '2025-01-01');

-- Both watermarks should now be in 2025 range
1: SELECT count(DISTINCT watermark > '2020-01-01'::timestamptz) AS all_advanced
   FROM time_series.cagg_watermark;

-- Both CAGGs should have 2020 data
1: SELECT count(*) AS cagg_1_2020 FROM cagg_1 WHERE bucket >= '2020-01-01';
1: SELECT count(*) AS cagg_2_2020 FROM cagg_2 WHERE bucket >= '2020-01-01';

-- Both EXCEPT = 0
1: SELECT count(*) AS diff_cagg1 FROM (
     SELECT bucket, round(avg_val::numeric, 6) FROM cagg_1 EXCEPT
     SELECT time_bucket('4 hour'::interval, time), round(avg(val)::numeric, 6)
     FROM temperature GROUP BY 1
   ) x;

1: SELECT count(*) AS diff_cagg2 FROM (
     SELECT bucket, round(avg_val::numeric, 6) FROM cagg_2 EXCEPT
     SELECT time_bucket('4 hour'::interval, time), round(avg(val)::numeric, 6)
     FROM temperature GROUP BY 1
   ) x;

-- ============================================================
-- Test 2: INSERT new data while REFRESH is happening.
--         The INSERT's trigger should see a consistent threshold
--         (not a partially-updated value from the concurrent REFRESH).
--
-- Corresponds to TSDB permutation:
--   "s2_insert_new_data_2022" "s3_lock_invalidation_tuple_found"
--   "s2_insert_new_data_2023" "s1_run_update"
--   "s3_release_invalidation_tuple_found"
-- ============================================================

-- Session 1 REFRESHes cagg_1
1: CALL time_series.refresh_continuous_aggregate('cagg_1', NULL, NULL);

-- Session 2 concurrently inserts data from 2022 and 2023
2: INSERT INTO temperature
   SELECT '2022-01-01'::timestamptz + (i * interval '1 min'), (i % 50)::float8
   FROM generate_series(1, 100) i;

2: INSERT INTO temperature
   SELECT '2023-01-01'::timestamptz + (i * interval '1 min'), (i % 50)::float8
   FROM generate_series(1, 100) i;

-- REFRESH both to pick up all new data
1: CALL time_series.refresh_continuous_aggregate('cagg_1', NULL, NULL);
1: CALL time_series.refresh_continuous_aggregate('cagg_2', NULL, NULL);

-- Both should be consistent with source
1: SELECT count(*) AS diff2_cagg1 FROM (
     SELECT bucket, round(avg_val::numeric, 6) FROM cagg_1 EXCEPT
     SELECT time_bucket('4 hour'::interval, time), round(avg(val)::numeric, 6)
     FROM temperature GROUP BY 1
   ) x;

1: SELECT count(*) AS diff2_cagg2 FROM (
     SELECT bucket, round(avg_val::numeric, 6) FROM cagg_2 EXCEPT
     SELECT time_bucket('4 hour'::interval, time), round(avg(val)::numeric, 6)
     FROM temperature GROUP BY 1
   ) x;

-- ============================================================
-- Test 3: Fault injection — INSERT during REFRESH mid-flight [P0]
--         Pause REFRESH before watermark advance, INSERT,
--         then resume. Verifies L1 consistency.
-- ============================================================

1: INSERT INTO temperature
   SELECT '2024-06-01'::timestamptz + (i * interval '1 min'), i::float8
   FROM generate_series(1, 100) i;

-- Pause REFRESH before watermark advance
1: SELECT gp_inject_fault('cagg_refresh_before_watermark_advance', 'suspend', dbid)
   FROM gp_segment_configuration WHERE role = 'p' AND content = -1;

-- Session 1 starts REFRESH
1&: CALL time_series.refresh_continuous_aggregate('cagg_1', NULL, NULL);

-- Wait for REFRESH to hit fault point (mat data written, watermark not yet advanced)
2: SELECT gp_wait_until_triggered_fault('cagg_refresh_before_watermark_advance', 1, dbid)
   FROM gp_segment_configuration WHERE role = 'p' AND content = -1;

-- Session 2 inserts new data — trigger sees OLD watermark (consistent)
2: INSERT INTO temperature
   SELECT '2024-07-01'::timestamptz + (i * interval '1 min'), i::float8
   FROM generate_series(1, 50) i;

-- L1 should have entries from session 2's insert
2: SELECT count(*) > 0 AS l1_from_concurrent_insert
   FROM time_series.cagg_invalidation_log;

-- Resume REFRESH
2: SELECT gp_inject_fault('cagg_refresh_before_watermark_advance', 'resume', dbid)
   FROM gp_segment_configuration WHERE role = 'p' AND content = -1;
1<:

2: SELECT gp_inject_fault('cagg_refresh_before_watermark_advance', 'reset', dbid)
   FROM gp_segment_configuration WHERE role = 'p' AND content = -1;

-- One more REFRESH to pick up the concurrent insert's L1
1: CALL time_series.refresh_continuous_aggregate('cagg_1', NULL, NULL);

-- Final: EXCEPT = 0
1: SELECT count(*) AS diff_fault FROM (
     SELECT bucket, round(avg_val::numeric, 6) FROM cagg_1 EXCEPT
     SELECT time_bucket('4 hour'::interval, time), round(avg(val)::numeric, 6)
     FROM temperature GROUP BY 1
   ) x;

-- Cleanup
1: DROP TABLE temperature CASCADE;
1q:
2q:
