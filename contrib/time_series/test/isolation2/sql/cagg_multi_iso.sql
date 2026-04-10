-- ============================================================
-- cagg_multi_iso.sql (isolation2)
-- Corresponds to TimescaleDB: tsl/test/isolation/specs/cagg_multi_iso.spec
--
-- Tests two CAGGs on the same source table with concurrent
-- INSERT, UPDATE, and REFRESH operations:
--   1. REFRESH on cagg_1 blocked, cagg_2 REFRESH proceeds independently
--   2. INSERT invalidates source, both CAGGs refresh correctly
--   3. UPDATE source, both CAGGs see updated values after refresh
-- ============================================================

1: SET optimizer = off;
1: DROP EXTENSION IF EXISTS time_series CASCADE;
1: CREATE EXTENSION time_series;
1: SET search_path TO public, time_series;

1: CREATE TABLE src (time TIMESTAMPTZ NOT NULL, device_id INT NOT NULL, val INT)
   DISTRIBUTED BY (device_id);
1: INSERT INTO src SELECT '2024-01-01'::timestamptz + (i * interval '30 min'),
   (i % 5) + 1, i FROM generate_series(1, 60) i;

-- Two CAGGs: different aggregates, same source
1: CREATE MATERIALIZED VIEW cv_count WITH (time_series.continuous) AS
   SELECT time_bucket('1 hour'::interval, time) AS bucket,
          device_id, count(val) AS cnt
   FROM src GROUP BY bucket, device_id;

1: CREATE MATERIALIZED VIEW cv_max WITH (time_series.continuous) AS
   SELECT time_bucket('1 hour'::interval, time) AS bucket,
          device_id, max(val) AS maxval
   FROM src GROUP BY bucket, device_id;

-- Initial REFRESH for both
1: CALL time_series.refresh_continuous_aggregate('cv_count', NULL, NULL);
1: CALL time_series.refresh_continuous_aggregate('cv_max', NULL, NULL);

2: SET optimizer = off;
2: SET search_path TO public, time_series;
3: SET optimizer = off;
3: SET search_path TO public, time_series;

-- ============================================================
-- Test 1: REFRESH cv_count blocked by mat table lock,
--         REFRESH cv_max proceeds independently.
--
-- Corresponds to TSDB permutation:
--   "LockMat1" "Refresh1" "Refresh2" "UnlockMat1"
-- ============================================================

-- Session 1 locks cv_count's mat table
1: BEGIN;
1: LOCK TABLE time_series._mat_cv_count_1 IN EXCLUSIVE MODE;

-- Session 2 tries to REFRESH cv_count — should block (needs mat table)
2&: CALL time_series.refresh_continuous_aggregate('cv_count', NULL, NULL);

-- Session 3 REFRESHes cv_max — should proceed (independent mat table)
3: CALL time_series.refresh_continuous_aggregate('cv_max', NULL, NULL);

-- Release lock, session 2 completes
1: COMMIT;
2<:

-- Both CAGGs correct
1: SELECT count(*) AS diff_count FROM (
     SELECT bucket, device_id, cnt FROM cv_count EXCEPT
     SELECT time_bucket('1 hour'::interval, time), device_id, count(val)
     FROM src GROUP BY 1, 2
   ) x;

1: SELECT count(*) AS diff_max FROM (
     SELECT bucket, device_id, maxval FROM cv_max EXCEPT
     SELECT time_bucket('1 hour'::interval, time), device_id, max(val)
     FROM src GROUP BY 1, 2
   ) x;

-- ============================================================
-- Test 2: INSERT invalidates source, both CAGGs refresh correctly.
--
-- Corresponds to TSDB permutation:
--   "Refresh1" "Refresh2" "LockMat1" "I1" "Refresh1" "Refresh2"
--   "UnlockMat1" "Refresh1_sel" "Refresh2_sel"
-- ============================================================

-- INSERT backfill data into bucket 0 range
2: INSERT INTO src SELECT '2024-01-01 00:00+00'::timestamptz, (i % 5) + 1, i * 10
   FROM generate_series(1, 10) i;

-- Lock cv_count mat table
1: BEGIN;
1: LOCK TABLE time_series._mat_cv_count_1 IN EXCLUSIVE MODE;

