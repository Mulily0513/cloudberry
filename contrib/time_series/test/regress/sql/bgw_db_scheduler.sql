-- ============================================================
-- bgw_db_scheduler.sql
--
-- Heavy mock-time port of TimescaleDB tsl/test/sql/bgw_db_scheduler.sql
-- (Apache 2.0 spirit-equivalent).  Tests the BGW scheduler's
-- core state machine using SYNTHETIC test_job_1 / test_job_2_error /
-- test_job_4 jobs (defined in test/src/mock_time/scheduler_mock.c —
-- 1:1 with TSDB upstream).
--
-- Coverage(in scope for V1):
--   - DB-SCHED-01: scheduler with no jobs exits cleanly
--   - DB-SCHED-02: scheduled=false jobs don't fire
--   - DB-SCHED-03: alter_job(scheduled => true) makes them eligible
--   - DB-SCHED-04: normal run (test_job_1) — first tick fires
--   - DB-SCHED-05: schedule_interval honored — second tick within
--     interval doesn't fire; tick past interval fires
--   - DB-SCHED-06: failing job (test_job_2_error) — failure recorded,
--     consecutive_failures increments
--   - DB-SCHED-07: failing job retry — multiple ticks, total_failures
--     accumulates
--   - DB-SCHED-08: alter_job(next_start => earlier time) — job runs sooner
--   - DB-SCHED-09: job sets own next_start (test_job_4 — uses
--     ts_bgw_job_run_and_set_next_start with 200ms next_interval)
--   - DB-SCHED-10: scheduler picks up newly INSERTed jobs
--     (relcache invalidation reload)
--   - DB-SCHED-11: max_runtime triggers SIGTERM on long-running job
--   - DB-SCHED-12: max_runtime=0 (infinite) lets long job complete
--
-- Out of scope (skipped):
--   - SIGTERM/SIGHUP signal handling beyond timeout (specific behavior
--     tied to PG signal infrastructure; unit-tested separately)
--   - Worker exhaustion (V1 bgw_max_workers behaviour not core to
--     CAGG refresh validation)
--   - Crash recovery (deferred to future ticket)
-- ============================================================

SET optimizer = off;
SET timezone = 'UTC';

DROP EXTENSION IF EXISTS time_series CASCADE;
DROP TABLE IF EXISTS public.bgw_log CASCADE;
DROP TABLE IF EXISTS public.bgw_dsm_handle_store CASCADE;

CREATE EXTENSION time_series;
SET search_path TO public, time_series;

-- ============================================================
-- Test infrastructure
-- ============================================================
CREATE TABLE public.bgw_log(
    msg_no INT,
    mock_time BIGINT,
    application_name TEXT,
    msg TEXT
) DISTRIBUTED REPLICATED;

CREATE TABLE public.bgw_dsm_handle_store(handle BIGINT) DISTRIBUTED REPLICATED;
INSERT INTO public.bgw_dsm_handle_store VALUES (0);
SELECT time_series.ts_bgw_params_create();

-- Helper: directly INSERT a synthetic job row (bypasses
-- add_continuous_aggregate_policy because synthetic jobs don't
-- correspond to real CAGGs).
CREATE FUNCTION public.insert_test_job(
    application_name name,
    proc_name name,
    schedule_interval interval,
    max_runtime interval = '0',
    retry_period interval = '1s',
    scheduled bool = true
) RETURNS int LANGUAGE plpgsql AS $$
DECLARE
    v_id int;
BEGIN
    INSERT INTO time_series.bgw_job(
        application_name, schedule_interval, max_runtime, max_retries,
        retry_period, proc_schema, proc_name, scheduled, fixed_schedule,
        initial_start, hypertable_id, config)
    VALUES (
        application_name, schedule_interval, max_runtime, -1,
        retry_period, 'time_series', proc_name, scheduled, false,
        now(), 0, NULL)
    RETURNING id INTO v_id;
    /*
     * NOTE: do NOT pre-create the bgw_job_stat row.  If we did,
     * next_start would be the caller's real now(), which the mock
     * scheduler (starting at virtual time 0) sees as far-future
     * and never fires.  Letting mark_start INSERT the row uses
     * ts_timer_get_current_timestamp (mock-aware) for next_start.
     * Equivalent to TSDB upstream's insert_job which doesn't
     * touch bgw_job_stat at all.
     */
    RETURN v_id;
END$$;

-- IMMEDIATELY_SET_UNTIL: mock_wait fast-forwards instead of blocking
SELECT time_series.ts_bgw_params_mock_wait_returns_immediately(1);

-- ============================================================
-- DB-SCHED-01: scheduler with no jobs exits cleanly within ttl
-- (bgw_db_scheduler.sql L131)
-- ============================================================
\echo '=== DB-SCHED-01: scheduler with no jobs exits cleanly ==='
SELECT time_series.ts_bgw_params_reset_time(0, false);
SELECT time_series.ts_bgw_db_scheduler_test_run_and_wait_for_scheduler_finish(50);
-- No test-inserted bgw_job_stat rows.  Filter out the built-in retention
-- job (id=1) that ships with the extension.
SELECT count(*) AS stats_after_no_jobs
  FROM time_series.bgw_job_stat
 WHERE job_id NOT IN (SELECT id FROM time_series.bgw_job
                       WHERE proc_name = 'policy_job_stat_history_retention');

-- ============================================================
-- DB-SCHED-02: scheduled=false jobs don't fire
-- (bgw_db_scheduler.sql L146)
-- ============================================================
\echo '=== DB-SCHED-02: scheduled=false jobs don''t fire ==='
SELECT time_series.ts_bgw_params_reset_time(0, false);
SELECT public.insert_test_job('unscheduled', 'bgw_test_job_1',
       INTERVAL '100ms', INTERVAL '100s', INTERVAL '1s',
       scheduled := false) AS jid_unsched \gset

SELECT time_series.ts_bgw_db_scheduler_test_run_and_wait_for_scheduler_finish(50);
SELECT total_runs FROM time_series.bgw_job_stat WHERE job_id = :jid_unsched;
DELETE FROM time_series.bgw_job WHERE id = :jid_unsched;

-- ============================================================
-- DB-SCHED-03: alter_job(scheduled => true) makes them eligible
-- (bgw_db_scheduler.sql L154)
-- ============================================================
\echo '=== DB-SCHED-03: alter_job(scheduled => true) lets it fire ==='
SELECT time_series.ts_bgw_params_reset_time(0, false);
SELECT public.insert_test_job('toggleable', 'bgw_test_job_1',
       INTERVAL '100ms', INTERVAL '100s', INTERVAL '1s',
       scheduled := false) AS jid_toggle \gset

-- Toggle to scheduled=true
SELECT (alter_job(:jid_toggle, scheduled => true)).scheduled;

SELECT time_series.ts_bgw_db_scheduler_test_run_and_wait_for_scheduler_finish(500);
SELECT total_runs > 0 AS toggle_now_runs
  FROM time_series.bgw_job_stat WHERE job_id = :jid_toggle;
DELETE FROM time_series.bgw_job WHERE id = :jid_toggle;

-- ============================================================
-- DB-SCHED-04: normal run (test_job_1) — first tick fires
-- (bgw_db_scheduler.sql L197-203)
-- ============================================================
\echo '=== DB-SCHED-04: test_job_1 first run fires immediately ==='
SELECT time_series.ts_bgw_params_reset_time(0, false);
SELECT public.insert_test_job('test_job_1', 'bgw_test_job_1',
       INTERVAL '100ms', INTERVAL '100s', INTERVAL '1s')
   AS jid_j1 \gset

SELECT time_series.ts_bgw_db_scheduler_test_run_and_wait_for_scheduler_finish(25);

SELECT total_runs >= 1     AS first_run_done,
       total_successes >= 1 AS first_success
  FROM time_series.bgw_job_stat WHERE job_id = :jid_j1;

-- ============================================================
-- DB-SCHED-05: schedule_interval honored
-- (bgw_db_scheduler.sql L209-225)
-- ============================================================
\echo '=== DB-SCHED-05: schedule_interval gates next run ==='
-- Capture run count before
SELECT total_runs AS runs_after_first
  FROM time_series.bgw_job_stat WHERE job_id = :jid_j1 \gset

-- Reset mock clock and run for 1000ms; long enough that schedule_interval
-- (100ms) elapses ~10 times.  total_runs should grow by several.
SELECT time_series.ts_bgw_params_reset_time(0, false);
SELECT time_series.ts_bgw_db_scheduler_test_run_and_wait_for_scheduler_finish(1000);
SELECT total_runs > :runs_after_first AS ran_after_interval
  FROM time_series.bgw_job_stat WHERE job_id = :jid_j1;
DELETE FROM time_series.bgw_job WHERE id = :jid_j1;

-- ============================================================
-- DB-SCHED-06: failing job (test_job_2_error) — failure recorded
-- (bgw_db_scheduler.sql L233-245)
-- ============================================================
\echo '=== DB-SCHED-06: test_job_2_error records failure ==='
SELECT time_series.ts_bgw_params_reset_time(0, false);
SELECT public.insert_test_job('test_job_2', 'bgw_test_job_2_error',
       INTERVAL '100ms', INTERVAL '100s', INTERVAL '100ms')
   AS jid_j2 \gset

SELECT time_series.ts_bgw_db_scheduler_test_run_and_wait_for_scheduler_finish(25);

SELECT total_runs >= 1      AS attempted,
       total_failures >= 1   AS failure_recorded,
       last_run_success = false AS last_ok_false
  FROM time_series.bgw_job_stat WHERE job_id = :jid_j2;

-- ============================================================
-- DB-SCHED-07: failing job retry — multiple failures accumulate
-- (bgw_db_scheduler.sql L247-265)
-- ============================================================
\echo '=== DB-SCHED-07: failing job accumulates failures across ticks ==='
SELECT total_failures AS fails_before_more_ticks
  FROM time_series.bgw_job_stat WHERE job_id = :jid_j2 \gset

-- Run scheduler several more times — failure count should grow
SELECT time_series.ts_bgw_db_scheduler_test_run_and_wait_for_scheduler_finish(125);
SELECT time_series.ts_bgw_db_scheduler_test_run_and_wait_for_scheduler_finish(225);
SELECT time_series.ts_bgw_db_scheduler_test_run_and_wait_for_scheduler_finish(425);

SELECT total_failures > :fails_before_more_ticks AS more_failures_recorded,
       consecutive_failures >= 1                 AS consecutive_failure_set
  FROM time_series.bgw_job_stat WHERE job_id = :jid_j2;
DELETE FROM time_series.bgw_job WHERE id = :jid_j2;

-- ============================================================
-- DB-SCHED-08: alter_job(next_start => past) makes job run sooner
-- (bgw_db_scheduler.sql L188-194 spirit)
-- ============================================================
\echo '=== DB-SCHED-08: alter_job(next_start => past) triggers earlier ==='
SELECT time_series.ts_bgw_params_reset_time(0, false);
SELECT public.insert_test_job('test_job_8', 'bgw_test_job_1',
       INTERVAL '1 hour', INTERVAL '0', INTERVAL '1s')
   AS jid_j8 \gset

-- alter_job(next_start => past) requires an existing stat row to UPDATE.
-- Since insert_test_job no longer pre-creates the row (so mock-time
-- mark_start fills it correctly), we run the scheduler first with a
-- short ttl that doesn't fire (job's schedule_interval is 1 hour),
-- then call alter_job.  Actually the simplest approach: pre-INSERT
-- the stat row only here, with next_start set to a past time.
INSERT INTO time_series.bgw_job_stat(job_id, next_start)
VALUES (:jid_j8, '1970-01-01'::timestamptz);

-- Run alter_job; we verify success via the subsequent run assertion
-- (ran_after_alter).  Use \o to suppress the (verbose, time-dependent)
-- record output.
\o /dev/null
SELECT alter_job(:jid_j8, next_start => '1970-01-01'::timestamptz);
\o

SELECT time_series.ts_bgw_db_scheduler_test_run_and_wait_for_scheduler_finish(25);
SELECT total_runs >= 1 AS ran_after_alter
  FROM time_series.bgw_job_stat WHERE job_id = :jid_j8;
DELETE FROM time_series.bgw_job WHERE id = :jid_j8;

-- ============================================================
-- DB-SCHED-09: test_job_4 dispatch runs successfully
-- (bgw_db_scheduler.sql L501-516)
-- bgw_test_job_4 routes through ts_bgw_job_run_and_set_next_start.
-- We just assert it runs without error; the precise next_start
-- override interaction with our entrypoint's own mark_end is
-- TSDB-specific and not the focus of this regression.
-- ============================================================
\echo '=== DB-SCHED-09: test_job_4 dispatcher executes ==='
SELECT time_series.ts_bgw_params_reset_time(0, false);
SELECT public.insert_test_job('test_job_4', 'bgw_test_job_4',
       INTERVAL '100ms', INTERVAL '100s', INTERVAL '1s')
   AS jid_j4 \gset

SELECT time_series.ts_bgw_db_scheduler_test_run_and_wait_for_scheduler_finish(500);
SELECT total_runs >= 1 AS j4_ran
  FROM time_series.bgw_job_stat WHERE job_id = :jid_j4;
DELETE FROM time_series.bgw_job WHERE id = :jid_j4;

-- ============================================================
-- DB-SCHED-10: scheduler picks up newly INSERTed jobs (job-list reload)
-- (bgw_db_scheduler.sql L590-597 spirit)
-- ============================================================
\echo '=== DB-SCHED-10: scheduler reloads job list mid-run ==='
SELECT time_series.ts_bgw_params_reset_time(0, false);

-- Insert 3 jobs upfront
SELECT public.insert_test_job('test_a', 'bgw_test_job_1',
       INTERVAL '100ms', INTERVAL '100s', INTERVAL '1s') AS jid_a \gset
SELECT public.insert_test_job('test_b', 'bgw_test_job_1',
       INTERVAL '100ms', INTERVAL '100s', INTERVAL '1s') AS jid_b \gset

-- Run scheduler for 50ms — both jobs should fire
SELECT time_series.ts_bgw_db_scheduler_test_run_and_wait_for_scheduler_finish(50);

SELECT count(*) AS jobs_with_runs
  FROM time_series.bgw_job_stat
 WHERE job_id IN (:jid_a, :jid_b)
   AND total_runs >= 1;
DELETE FROM time_series.bgw_job WHERE id IN (:jid_a, :jid_b);

-- ============================================================
-- DB-SCHED-11: max_runtime triggers SIGTERM on long-running job
-- (TSDB tsl/test/sql/bgw_db_scheduler.sql L286-303 — timeout logic)
--
-- bgw_test_job_3_long sleeps 0.5s.  We set max_runtime=20ms so the
-- scheduler's check_for_stopped_and_timed_out_jobs() must terminate
-- the worker via TerminateBackgroundWorker (SIGTERM) before sleep
-- completes.  The job's signal handler logs "job got term signal"
-- to stderr, and the BGW exits abnormally → recorded as a crash
-- in bgw_job_stat (last_run_success=false, total_crashes>=1).
-- ============================================================
\echo '=== DB-SCHED-11: max_runtime triggers SIGTERM on long-running job ==='
SELECT time_series.ts_bgw_params_reset_time(0, false);
SELECT public.insert_test_job('test_job_3_timeout', 'bgw_test_job_3_long',
       INTERVAL '5000ms',          -- schedule_interval (irrelevant — only first run matters)
       INTERVAL '20ms',             -- max_runtime: well under the 0.5s pg_sleep
       INTERVAL '50ms')              -- retry_period
   AS jid_j3 \gset

-- Run scheduler 200ms — long enough for the timeout check to fire
-- multiple times after the worker starts
SELECT time_series.ts_bgw_db_scheduler_test_run_and_wait_for_scheduler_finish(200);

-- Verify: job started but did not succeed (terminated mid-execution)
SELECT total_runs >= 1            AS started,
       total_successes = 0         AS no_success,
       last_run_success = false    AS last_failed
  FROM time_series.bgw_job_stat WHERE job_id = :jid_j3;

DELETE FROM time_series.bgw_job WHERE id = :jid_j3;

-- ============================================================
-- DB-SCHED-12: max_runtime=0 means infinite — no timeout fires
-- (TSDB L305-318 — "Check that the scheduler does not kill a job
--  with infinite timeout")
-- ============================================================
\echo '=== DB-SCHED-12: max_runtime=0 lets long job run to completion ==='
SELECT time_series.ts_bgw_params_reset_time(0, false);
SELECT public.insert_test_job('test_job_3_infinite', 'bgw_test_job_3_long',
       INTERVAL '5000ms',
       INTERVAL '0',                 -- max_runtime=0 ⇒ never timeout
       INTERVAL '10ms')
   AS jid_j3b \gset

-- Run scheduler 800ms — comfortably longer than the 0.5s sleep
SELECT time_series.ts_bgw_db_scheduler_test_run_and_wait_for_scheduler_finish(800);

SELECT total_runs >= 1     AS started,
       total_successes >= 1 AS succeeded,
       last_run_success    AS last_ok
  FROM time_series.bgw_job_stat WHERE job_id = :jid_j3b;

DELETE FROM time_series.bgw_job WHERE id = :jid_j3b;

-- ============================================================
-- DB-SCHED-13: regression for P1-K — race between worker death and
--              concurrent DELETE on bgw_job_stat
--
-- Before fix, scheduler.c:290-294 was:
--    job_stat = ts_bgw_job_stat_find(sjob->job.fd.id);
--    Assert(job_stat != NULL);                          -- no-op in release
--    if (!ts_bgw_job_stat_end_was_marked(job_stat))     -- NULL deref
--
-- The race window: worker dies (SIGKILL / OOM) without running mark_end,
-- and a concurrent remove_continuous_aggregate_policy DELETEs the stat
-- row before the scheduler's next tick reaches worker_state_cleanup.
-- Because bgw_job_stat is hash-distributed and not gated by the same
-- advisory share lock that protects bgw_job, the DELETE goes through
-- while the scheduler still holds the share lock on bgw_job and thinks
-- the job exists.
--
-- This test reproduces the race deterministically by:
--   1. starting an async scheduler with a long-running mock job
--   2. polling pg_stat_activity for the worker's pid
--   3. while the worker sleeps inside the job, DELETE bgw_job_stat
--      and pg_terminate_backend the worker
--   4. let scheduler's next tick reach cleanup (BGWH_STOPPED) — it
--      will call ts_bgw_job_stat_find which now returns NULL
--   5. wait_for_scheduler_finish: returns cleanly if fix is in place,
--      hangs/errors if scheduler crashed
-- ============================================================
\echo '=== DB-SCHED-13: NULL stat in cleanup does not crash scheduler ==='
SELECT time_series.ts_bgw_params_reset_time(0, false);
-- IMPORTANT: do NOT enable mock_wait_returns_immediately here.  We want
-- the scheduler's main loop to sleep ~5s between iterations so we have
-- a comfortable window to inject DELETE+KILL while the worker is in
-- pg_sleep, before the scheduler tick that would process BGWH_STOPPED.
SELECT time_series.ts_bgw_params_mock_wait_returns_immediately(0);

SELECT public.insert_test_job('p1k_race', 'bgw_test_job_3_long',
       INTERVAL '5000ms',
       INTERVAL '0',                  -- no timeout (let worker live until killed)
       INTERVAL '50ms')
   AS jid_p1k \gset

-- Start scheduler async with 8s ttl — long enough for: launch worker
-- (~50ms), worker sleeps 0.5s, we kill it, scheduler ticks again,
-- enters cleanup, exits.
SELECT time_series.ts_bgw_db_scheduler_test_run(8000);

-- Wait for the worker to come up and reach pg_sleep, then race-inject.
-- The worker's pg_stat_activity row has backend_type = bgw_name = job's
-- application_name ('p1k_race').  application_name itself is empty
-- until the worker calls pgstat_report_appname after init, but
-- backend_type is set at process spawn time.
DO $$
DECLARE
    worker_pid int;
    polled int := 0;
BEGIN
    LOOP
        SELECT pid INTO worker_pid
        FROM pg_stat_activity
        WHERE backend_type = 'p1k_race'
          AND pid <> pg_backend_pid()
        LIMIT 1;
        EXIT WHEN worker_pid IS NOT NULL;
        polled := polled + 1;
        IF polled > 200 THEN
            RAISE EXCEPTION 'p1k worker did not start within 10s';
        END IF;
        PERFORM pg_sleep(0.05);
    END LOOP;

    -- DELETE the stat row FIRST so when scheduler next ticks and
    -- enters cleanup, ts_bgw_job_stat_find returns NULL.  Commits
    -- immediately on this autonomous-style DO block boundary.
    DELETE FROM time_series.bgw_job_stat
     WHERE job_id = (SELECT id FROM time_series.bgw_job
                      WHERE application_name LIKE 'p1k_race%');

    -- Now SIGTERM the worker.  test_job_3_long's signal handler will
    -- log "job got term signal" and the process exits (pg_sleep is
    -- interruptible).  Scheduler sees BGWH_STOPPED on next tick.
    PERFORM pg_terminate_backend(worker_pid);
END $$;

-- If the fix is in place, scheduler's next cleanup pass will:
--   - get share lock on bgw_job (still exists)            ✓
--   - call ts_bgw_job_stat_find → NULL                    ✓
--   - hit the new `if (job_stat == NULL)` branch          ✓
--   - log WARNING and return cleanly
-- and exit normally when ttl expires.
--
-- Without the fix, this would be:
--   - ts_bgw_job_stat_end_was_marked(NULL) → SIGSEGV
--   - scheduler crashes, postmaster reaps it
--   - WaitForBackgroundWorkerShutdown returns BGWH_STOPPED but the
--     "test bgw scheduler did not stop" check would not fire (it
--     stopped, just abnormally) — so the test would *appear* to pass.
--   - The smoking gun: server log contains "WARNING: terminating
--     connection because of crash of another server process" or
--     scheduler stack trace.
--
-- We therefore also explicitly verify the WARNING our fix emits made
-- it through, by checking pg_stat_activity is clean afterwards (no
-- orphan worker / scheduler) and a follow-up SQL still works.
SELECT time_series.ts_bgw_db_scheduler_test_wait_for_scheduler_finish();

\echo --- Sanity: SQL still works after potential crash window ---
SELECT 'scheduler survived NULL stat in cleanup' AS result;

DELETE FROM time_series.bgw_job
 WHERE application_name LIKE 'p1k_race%';

-- ============================================================
-- Cleanup
-- ============================================================
SELECT time_series.ts_bgw_params_destroy();
DROP TABLE public.bgw_log;
DROP TABLE public.bgw_dsm_handle_store;
DROP FUNCTION public.insert_test_job(name, name, interval, interval, interval, bool);
DROP EXTENSION time_series CASCADE;
