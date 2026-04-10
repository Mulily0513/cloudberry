-- ============================================================
-- cagg_concurrent_refresh.sql (isolation2)
-- Aligned with TimescaleDB: cagg_concurrent_refresh.spec (9 perms)
--
-- Tests REFRESH serialization using:
-- - LOCK TABLE on mat table (simulates ongoing REFRESH holding lock)
-- - Advisory locks (our native REFRESH locking mechanism)
-- - gp_inject_fault for mid-REFRESH pause
--
-- Fault injection points used:
-- - cagg_refresh_before_watermark_advance (pause before watermark update)
-- ============================================================

-- ============================================================
-- Setup
-- ============================================================
1: SET optimizer = off;
1: DROP EXTENSION IF EXISTS time_series CASCADE;
1: CREATE EXTENSION time_series;
1: SET search_path TO public, time_series;

1: CREATE TABLE cond (time TIMESTAMPTZ NOT NULL, device INT NOT NULL, temp FLOAT8)
   DISTRIBUTED BY (device);
1: INSERT INTO cond SELECT '2024-01-01'::timestamptz + (i * interval '6 min'),
   (i % 5) + 1, i * 0.5 FROM generate_series(1, 500) i;

-- Two CAGGs on same source (different bucket widths)
1: CREATE MATERIALIZED VIEW cv_1h WITH (time_series.continuous) AS
   SELECT time_bucket('1 hour'::interval, time) AS bucket,
          device, count(*) AS cnt
   FROM cond GROUP BY bucket, device;

1: CREATE MATERIALIZED VIEW cv_2h WITH (time_series.continuous) AS
   SELECT time_bucket('2 hour'::interval, time) AS bucket,
          count(*) AS cnt
   FROM cond GROUP BY bucket;

-- Second source table + CAGG (for cross-source independence test)
1: CREATE TABLE cond2 (time TIMESTAMPTZ NOT NULL, device INT NOT NULL, temp FLOAT8)
   DISTRIBUTED BY (device);
1: INSERT INTO cond2 SELECT '2024-01-01'::timestamptz + (i * interval '6 min'),
   (i % 3) + 1, i FROM generate_series(1, 200) i;
1: CREATE MATERIALIZED VIEW cv_cond2 WITH (time_series.continuous) AS
   SELECT time_bucket('1 hour'::interval, time) AS bucket,
          count(*) AS cnt
   FROM cond2 GROUP BY bucket;

-- Initial REFRESH to establish watermarks
1: CALL time_series.refresh_continuous_aggregate('cv_1h', NULL, NULL);
1: CALL time_series.refresh_continuous_aggregate('cv_2h', NULL, NULL);
1: CALL time_series.refresh_continuous_aggregate('cv_cond2', NULL, NULL);

-- Insert backfill data to create dirty intervals
1: INSERT INTO cond SELECT '2024-01-01 01:30+00'::timestamptz + (i * interval '1 min'),
   1, i FROM generate_series(1, 10) i;
1: INSERT INTO cond SELECT '2024-01-01 03:30+00'::timestamptz + (i * interval '1 min'),
   2, i FROM generate_series(1, 10) i;

2: SET optimizer = off;
2: SET search_path TO public, time_series;
3: SET optimizer = off;
3: SET search_path TO public, time_series;

-- ============================================================
-- Perm 1: Baseline — single REFRESH + SELECT, no concurrency
--         (TSDB concurrent_refresh perm 1)
-- ============================================================
1: CALL time_series.refresh_continuous_aggregate('cv_1h', NULL, NULL);
1: SELECT count(*) AS cv1h_rows FROM cv_1h;

-- ============================================================
-- Perm 5: LOCK mat table → REFRESH blocked → release → verify
--         (TSDB concurrent_refresh perm 5)
--         Core test: advisory lock serializes REFRESH
-- ============================================================

-- Re-dirty
1: INSERT INTO cond VALUES ('2024-01-01 02:30+00', 3, 100.0);

-- Session 1 locks mat table (simulates ongoing REFRESH)
1: BEGIN;
1: DO $$ BEGIN
     PERFORM 1 FROM time_series.continuous_agg WHERE user_view_name = 'cv_1h';
     EXECUTE format('LOCK TABLE time_series.%I IN EXCLUSIVE MODE',
       (SELECT mat_table_name FROM time_series.continuous_agg WHERE user_view_name = 'cv_1h'));
   END $$;

-- Session 2 REFRESH → blocks on advisory lock (which acquires mat table lock internally)
2&: CALL time_series.refresh_continuous_aggregate('cv_1h', NULL, NULL);

-- Verify session 2 is waiting
1: SELECT count(*) > 0 AS s2_waiting
   FROM pg_stat_activity
   WHERE query LIKE '%refresh_continuous_aggregate%'
     AND wait_event_type = 'Lock'
     AND pid != pg_backend_pid();