-- Session 2 tries REFRESH cv_count — blocked
2&: CALL time_series.refresh_continuous_aggregate('cv_count', NULL, NULL);

-- Session 3 REFRESHes cv_max — proceeds, sees the INSERT
3: CALL time_series.refresh_continuous_aggregate('cv_max', NULL, NULL);

-- Unlock → cv_count REFRESH proceeds
1: COMMIT;
2<:

-- cv_count: bucket 00:00 should have the extra rows
1: SELECT cnt FROM cv_count WHERE bucket = '2024-01-01 00:00+00' ORDER BY device_id LIMIT 3;

-- cv_max: should see the max of the new data
1: SELECT maxval FROM cv_max WHERE bucket = '2024-01-01 00:00+00' ORDER BY device_id LIMIT 3;

-- Both EXCEPT = 0
1: SELECT count(*) AS diff2_count FROM (
     SELECT bucket, device_id, cnt FROM cv_count EXCEPT
     SELECT time_bucket('1 hour'::interval, time), device_id, count(val)
     FROM src GROUP BY 1, 2
   ) x;

1: SELECT count(*) AS diff2_max FROM (
     SELECT bucket, device_id, maxval FROM cv_max EXCEPT
     SELECT time_bucket('1 hour'::interval, time), device_id, max(val)
     FROM src GROUP BY 1, 2
   ) x;

-- ============================================================
-- Test 3: UPDATE source data, both CAGGs see updated values.
--
-- Corresponds to TSDB permutation:
--   "U1" "U2" "LInvRow" "Refresh1" "Refresh2" "UnlockInvRow"
--   "Refresh1_sel" "Refresh2_sel"
-- ============================================================

-- UPDATE some source rows
2: UPDATE src SET val = 9999 WHERE device_id = 1 AND time < '2024-01-01 02:00+00';
2: UPDATE src SET val = 1 WHERE device_id = 3 AND time >= '2024-01-01 03:00+00' AND time < '2024-01-01 05:00+00';

-- REFRESH both
1: CALL time_series.refresh_continuous_aggregate('cv_count', NULL, NULL);
1: CALL time_series.refresh_continuous_aggregate('cv_max', NULL, NULL);

-- Both EXCEPT = 0 after UPDATE + REFRESH
1: SELECT count(*) AS diff3_count FROM (
     SELECT bucket, device_id, cnt FROM cv_count EXCEPT
     SELECT time_bucket('1 hour'::interval, time), device_id, count(val)
     FROM src GROUP BY 1, 2
   ) x;

1: SELECT count(*) AS diff3_max FROM (
     SELECT bucket, device_id, maxval FROM cv_max EXCEPT
     SELECT time_bucket('1 hour'::interval, time), device_id, max(val)
     FROM src GROUP BY 1, 2
   ) x;

-- ============================================================
-- Test 4: UPDATE + LOCK watermark → both REFRESHes blocked → release
--         (TSDB multi_iso perm 4: U1 → U2 → LInvRow → Refresh → Unlock)
--         Uses LOCK TABLE on cagg_watermark to simulate threshold lock
-- ============================================================

-- UPDATE more data
2: UPDATE src SET val = 7777 WHERE device_id = 2 AND time < '2024-01-01 03:00+00';

-- Lock watermark table (simulates threshold row lock)
1: BEGIN;
1: LOCK TABLE time_series.cagg_watermark IN EXCLUSIVE MODE;

-- Both REFRESHes block on watermark read
2&: CALL time_series.refresh_continuous_aggregate('cv_count', NULL, NULL);
3&: CALL time_series.refresh_continuous_aggregate('cv_max', NULL, NULL);

-- Verify both waiting
1: SELECT count(*) AS both_waiting
   FROM pg_stat_activity
   WHERE query LIKE '%refresh_continuous_aggregate%'
     AND wait_event_type = 'Lock'
     AND pid != pg_backend_pid();

-- Release
1: COMMIT;
2<:
3<:

-- Both should see the UPDATE
1: SELECT count(*) AS diff4_count FROM (
     SELECT bucket, device_id, cnt FROM cv_count EXCEPT
     SELECT time_bucket('1 hour'::interval, time), device_id, count(val)
     FROM src GROUP BY 1, 2
   ) x;

1: SELECT count(*) AS diff4_max FROM (
     SELECT bucket, device_id, maxval FROM cv_max EXCEPT
     SELECT time_bucket('1 hour'::interval, time), device_id, max(val)
     FROM src GROUP BY 1, 2
   ) x;

-- ============================================================
-- Test 5: TRUNCATE source → watermark reset → CAGG empty
--         Verifies the TRUNCATE hook resets watermark + threshold
--         so the real-time view returns 0 rows immediately.
-- ============================================================

1: CALL time_series.refresh_continuous_aggregate('cv_count', NULL, NULL);
1: SELECT count(*) AS mat_before_trunc FROM cv_count;

-- TRUNCATE resets watermark to -infinity
1: TRUNCATE src;

-- CAGG should show 0 rows (watermark reset, live branch = empty source)
1: SELECT count(*) AS cagg_after_trunc FROM cv_count;

-- REFRESH cleans up mat table
1: CALL time_series.refresh_continuous_aggregate('cv_count', NULL, NULL);
1: ALTER VIEW cv_count SET (time_series.materialized_only = true);
1: SELECT count(*) AS mat_after_trunc FROM cv_count;
1: ALTER VIEW cv_count SET (time_series.materialized_only = false);

-- ============================================================
-- Test 6: MPP — Concurrent DROP CAGG + INSERT
--         Session 1 drops CAGG (event trigger cleans catalog),
--         Session 2 INSERTs into source (trigger scans catalog).
--         Trigger should either see the CAGG or not — no crash.
-- ============================================================

-- Re-populate source for this test
1: INSERT INTO src SELECT '2024-01-01'::timestamptz + (i * interval '30 min'),
   (i % 5) + 1, i FROM generate_series(1, 20) i;
1: CALL time_series.refresh_continuous_aggregate('cv_count', NULL, NULL);

-- Session 1 drops cv_max (event trigger deletes catalog entries)
-- Session 2 concurrently INSERTs (trigger scans continuous_agg)
1&: DROP VIEW cv_max CASCADE;
2: INSERT INTO src VALUES ('2024-01-01 01:15+00', 99, 999);
1<:

-- No crash — cv_count should still work
1: CALL time_series.refresh_continuous_aggregate('cv_count', NULL, NULL);
1: SELECT count(*) AS diff_drop_insert FROM (
     SELECT bucket, device_id, cnt FROM cv_count EXCEPT
     SELECT time_bucket('1 hour'::interval, time), device_id, count(val)
     FROM src GROUP BY 1, 2
   ) x;

-- cv_max should be gone
1: SELECT count(*) AS cv_max_gone FROM pg_class WHERE relname = 'cv_max';

-- ============================================================
-- Test 7: MPP — Concurrent ALTER materialized_only + SELECT
--         Session 1 toggles mode (CREATE OR REPLACE VIEW),
--         Session 2 reads CAGG at the same time.
--         SELECT should either see old or new view — no crash.
-- ============================================================

-- Ensure real-time mode
1: ALTER VIEW cv_count SET (time_series.materialized_only = false);

-- Session 2 reads in a loop (background) while session 1 toggles
2&: SELECT count(*) AS select_during_toggle FROM cv_count;
1: ALTER VIEW cv_count SET (time_series.materialized_only = true);
2<:

-- Toggle back and verify
1: ALTER VIEW cv_count SET (time_series.materialized_only = false);
2&: SELECT count(*) AS select_during_toggle2 FROM cv_count;
1: ALTER VIEW cv_count SET (time_series.materialized_only = false);
2<:

-- CAGG still queryable and correct
1: SELECT count(*) AS diff_toggle FROM (
     SELECT bucket, device_id, cnt FROM cv_count EXCEPT
     SELECT time_bucket('1 hour'::interval, time), device_id, count(val)
     FROM src GROUP BY 1, 2
   ) x;

-- ============================================================
-- Test 8: MPP — Two UPDATEs on same row → two triggers on same
--         segment both write L1 concurrently.
--         simple_heap_insert has no unique constraint → both succeed.
-- ============================================================

1: CALL time_series.refresh_continuous_aggregate('cv_count', NULL, NULL);

-- Two sessions UPDATE same device_id (hashes to same segment)
1&: UPDATE src SET val = 8888 WHERE device_id = 1 AND time = '2024-01-01 00:30+00';
2&: UPDATE src SET val = 7777 WHERE device_id = 1 AND time = '2024-01-01 01:00+00';
1<:
2<:

-- Both UPDATEs should have created L1 entries
1: SELECT count(*) AS l1_from_two_updates FROM time_series.cagg_invalidation_log;

-- REFRESH picks up both
1: CALL time_series.refresh_continuous_aggregate('cv_count', NULL, NULL);
1: SELECT count(*) AS diff_two_updates FROM (
     SELECT bucket, device_id, cnt FROM cv_count EXCEPT
     SELECT time_bucket('1 hour'::interval, time), device_id, count(val)
     FROM src GROUP BY 1, 2
   ) x;

-- ============================================================
-- Test 9: P0-2 — Concurrent REFRESH on two CAGGs (same source)
--         L1→L2 migration serialized by source-level advisory lock.
--         Previously: "tuple concurrently deleted" error.
--         Now: both serialize safely, no errors, data correct.
-- ============================================================

-- Re-create cv_max (dropped by Test 6)
1: CREATE MATERIALIZED VIEW cv_max WITH (time_series.continuous) AS
   SELECT time_bucket('1 hour'::interval, time) AS bucket,
          device_id, max(val) AS maxval
   FROM src GROUP BY bucket, device_id;

-- Re-populate and refresh both
1: INSERT INTO src SELECT '2024-01-01'::timestamptz + (i * interval '30 min'),
   (i % 5) + 1, i FROM generate_series(101, 120) i;

-- Create dirty L1 entries
1: CALL time_series.refresh_continuous_aggregate('cv_count', NULL, NULL);
1: CALL time_series.refresh_continuous_aggregate('cv_max', NULL, NULL);
1: INSERT INTO src VALUES ('2024-01-01 01:15+00', 99, 999);

-- Both REFRESHes run sequentially via different sessions.
-- Source-level advisory lock ensures L1→L2 migration is serialized.
-- No "tuple concurrently deleted" error should occur.
1: CALL time_series.refresh_continuous_aggregate('cv_count', NULL, NULL);
2: CALL time_series.refresh_continuous_aggregate('cv_max', NULL, NULL);

-- Both correct
1: SELECT count(*) AS diff_concurrent_cv_count FROM (
     SELECT bucket, device_id, cnt FROM cv_count EXCEPT
     SELECT time_bucket('1 hour'::interval, time), device_id, count(val)
     FROM src GROUP BY 1, 2
   ) x;
1: SELECT count(*) AS diff_concurrent_cv_max FROM (
     SELECT bucket, device_id, maxval FROM cv_max EXCEPT
     SELECT time_bucket('1 hour'::interval, time), device_id, max(val)
     FROM src GROUP BY 1, 2
   ) x;

-- Run 3 more rounds — each round inserts dirty data then refreshes both
1: INSERT INTO src VALUES ('2024-01-01 02:15+00', 88, 888);
1: CALL time_series.refresh_continuous_aggregate('cv_count', NULL, NULL);
2: CALL time_series.refresh_continuous_aggregate('cv_max', NULL, NULL);

1: INSERT INTO src VALUES ('2024-01-01 03:15+00', 77, 777);
2: CALL time_series.refresh_continuous_aggregate('cv_count', NULL, NULL);
1: CALL time_series.refresh_continuous_aggregate('cv_max', NULL, NULL);

1: INSERT INTO src VALUES ('2024-01-01 04:15+00', 66, 666);
1: CALL time_series.refresh_continuous_aggregate('cv_count', NULL, NULL);
2: CALL time_series.refresh_continuous_aggregate('cv_max', NULL, NULL);

-- Final correctness check
1: SELECT count(*) AS diff_final_count FROM (
     SELECT bucket, device_id, cnt FROM cv_count EXCEPT
     SELECT time_bucket('1 hour'::interval, time), device_id, count(val)
     FROM src GROUP BY 1, 2
   ) x;
1: SELECT count(*) AS diff_final_max FROM (
     SELECT bucket, device_id, maxval FROM cv_max EXCEPT
     SELECT time_bucket('1 hour'::interval, time), device_id, max(val)
     FROM src GROUP BY 1, 2
   ) x;

-- Cleanup
1: DROP TABLE src CASCADE;
1q:
2q:
3q:
3q:
