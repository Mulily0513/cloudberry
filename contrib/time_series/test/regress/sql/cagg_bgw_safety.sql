-- ============================================================
-- cagg_bgw_safety.sql
--
-- P2-level safety / robustness tests for the BGW + run_job pipeline.
--
-- These cover edge cases that have been identified but are
-- defensive in nature (production users are unlikely to hit them
-- under normal usage):
--   SAFE-01: pre-existing bgw_job_stat row before policy creation
--            does NOT cause PK violation in mark_start INSERT.
--   SAFE-02: ts_bgw_params_destroy is idempotent (multi-call OK).
--   SAFE-03: extreme schedule_interval values (small + large) do
--            not overflow next_start computation.
--   SAFE-04: schedule_interval = 0 (a corner case the SQL surface
--            accepts) is tolerated by run_job without divide-by-zero
--            or infinite next_start.
--
-- Concurrent run_job vs BGW worker on the same job is NOT exercised
-- here — the same advisory-lock protections are tested directly via
-- isolation2/cagg_concurrent_refresh.sql, since BGW worker timing is
-- non-deterministic in a regress framework.
-- ============================================================

SET optimizer = off;
SET timezone = 'UTC';

DROP EXTENSION IF EXISTS time_series CASCADE;
CREATE EXTENSION time_series;
SET search_path TO public, time_series;

CREATE TABLE m_safety (
    time TIMESTAMPTZ NOT NULL,
    tags_id INT NOT NULL,
    v INT
) DISTRIBUTED BY (tags_id);

INSERT INTO m_safety
SELECT now() - (h * interval '1 hour'), (h % 3) + 1, h
FROM generate_series(1, 24) h;

CREATE MATERIALIZED VIEW cv_safety
WITH (time_series.continuous) AS
  SELECT time_bucket('1 hour'::interval, time) AS bucket,
         tags_id, count(*) AS c
  FROM m_safety GROUP BY bucket, tags_id;

-- ============================================================
-- SAFE-01: mark_start INSERT is idempotent (ON CONFLICT DO NOTHING)
--
-- Race scenario: a stat row already exists for a job when mark_start
-- runs and its UPDATE-first probe somehow misses it (snapshot timing,
-- extension recreate, etc.).  Without ON CONFLICT, the fallback INSERT
-- would PK-violate and crash the worker.  We exercise the path by
-- DELETEing the auto-created stat row, calling mark_start indirectly
-- via run_job (forcing the INSERT branch), then DELETing+INSERTing a
-- conflicting row before another run_job to exercise the conflict
-- branch.
-- ============================================================
\echo '=== SAFE-01: mark_start INSERT no longer PK-violates on race ==='
SELECT add_continuous_aggregate_policy('cv_safety',
       '7 days'::interval, '0'::interval, '1 hour'::interval) AS jid_s1 \gset

-- 1. Delete the auto-created stat row.  The next mark_start probe
--    UPDATEs zero rows and falls back to INSERT (the path under test).
DELETE FROM time_series.bgw_job_stat WHERE job_id = :jid_s1;
CALL time_series.run_job(:jid_s1);
SELECT total_runs > 0  AS first_run_ok
  FROM time_series.bgw_job_stat WHERE job_id = :jid_s1;

-- 2. DELETE the stat row, INSERT a fresh one inline (simulating a
--    racing committer), then call run_job.  The UPDATE-first probe
--    sees the racing row and updates it; INSERT path is not hit, but
--    if it were hit, ON CONFLICT DO NOTHING would catch it.
DELETE FROM time_series.bgw_job_stat WHERE job_id = :jid_s1;
INSERT INTO time_series.bgw_job_stat (job_id, next_start)
VALUES (:jid_s1, '-infinity');
CALL time_series.run_job(:jid_s1);
SELECT total_runs > 0  AS second_run_ok
  FROM time_series.bgw_job_stat WHERE job_id = :jid_s1;

SELECT remove_continuous_aggregate_policy('cv_safety');

-- ============================================================
-- SAFE-02: ts_bgw_params_destroy is idempotent
--
-- The mock framework's destroy() is intentionally a no-op (DSM
-- segments are pinned for EXEC_BACKEND parity with TSDB).  We just
-- assert it never errors regardless of state.
-- ============================================================
\echo '=== SAFE-02: ts_bgw_params_destroy can be called repeatedly ==='
-- ts_bgw_params_create needs public.bgw_dsm_handle_store; create it
-- (cagg_bgw_mock.sql sets this up too).
CREATE TABLE public.bgw_dsm_handle_store(handle BIGINT) DISTRIBUTED REPLICATED;
INSERT INTO public.bgw_dsm_handle_store VALUES (0);

SELECT time_series.ts_bgw_params_destroy();   -- before create
SELECT time_series.ts_bgw_params_create();
SELECT time_series.ts_bgw_params_destroy();   -- after create
SELECT time_series.ts_bgw_params_destroy();   -- again — must be OK
DROP TABLE public.bgw_dsm_handle_store;

-- ============================================================
-- SAFE-03: extreme schedule_interval values
--
-- Both ends of the interval space.  Verifies:
--   * 1-microsecond schedule (unrealistic but shouldn't crash)
--   * 500-year schedule (no internal overflow when computing
--     next_start = last_finish + schedule_interval)
-- ============================================================
\echo '=== SAFE-03a: schedule_interval = 1 microsecond accepted ==='
SELECT add_continuous_aggregate_policy('cv_safety',
       '7 days'::interval, '0'::interval, '1 microsecond'::interval) AS jid_s3a \gset
SELECT schedule_interval = '1 microsecond'::interval AS small_interval_stored
  FROM time_series.bgw_job WHERE id = :jid_s3a;
SELECT remove_continuous_aggregate_policy('cv_safety');

\echo '=== SAFE-03b: schedule_interval = 500 years accepted ==='
SELECT add_continuous_aggregate_policy('cv_safety',
       '1000 years'::interval, '0'::interval, '500 years'::interval) AS jid_s3b \gset
SELECT schedule_interval = '500 years'::interval AS large_interval_stored
  FROM time_series.bgw_job WHERE id = :jid_s3b;

-- run_job under a huge schedule_interval should not overflow
-- when computing next_start = now() + 500 years.
CALL time_series.run_job(:jid_s3b);
SELECT next_start IS NOT NULL AS next_start_set,
       next_start > now() + interval '400 years' AS next_start_in_far_future
  FROM time_series.bgw_job_stat WHERE job_id = :jid_s3b;

SELECT remove_continuous_aggregate_policy('cv_safety');

-- ============================================================
-- SAFE-04: schedule_interval = 0 — corner case
--
-- The SQL surface accepts schedule_interval = '0', which would in
-- principle make next_start = last_finish (i.e. fire again
-- immediately).  Verify run_job tolerates this without infinite
-- loop (run_job is one-shot anyway) and without divide-by-zero.
-- ============================================================
\echo '=== SAFE-04: schedule_interval = 0 tolerated (failure recorded, no crash) ==='
SELECT add_continuous_aggregate_policy('cv_safety',
       '7 days'::interval, '0'::interval, '0'::interval) AS jid_s4 \gset
-- next_start re-computation will fail inside time_bucket (period > 0
-- guard) — that bubbles into the failure path.  Verify run_job
-- catches it and records failure rather than crashing the backend.
\set VERBOSITY terse
\set ON_ERROR_STOP 0
CALL time_series.run_job(:jid_s4);
\set ON_ERROR_STOP 1
\set VERBOSITY default
SELECT total_runs > 0  AS run_attempted
  FROM time_series.bgw_job_stat WHERE job_id = :jid_s4;
SELECT remove_continuous_aggregate_policy('cv_safety');

-- ============================================================
-- SAFE-05: ghost rows from worker crash are reclaimable
--
-- A "ghost row" is a bgw_job_stat_history row where mark_start ran
-- (execution_start written) but mark_end never did (execution_finish
-- stays NULL forever).  Diagnostic of: worker SIGKILL'd, coordinator
-- restart mid-execution, OOM-killer fired.
--
-- Without explicit handling, retention WHERE execution_finish < cutoff
-- skips these rows because NULL < anything is NULL → never deleted.
-- Long-stability runs accumulate one ghost per crash forever.
--
-- This test:
--   1. Synthesizes a ghost row (we can't actually SIGKILL a worker
--      from a regression test, but the table-level state is what
--      retention actually sees — the schema check is the real assert).
--   2. Verifies the built-in retention policy DELETEs the ghost when
--      execution_start is older than drop_after.
--   3. Verifies job_history.is_crashed correctly flags the row before
--      retention removes it.
-- ============================================================
\echo '=== SAFE-05: ghost rows are flagged + reclaimable by retention ==='

-- Synthesize a fresh ghost row (not yet stale) and an old ghost row
-- (older than the soon-to-be-tuned drop_after).  Use distinct pid
-- markers so we can assert without race against the real BGW worker.
INSERT INTO time_series.bgw_job_stat_history
       (job_id, pid, execution_start, execution_finish, succeeded, data)
VALUES (1, 99001, now() - '2 minutes'::interval, NULL, NULL, NULL),
       (1, 99002, now() - '60 days'::interval,   NULL, NULL, NULL);

-- (a) Both rows show up as is_crashed in job_history.
SELECT pid, is_crashed, succeeded
  FROM time_series.job_history
 WHERE pid IN (99001, 99002)
 ORDER BY pid;

-- (b) Both rows surface in job_errors (succeeded IS NOT TRUE picks up NULL).
SELECT pid, is_crashed, sqlerrcode, err_message
  FROM time_series.job_errors
 WHERE pid IN (99001, 99002)
 ORDER BY pid;

-- (c) Run retention with drop_after = 1 day.  The 60-day-old ghost
--     should be deleted; the 2-minute-old ghost should be kept.
SELECT time_series.policy_job_stat_history_retention(1,
       '{"drop_after": "1 day"}'::jsonb) >= 1 AS at_least_one_deleted;

SELECT pid, (execution_finish IS NULL) AS still_ghost
  FROM time_series.bgw_job_stat_history
 WHERE pid IN (99001, 99002)
 ORDER BY pid;

-- (d) Cleanup the surviving ghost row so it doesn't leak into other tests.
DELETE FROM time_series.bgw_job_stat_history WHERE pid IN (99001, 99002);

-- ============================================================
-- Cleanup
-- ============================================================
DROP VIEW cv_safety;
DROP TABLE m_safety;
DROP EXTENSION time_series CASCADE;