-- Release
1: COMMIT;
2<:

-- No duplicate rows
1: SELECT count(*) AS dup_rows FROM (
     SELECT bucket, device, count(*) FROM time_series._mat_cv_1h_1
     GROUP BY bucket, device HAVING count(*) > 1
   ) x;

-- ============================================================
-- Perm 6: TWO queued REFRESHes serialize, no duplicate rows [P0]
--         (TSDB concurrent_refresh perm 6)
-- ============================================================

-- Re-dirty
1: INSERT INTO cond VALUES ('2024-01-01 04:30+00', 4, 200.0);

-- Lock mat table
1: BEGIN;
1: DO $$ BEGIN
     EXECUTE format('LOCK TABLE time_series.%I IN EXCLUSIVE MODE',
       (SELECT mat_table_name FROM time_series.continuous_agg WHERE user_view_name = 'cv_1h'));
   END $$;

-- TWO sessions try REFRESH simultaneously
2&: CALL time_series.refresh_continuous_aggregate('cv_1h', NULL, NULL);
3&: CALL time_series.refresh_continuous_aggregate('cv_1h', NULL, NULL);

-- Both should be waiting
1: SELECT count(*) AS waiters
   FROM pg_stat_activity
   WHERE query LIKE '%refresh_continuous_aggregate%'
     AND wait_event_type = 'Lock'
     AND pid != pg_backend_pid();

-- Release — both proceed sequentially
1: COMMIT;
2<:
3<:

-- CRITICAL: no duplicate rows after two concurrent REFRESHes
1: SELECT count(*) AS dup_after_double FROM (
     SELECT bucket, device, count(*) FROM time_series._mat_cv_1h_1
     GROUP BY bucket, device HAVING count(*) > 1
   ) x;

-- Data still correct
1: SELECT count(*) AS diff_double FROM (
     SELECT bucket, device, cnt FROM cv_1h EXCEPT
     SELECT time_bucket('1 hour'::interval, time), device, count(*)
     FROM cond GROUP BY 1, 2
   ) x;

-- ============================================================
-- Perm 8: Different CAGGs on same source → don't block each other
--         (TSDB concurrent_refresh perm 8)
-- ============================================================

-- Dirty both CAGGs
1: INSERT INTO cond VALUES ('2024-01-01 05:30+00', 5, 300.0);

-- Lock cv_1h's mat table only
1: BEGIN;
1: DO $$ BEGIN
     EXECUTE format('LOCK TABLE time_series.%I IN EXCLUSIVE MODE',
       (SELECT mat_table_name FROM time_series.continuous_agg WHERE user_view_name = 'cv_1h'));
   END $$;

-- cv_1h REFRESH blocks
2&: CALL time_series.refresh_continuous_aggregate('cv_1h', NULL, NULL);

-- cv_2h REFRESH should NOT block (different mat table)
3: CALL time_series.refresh_continuous_aggregate('cv_2h', NULL, NULL);

-- cv_2h completed while cv_1h is still blocked
3: SELECT count(*) AS cv2h_ok FROM (
     SELECT bucket, cnt FROM cv_2h EXCEPT
     SELECT time_bucket('2 hour'::interval, time), count(*)
     FROM cond GROUP BY 1
   ) x;

-- Release cv_1h
1: COMMIT;
2<:

-- Both correct
1: SELECT count(*) AS diff_1h FROM (
     SELECT bucket, device, cnt FROM cv_1h EXCEPT
     SELECT time_bucket('1 hour'::interval, time), device, count(*)
     FROM cond GROUP BY 1, 2
   ) x;

-- ============================================================
-- Perm 9: Different source tables → don't block each other
--         (TSDB concurrent_refresh perm 9)
-- ============================================================

-- Dirty both sources
1: INSERT INTO cond VALUES ('2024-01-01 06:30+00', 1, 400.0);
1: INSERT INTO cond2 VALUES ('2024-01-01 06:30+00', 1, 400.0);

-- Both REFRESH simultaneously — should NOT block each other
2&: CALL time_series.refresh_continuous_aggregate('cv_1h', NULL, NULL);
3&: CALL time_series.refresh_continuous_aggregate('cv_cond2', NULL, NULL);
2<:
3<:

-- Both correct
1: SELECT count(*) AS diff_cross_1 FROM (
     SELECT bucket, device, cnt FROM cv_1h EXCEPT
     SELECT time_bucket('1 hour'::interval, time), device, count(*)
     FROM cond GROUP BY 1, 2
   ) x;
1: SELECT count(*) AS diff_cross_2 FROM (
     SELECT bucket, cnt FROM cv_cond2 EXCEPT
     SELECT time_bucket('1 hour'::interval, time), count(*)
     FROM cond2 GROUP BY 1
   ) x;

-- ============================================================
-- Perm FAULT: Pause REFRESH before watermark advance
--             (TSDB watermark perm 1 equivalent, using fault injection)
-- ============================================================

1: INSERT INTO cond VALUES ('2024-01-01 07:30+00', 1, 500.0);

-- Enable fault: pause REFRESH before watermark advance
1: SELECT gp_inject_fault('cagg_refresh_before_watermark_advance', 'suspend', dbid)
   FROM gp_segment_configuration WHERE role = 'p' AND content = -1;

-- Session 2 REFRESH — will pause before watermark update
2&: CALL time_series.refresh_continuous_aggregate('cv_1h', NULL, NULL);

-- Wait for session 2 to hit the fault point
1: SELECT gp_wait_until_triggered_fault('cagg_refresh_before_watermark_advance', 1, dbid)
   FROM gp_segment_configuration WHERE role = 'p' AND content = -1;

-- At this point: mat table updated but watermark NOT yet advanced
-- Session 3 can query and should see old watermark
3: SELECT count(*) AS mid_refresh_query FROM cv_1h;

-- Resume REFRESH
1: SELECT gp_inject_fault('cagg_refresh_before_watermark_advance', 'resume', dbid)
   FROM gp_segment_configuration WHERE role = 'p' AND content = -1;
2<:

-- Reset fault
1: SELECT gp_inject_fault('cagg_refresh_before_watermark_advance', 'reset', dbid)
   FROM gp_segment_configuration WHERE role = 'p' AND content = -1;

-- Final correctness
1: SELECT count(*) AS diff_final FROM (
     SELECT bucket, device, cnt FROM cv_1h EXCEPT
     SELECT time_bucket('1 hour'::interval, time), device, count(*)
     FROM cond GROUP BY 1, 2
   ) x;

-- ============================================================
-- Perm 2-4: LOCK watermark table (ACCESS SHARE) → REFRESH blocked
--           (TSDB concurrent_refresh perm 2-4: threshold reader blocks refresh)
--           Our equivalent: LOCK cagg_watermark blocks REFRESH's
--           cagg_get_min_watermark() SPI query.
-- ============================================================

1: INSERT INTO cond VALUES ('2024-01-01 08:30+00', 1, 600.0);

-- Session 1: ACCESS SHARE lock on watermark
1: BEGIN;
1: LOCK TABLE time_series.cagg_watermark IN ACCESS SHARE MODE;

-- Session 2: REFRESH needs to update watermark (EXCLUSIVE) → blocks
2&: CALL time_series.refresh_continuous_aggregate('cv_1h', NULL, NULL);

-- Verify blocked
1: SELECT count(*) > 0 AS refresh_blocked_by_share
   FROM pg_stat_activity
   WHERE query LIKE '%refresh_continuous_aggregate%'
     AND wait_event_type = 'Lock'
     AND pid != pg_backend_pid();

1: COMMIT;
2<:

-- Data correct after unblock
1: SELECT count(*) AS diff_share FROM (
     SELECT bucket, device, cnt FROM cv_1h EXCEPT
     SELECT time_bucket('1 hour'::interval, time), device, count(*)
     FROM cond GROUP BY 1, 2
   ) x;

-- ============================================================
-- Perm 7: Non-overlapping refresh windows still serialize
--         (TSDB concurrent_refresh perm 7)
--         Two REFRESHes with different windows on same CAGG
--         should still serialize (conservative locking).
-- ============================================================

1: INSERT INTO cond VALUES ('2024-01-01 01:15+00', 1, 700.0);
1: INSERT INTO cond VALUES ('2024-01-01 09:15+00', 2, 800.0);

-- Lock mat table
1: BEGIN;
1: DO $$ BEGIN
     EXECUTE format('LOCK TABLE time_series.%I IN EXCLUSIVE MODE',
       (SELECT mat_table_name FROM time_series.continuous_agg WHERE user_view_name = 'cv_1h'));
   END $$;

-- Two non-overlapping windows
2&: CALL time_series.refresh_continuous_aggregate('cv_1h', '2024-01-01 01:00+00', '2024-01-01 02:00+00');
3&: CALL time_series.refresh_continuous_aggregate('cv_1h', '2024-01-01 09:00+00', '2024-01-01 10:00+00');

1: SELECT count(*) AS non_overlap_waiters
   FROM pg_stat_activity
   WHERE query LIKE '%refresh_continuous_aggregate%'
     AND wait_event_type = 'Lock'
     AND pid != pg_backend_pid();

1: COMMIT;
2<:
3<:

-- Both windows refreshed correctly
1: SELECT count(*) AS diff_nonoverlap FROM (
     SELECT bucket, device, cnt FROM cv_1h EXCEPT
     SELECT time_bucket('1 hour'::interval, time), device, count(*)
     FROM cond GROUP BY 1, 2
   ) x;

-- ============================================================
-- Cleanup
-- ============================================================
1: DROP TABLE cond CASCADE;
1: DROP TABLE cond2 CASCADE;
1q:
2q:
3q:
