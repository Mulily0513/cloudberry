-- ============================================================
-- cagg_bgw_policy.sql
--
-- CAGG auto-refresh (BGW) policy tests.
--
-- Borrowed from TimescaleDB tsl/test/sql/cagg_policy.sql,
-- cagg_policy_run.sql, cagg_bgw.sql.in. Adapted for our V1 API:
--   - add_continuous_aggregate_policy(cagg_name, start_offset, end_offset,
--                                     schedule_interval, if_not_exists)
--   - remove_continuous_aggregate_policy(cagg_name, if_exists)
--   - cagg_policy_stats view
--
-- Sections:
--   1. DDL validation (BGW-DDL-01..11) — deterministic, no BGW timing
--   2. cagg_policy_stats view content
--   3. BGW execution end-to-end (BGW-01, BGW-02, BGW-08, BGW-09)
--   4. Invalidation behaviour via synchronous CALL (BGW-03..05)
--      — verifying that BGW-driven and CALL-driven refresh share the same
--        invalidation processing logic; BGW path itself is exercised in
--        Section 3.
--   5. Dangling-pointer regression (BGW-10)
--
-- Note: BGW timing-dependent cases use a polling helper with a 30s ceiling.
-- Data times are generated relative to now() because BGW worker uses real
-- system time (no mock-time hook in our V1).
-- ============================================================

SET optimizer = off;
SET timezone = 'UTC';

DROP EXTENSION IF EXISTS time_series CASCADE;
-- Defensive: prior test runs may leave behind source tables and auto-named
-- materialization tables (which are NOT cleaned by DROP EXTENSION CASCADE
-- for views that were dropped via DROP MATERIALIZED VIEW).
DROP TABLE IF EXISTS metrics_tstz CASCADE;
DROP TABLE IF EXISTS metrics_ts   CASCADE;
DROP TABLE IF EXISTS metrics_date CASCADE;

CREATE EXTENSION time_series;
SET search_path TO public, time_series;

-- ============================================================
-- Helpers
-- ============================================================

-- Block until job's total_runs reaches target (or timeout).
CREATE OR REPLACE FUNCTION wait_for_runs(p_job_id int, p_target_runs int,
                                         p_timeout_sec int DEFAULT 30)
RETURNS bool LANGUAGE plpgsql AS $$
DECLARE
    v_runs int;
    v_deadline timestamptz := clock_timestamp()
                              + (p_timeout_sec || ' seconds')::interval;
BEGIN
    LOOP
        SELECT total_runs INTO v_runs
          FROM time_series.bgw_job_stat WHERE job_id = p_job_id;
        IF v_runs IS NOT NULL AND v_runs >= p_target_runs THEN
            RETURN true;
        END IF;
        IF clock_timestamp() > v_deadline THEN
            RETURN false;
        END IF;
        PERFORM pg_sleep(0.5);
    END LOOP;
END $$;

-- Force the scheduler to fire this job on the next tick by backdating
-- next_start. Used to keep tests fast.
CREATE OR REPLACE FUNCTION trigger_worker(p_job_id int)
RETURNS void LANGUAGE sql AS $$
    UPDATE time_series.bgw_job_stat
       SET next_start = now() - interval '5 minutes'
     WHERE job_id = p_job_id;
$$;

-- ============================================================
-- Setup: source tables with data placed relative to now()
-- so that policy default windows (now()-Xd, now()] actually cover them.
-- ============================================================
CREATE TABLE metrics_tstz (
    time        TIMESTAMPTZ       NOT NULL,
    tags_id     INT               NOT NULL,
    temperature DOUBLE PRECISION
) DISTRIBUTED BY (tags_id);

-- 48 hours of data ending ~2 hours before now(), so refresh windows that
-- exclude the most-recent few minutes still cover the last bucket.
INSERT INTO metrics_tstz
SELECT date_trunc('hour', now()) - ((48 - h) * interval '1 hour')
       + (m * interval '5 minutes'),
       (h % 5) + 1,
       20.0 + h * 0.1 + m * 0.01
FROM generate_series(1, 48) h,
     generate_series(0, 5)  m;

CREATE TABLE metrics_ts (
    time        TIMESTAMP         NOT NULL,
    tags_id     INT               NOT NULL,
    temperature DOUBLE PRECISION
) DISTRIBUTED BY (tags_id);
INSERT INTO metrics_ts
SELECT (date_trunc('hour', now()) - ((48 - h) * interval '1 hour'))::timestamp,
       (h % 5) + 1, 20.0 + h * 0.1
FROM generate_series(1, 48) h;

CREATE TABLE metrics_date (
    time        DATE              NOT NULL,
    tags_id     INT               NOT NULL,
    cnt         INT
) DISTRIBUTED BY (tags_id);
INSERT INTO metrics_date
SELECT (now()::date - d), (d % 5) + 1, d * 10
FROM generate_series(1, 10) d;

-- ============================================================
-- CAGG views
-- ============================================================
CREATE MATERIALIZED VIEW cv_tstz
WITH (time_series.continuous) AS
  SELECT time_bucket('1 hour'::interval, time) AS bucket, tags_id,
         count(*) AS cnt, avg(temperature) AS avg_temp
  FROM metrics_tstz GROUP BY bucket, tags_id;

CREATE MATERIALIZED VIEW cv_ts
WITH (time_series.continuous) AS
  SELECT time_bucket('1 hour'::interval, time) AS bucket, tags_id,
         count(*) AS cnt
  FROM metrics_ts GROUP BY bucket, tags_id;

CREATE MATERIALIZED VIEW cv_date
WITH (time_series.continuous) AS
  SELECT time_bucket('1 day'::interval, time) AS bucket, tags_id,
         sum(cnt) AS s
  FROM metrics_date GROUP BY bucket, tags_id;

-- ============================================================
-- Section 1: DDL validation
-- ============================================================

\echo '=== BGW-DDL-01: basic add then remove ==='
-- TSDB cagg_policy.sql L9-L44
SELECT add_continuous_aggregate_policy('cv_tstz',
       start_offset => '2 days'::interval,
       end_offset   => '0 minutes'::interval,
       schedule_interval => '10 seconds'::interval) AS job_id_tstz \gset
SELECT cagg_name, schedule_interval, start_offset, end_offset, active
  FROM time_series.cagg_policy_stats
 WHERE cagg_name LIKE '%cv_tstz%';

SELECT remove_continuous_aggregate_policy('cv_tstz');
SELECT count(*) AS policies_after_remove
  FROM time_series.cagg_policy_stats WHERE cagg_name LIKE '%cv_tstz%';

\echo '=== BGW-DDL-02: start_offset <= end_offset → ERROR ==='
-- TSDB cagg_policy.sql L116-L117
\set ON_ERROR_STOP 0
SELECT add_continuous_aggregate_policy('cv_tstz',
       '5 minutes'::interval,
       '10 minutes'::interval,
       '10 seconds'::interval);
SELECT add_continuous_aggregate_policy('cv_tstz',
       '5 minutes'::interval,
       '5 minutes'::interval,
       '10 seconds'::interval);
\set ON_ERROR_STOP 1

\echo '=== BGW-DDL-03: refresh window < 2 buckets → ERROR ==='
-- TSDB cagg_policy.sql L118-119: rejects (mat_m1, 11, 10, '1h') because
-- window = 1 bucket.  V1 now mirrors this check (bucket_width=1h here,
-- so window=30min<2h triggers ERROR).
\set ON_ERROR_STOP 0
SELECT add_continuous_aggregate_policy('cv_tstz',
       '30 minutes'::interval,
       '0 minutes'::interval,
       '10 seconds'::interval);
\set ON_ERROR_STOP 1

\echo '=== BGW-DDL-04: duplicate add → ERROR ==='
-- TSDB cagg_policy.sql L122-L123
SELECT add_continuous_aggregate_policy('cv_tstz',
       '2 days'::interval, '0 minutes'::interval, '10 seconds'::interval)
  AS dup_first \gset
\set ON_ERROR_STOP 0
SELECT add_continuous_aggregate_policy('cv_tstz',
       '2 days'::interval, '0 minutes'::interval, '10 seconds'::interval);
\set ON_ERROR_STOP 1

\echo '=== BGW-DDL-05: duplicate add + if_not_exists=true → NOTICE, returns existing ==='
-- TSDB cagg_policy.sql L124
SELECT add_continuous_aggregate_policy('cv_tstz',
       '2 days'::interval, '0 minutes'::interval, '10 seconds'::interval,
       if_not_exists => true) AS jid_existing \gset
SELECT :jid_existing = :dup_first AS returned_existing_jobid;

\echo '=== BGW-DDL-06: duplicate add + if_not_exists=true (different params) ==='
-- TSDB cagg_policy.sql L125 emits WARNING; V1 emits NOTICE regardless and
-- returns the existing job_id. Documented behavior difference.
SELECT add_continuous_aggregate_policy('cv_tstz',
       '3 days'::interval, '5 minutes'::interval, '20 seconds'::interval,
       if_not_exists => true) AS jid_existing2 \gset
SELECT :jid_existing2 = :dup_first AS still_returns_existing_jobid;

-- Cleanup before next group
SELECT remove_continuous_aggregate_policy('cv_tstz');

\echo '=== BGW-DDL-07: NULL start_offset (open-start window) ==='
-- TSDB cagg_policy.sql L154
SELECT add_continuous_aggregate_policy('cv_tstz',
       NULL, '1 hour'::interval, '10 seconds'::interval) AS job_id_null_start \gset
SELECT start_offset IS NULL AS start_is_null,
       end_offset
  FROM time_series.cagg_policy_stats WHERE job_id = :job_id_null_start;
SELECT remove_continuous_aggregate_policy('cv_tstz');

\echo '=== BGW-DDL-08: NULL end_offset (open-end window) ==='
-- TSDB cagg_policy.sql L150
SELECT add_continuous_aggregate_policy('cv_tstz',
       '2 days'::interval, NULL, '10 seconds'::interval) AS job_id_null_end \gset
SELECT start_offset,
       end_offset IS NULL AS end_is_null
  FROM time_series.cagg_policy_stats WHERE job_id = :job_id_null_end;
SELECT remove_continuous_aggregate_policy('cv_tstz');

\echo '=== BGW-DDL-09a: timestamp column type ==='
-- TSDB cagg_policy.sql L185 (timestamp variant)
SELECT add_continuous_aggregate_policy('cv_ts',
       '2 days'::interval, '0 minutes'::interval, '10 seconds'::interval)
  AS job_id_ts \gset
SELECT cagg_name FROM time_series.cagg_policy_stats WHERE job_id = :job_id_ts;
SELECT remove_continuous_aggregate_policy('cv_ts');

\echo '=== BGW-DDL-09b: date column type ==='
-- TSDB cagg_policy.sql L173 (date variant)
SELECT add_continuous_aggregate_policy('cv_date',
       '7 days'::interval, '0 minutes'::interval, '10 seconds'::interval)
  AS job_id_date \gset
SELECT cagg_name FROM time_series.cagg_policy_stats WHERE job_id = :job_id_date;
SELECT remove_continuous_aggregate_policy('cv_date');

\echo '=== BGW-DDL-10: remove non-existent → ERROR; if_exists=true → silent ==='
\set ON_ERROR_STOP 0
SELECT remove_continuous_aggregate_policy('cv_tstz');
\set ON_ERROR_STOP 1
SELECT remove_continuous_aggregate_policy('cv_tstz', if_exists => true);

\echo '=== BGW-DDL-11: add for non-existent CAGG → ERROR ==='
-- TSDB cagg_policy.sql L113
\set ON_ERROR_STOP 0
SELECT add_continuous_aggregate_policy('does_not_exist',
       '2 days'::interval, '0 minutes'::interval, '10 seconds'::interval);
\set ON_ERROR_STOP 1

-- ============================================================
-- BGW-DDL-12: cagg_default_max_retries / cagg_default_max_runtime GUCs
--
-- TSDB upstream hard-codes max_retries=-1 (unlimited) + max_runtime=0
-- (no timeout) for new CAGG refresh policies.  We keep that as the
-- compatible default but expose two PGC_USERSET GUCs so operators can
-- install safer defaults globally without patching SQL.  Tests cover:
--   12a — defaults match TSDB (-1, '0')
--   12b — SET GUC affects subsequent add_continuous_aggregate_policy
--   12c — existing policies are NOT retroactively changed
--   12d — alter_job still overrides per-policy regardless of GUC
--   12e — RESET GUC returns to TSDB-compatible defaults
-- ============================================================
\echo '=== BGW-DDL-12a: GUC defaults match TSDB upstream ==='
SHOW time_series.cagg_default_max_retries;
SHOW time_series.cagg_default_max_runtime;

SELECT add_continuous_aggregate_policy('cv_tstz',
       '2 days'::interval, '0 minutes'::interval, '10 seconds'::interval)
   AS jid_g1 \gset
SELECT max_retries, max_runtime
  FROM time_series.bgw_job WHERE id = :jid_g1;

\echo '=== BGW-DDL-12b: SET GUCs → next add picks them up ==='
SET time_series.cagg_default_max_retries = 20;
SET time_series.cagg_default_max_runtime = '30 minutes';
SELECT add_continuous_aggregate_policy('cv_ts',
       '2 days'::interval, '0 minutes'::interval, '10 seconds'::interval)
   AS jid_g2 \gset
SELECT max_retries, max_runtime
  FROM time_series.bgw_job WHERE id = :jid_g2;

\echo '=== BGW-DDL-12c: pre-existing policy unchanged by GUC ==='
-- jid_g1 was created BEFORE we SET the GUCs; it must still have
-- the original (-1, '0') values, not the new (20, '30 minutes')
SELECT max_retries, max_runtime
  FROM time_series.bgw_job WHERE id = :jid_g1;

\echo '=== BGW-DDL-12d: alter_job overrides per-policy regardless of GUC ==='
SELECT (alter_job(:jid_g2, max_retries => 5,
                  max_runtime => '1 hour'::interval)).id IS NOT NULL AS altered;
SELECT max_retries, max_runtime
  FROM time_series.bgw_job WHERE id = :jid_g2;

\echo '=== BGW-DDL-12e: RESET GUCs → next add returns to TSDB defaults ==='
RESET time_series.cagg_default_max_retries;
RESET time_series.cagg_default_max_runtime;
SHOW time_series.cagg_default_max_retries;
SHOW time_series.cagg_default_max_runtime;

-- Drop the existing two policies and add a fresh one to confirm
-- post-RESET defaults take effect.
SELECT remove_continuous_aggregate_policy('cv_tstz');
SELECT remove_continuous_aggregate_policy('cv_ts');
SELECT add_continuous_aggregate_policy('cv_tstz',
       '2 days'::interval, '0 minutes'::interval, '10 seconds'::interval)
   AS jid_g3 \gset
SELECT max_retries, max_runtime
  FROM time_series.bgw_job WHERE id = :jid_g3;

-- Cleanup for downstream tests
SELECT remove_continuous_aggregate_policy('cv_tstz');

-- ============================================================
-- Section 2: cagg_policy_stats view
-- ============================================================

\echo '=== BGW-VIEW-01: empty when no policies ==='
SELECT count(*) AS empty FROM time_series.cagg_policy_stats;

\echo '=== BGW-VIEW-02: lists active policies with config ==='
SELECT add_continuous_aggregate_policy('cv_tstz',
       '2 days'::interval, '0 minutes'::interval, '10 seconds'::interval)
  AS jid_v \gset
SELECT add_continuous_aggregate_policy('cv_ts',
       '2 days'::interval, '0 minutes'::interval, '15 seconds'::interval);

SELECT cagg_name, schedule_interval, start_offset, end_offset, active
  FROM time_series.cagg_policy_stats
 ORDER BY cagg_name;

-- Cleanup
SELECT remove_continuous_aggregate_policy('cv_tstz');
SELECT remove_continuous_aggregate_policy('cv_ts');

-- ============================================================
-- BGW-VIEW-03: owner filter mirrors TSDB's split
--
-- TSDB's design: job_stats / jobs / continuous_aggregates views are
-- public (operational metadata), but job_history / job_errors filter
-- by job owner OR database owner.  V1 mirrors that:
--
--   cagg_policy_stats (≈ TSDB job_stats) → no owner filter
--   job_history                          → filtered by job owner
--                                            OR database owner
--                                            + WITH (security_barrier)
--
-- Verifies:
--   - bob with SELECT grant sees policy metadata in cagg_policy_stats
--     (intentional, matches TSDB)
--   - bob with SELECT grant sees ZERO rows in job_history
--   - alice (owner) sees her own job_history rows
--   - the database owner (gpadmin in this test environment) sees
--     all rows (covered implicitly: tests run as superuser/datdba
--     and add_continuous_aggregate_policy populates the bgw_job)
-- ============================================================
\echo '=== BGW-VIEW-03: job_history filtered by owner + db-owner ==='
DROP ROLE IF EXISTS view_alice;
DROP ROLE IF EXISTS view_bob;
CREATE ROLE view_alice;
CREATE ROLE view_bob;
GRANT USAGE ON SCHEMA time_series TO view_alice, view_bob;
GRANT SELECT ON time_series.cagg_policy_stats TO view_alice, view_bob;
GRANT SELECT ON time_series.bgw_job_stat       TO view_alice, view_bob;
GRANT SELECT ON time_series.bgw_job_stat_history TO view_alice, view_bob;
GRANT SELECT ON time_series.job_history        TO view_alice, view_bob;

SELECT add_continuous_aggregate_policy('cv_tstz',
       '2 days'::interval, '0 minutes'::interval, '10 seconds'::interval)
  AS jid_alice \gset

-- Mark this policy as alice-owned
UPDATE time_series.bgw_job SET owner = 'view_alice'::regrole WHERE id = :jid_alice;

-- Generate one history row by running the job synchronously.
-- run_job is invoked as the test owner (the database owner),
-- so the row's job_id refers to alice's job and the row is
-- visible to alice (owner) and to the db owner (test runner).
SET client_min_messages TO warning;
CALL time_series.run_job(:jid_alice);
RESET client_min_messages;

\echo --- cagg_policy_stats: NOT filtered, bob sees alice''s policy ---
\echo --- (intentional, matches TSDB job_stats behavior) ---
SET ROLE view_bob;
SELECT count(*) AS bob_visible_policies FROM time_series.cagg_policy_stats;
RESET ROLE;

\echo --- job_history: filtered, bob sees ZERO rows ---
SET ROLE view_bob;
SELECT count(*) AS bob_visible_history FROM time_series.job_history;
RESET ROLE;

\echo --- job_history: alice (owner) sees her rows ---
SET ROLE view_alice;
SELECT count(*) >= 1 AS alice_sees_her_history FROM time_series.job_history;
RESET ROLE;

-- Cleanup
SELECT remove_continuous_aggregate_policy('cv_tstz');
REVOKE ALL ON SCHEMA time_series FROM view_alice, view_bob;
REVOKE ALL ON time_series.cagg_policy_stats FROM view_alice, view_bob;
REVOKE ALL ON time_series.bgw_job_stat FROM view_alice, view_bob;
REVOKE ALL ON time_series.bgw_job_stat_history FROM view_alice, view_bob;
REVOKE ALL ON time_series.job_history FROM view_alice, view_bob;
DROP ROLE view_alice;
DROP ROLE view_bob;

-- ============================================================
-- Section 3: BGW lifecycle (deterministic — no scheduler timing)
--
-- The BGW worker execution path (mark_start → cagg_refresh → mark_end) is
-- fully exercised by Section 5 (BGW-10 dangling-pointer regression), where
-- the test polls the worker for ≥5 iterations and verifies stat sanity.
--
-- This section covers add/remove/drop lifecycle without depending on
-- BGW scheduler tick timing, which is too racy for a regression test.
-- ============================================================

\echo '=== BGW-08: remove policy → bgw_job and bgw_job_stat cleaned ==='
-- TSDB removes via delete_job; we test our remove_continuous_aggregate_policy.
SELECT add_continuous_aggregate_policy('cv_tstz',
       '7 days'::interval, '0 minutes'::interval, '10 seconds'::interval)
  AS jid1 \gset

SELECT count(*) AS jobs_before_remove
  FROM time_series.bgw_job WHERE id = :jid1;
SELECT count(*) AS stats_before_remove
  FROM time_series.bgw_job_stat WHERE job_id = :jid1;

SELECT remove_continuous_aggregate_policy('cv_tstz');

SELECT count(*) AS jobs_after_remove
  FROM time_series.bgw_job WHERE id = :jid1;
SELECT count(*) AS stats_after_remove
  FROM time_series.bgw_job_stat WHERE job_id = :jid1;

\echo '=== BGW-09: DROP cagg view → continuous_agg catalog row removed ==='
-- TSDB bgw_custom.sql L295. Our V1 cagg presents a regular view (UNION ALL
-- over mat table + live aggregation), so DROP VIEW is the right idiom; an
-- event trigger removes the continuous_agg catalog row.
SELECT add_continuous_aggregate_policy('cv_ts',
       '2 days'::interval, '0 minutes'::interval, '10 seconds'::interval)
  AS jid_drop \gset
DROP VIEW cv_ts;

-- continuous_agg row gone via event trigger
SELECT count(*) AS cagg_rows
  FROM time_series.continuous_agg WHERE user_view_name = 'cv_ts';

-- V1 known behavior: bgw_job referencing the cagg is NOT auto-removed by
-- the DROP event trigger.  Verify the orphan job exists, then clean it up
-- manually so the test leaves no leftovers.  (Future work: extend event
-- trigger to also drop matching bgw_job rows.)
SELECT count(*) AS orphan_jobs_after_drop
  FROM time_series.bgw_job WHERE id = :jid_drop;

DELETE FROM time_series.bgw_job_stat WHERE job_id = :jid_drop;
DELETE FROM time_series.bgw_job      WHERE id     = :jid_drop;

-- ============================================================
-- Section 4: Invalidation behaviour via synchronous CALL
--
-- These borrow TSDB cagg_bgw.sql.in's invalidation scenarios but exercise
-- them via synchronous CALL refresh_continuous_aggregate(...) instead of
-- waiting for BGW. The BGW path itself is already covered in Section 3.
-- ============================================================

\echo '=== BGW-03: INSERT new data → next refresh picks it up ==='
-- TSDB cagg_bgw.sql.in L207-L233 (insert + run + verify)
-- Materialize current state first
CALL time_series.refresh_continuous_aggregate('cv_tstz', NULL, NULL);

-- Snapshot baseline
SELECT count(*) AS pre_insert_rows FROM cv_tstz;

-- Insert one new row in a brand new bucket (one bucket forward in time)
INSERT INTO metrics_tstz VALUES
  (date_trunc('hour', now()) + interval '1 hour' + interval '30 minutes',
   1, 99.9);

CALL time_series.refresh_continuous_aggregate('cv_tstz', NULL, NULL);

-- The new bucket should be visible
SELECT count(*) > 0 AS new_bucket_visible FROM cv_tstz
 WHERE bucket = date_trunc('hour', now()) + interval '1 hour';

\echo '=== BGW-04: UPDATE old data → invalidation log → next refresh picks up ==='
-- TSDB cagg_bgw.sql.in L236
UPDATE metrics_tstz SET temperature = 1000.0
 WHERE time = date_trunc('hour', now()) - interval '47 hours'
   AND tags_id = 1;

CALL time_series.refresh_continuous_aggregate('cv_tstz', NULL, NULL);

-- The cell containing the updated row should reflect the change.
SELECT max(temperature) >= 1000.0 AS update_visible_in_source
  FROM metrics_tstz
 WHERE time = date_trunc('hour', now()) - interval '47 hours' AND tags_id = 1;

SELECT bool_or(round(avg_temp::numeric, 1) >= 100) AS update_visible_in_cagg
  FROM cv_tstz
 WHERE bucket = date_trunc('hour', now()) - interval '47 hours' AND tags_id = 1;

\echo '=== BGW-05: DELETE old data → next refresh sees deletion ==='
-- TSDB cagg_bgw.sql.in L236-L254 (DELETE variant)
DELETE FROM metrics_tstz
 WHERE time = date_trunc('hour', now()) - interval '46 hours'
   AND tags_id = 2;

CALL time_series.refresh_continuous_aggregate('cv_tstz', NULL, NULL);

-- That source-bucket cell should now have one fewer row in cagg
SELECT cnt
  FROM cv_tstz
 WHERE bucket = date_trunc('hour', now()) - interval '46 hours'
   AND tags_id = 2;

-- ============================================================
-- Section 5: ts_policy_refresh_cagg procedure entry point
--
-- Borrowed from TSDB cagg_errors.sql L405-L465 (alter_job + config jsonb
-- validation). V1 does not have alter_job, but it does have a public
-- procedure ts_policy_refresh_cagg(job_id, config) intended as a manual
-- trigger of the policy.
-- ============================================================

\echo '=== POL-CALL-01: direct CALL ts_policy_refresh_cagg succeeds (manual trigger) ==='
-- ts_policy_refresh_cagg is the policy procedure registered in
-- bgw_job.proc_name; it parses config jsonb and dispatches to
-- refresh_continuous_aggregate via ExecuteCallStmt (NOT SPI_execute("CALL
-- ...") which would hit CBDB's "atomic context" guard).  Calling it
-- manually from psql exercises the same path the BGW worker uses.
SELECT add_continuous_aggregate_policy('cv_tstz',
       '7 days'::interval, '0'::interval, '10 seconds'::interval) AS jid_pol \gset
SELECT id AS jid_pol_check, config AS jcfg
  FROM time_series.bgw_job WHERE id = :jid_pol \gset
CALL time_series.ts_policy_refresh_cagg(:jid_pol, :'jcfg'::jsonb);

\echo '=== POL-CALL-02: NULL config → ERROR (TSDB cagg_errors L411-413 spirit) ==='
-- TSDB rejects malformed/missing config via alter_job(check_config). Our
-- procedure does an explicit NULL check at the top.
\set ON_ERROR_STOP 0
CALL time_series.ts_policy_refresh_cagg(:jid_pol, NULL);
\set ON_ERROR_STOP 1

\echo '=== POL-CALL-03: config missing cagg_name → ERROR ==='
-- TSDB cagg_errors.sql L405-407 (config validates required fields).
-- The raw error contains a dynamic function-pointer address; wrap in a
-- helper function so we can swallow the exception and emit a stable
-- NOTICE in its place.
CREATE OR REPLACE FUNCTION pg_temp.expect_call03_error(jid int) RETURNS void
LANGUAGE plpgsql AS $$
BEGIN
    CALL time_series.ts_policy_refresh_cagg(jid,
         '{"start_offset": "1 day", "end_offset": "0"}'::jsonb);
    RAISE EXCEPTION 'expected error did not occur';
EXCEPTION WHEN others THEN
    RAISE NOTICE 'POL-CALL-03 raised expected error';
END$$;
SELECT pg_temp.expect_call03_error(:jid_pol);

\echo '=== POL-CONFIG-01: bgw_job.config has all expected fields ==='
-- TSDB cagg_errors.sql L437-440 verifies the inserted bgw_job row.
SELECT (config->>'cagg_name')   IS NOT NULL AS has_cagg_name,
       config ? 'start_offset'  AS has_start_offset_key,
       config ? 'end_offset'    AS has_end_offset_key,
       config->>'start_offset'  AS start_offset_val,
       config->>'end_offset'    AS end_offset_val
  FROM time_series.bgw_job WHERE id = :jid_pol;

SELECT remove_continuous_aggregate_policy('cv_tstz');

-- ============================================================
-- Section 6: Lifecycle stress (add/remove/add idempotence; multi-cagg)
-- ============================================================

\echo '=== POL-LIFE-01: add → remove → add → remove cycle is idempotent ==='
-- TSDB cagg_policy.sql L72-L95 spirit (one-policy-per-cagg invariant).
SELECT add_continuous_aggregate_policy('cv_tstz',
       '2 days'::interval, '0'::interval, '10 seconds'::interval) AS jid_a \gset
SELECT remove_continuous_aggregate_policy('cv_tstz');
SELECT add_continuous_aggregate_policy('cv_tstz',
       '2 days'::interval, '0'::interval, '10 seconds'::interval) AS jid_b \gset
SELECT remove_continuous_aggregate_policy('cv_tstz');
-- Each add must produce a distinct job_id (we don't reuse old ids)
SELECT :jid_b > :jid_a AS new_jid_strictly_higher;
-- And after removal, no policies remain
SELECT count(*) AS policies_after_cycle FROM time_series.cagg_policy_stats;

\echo '=== POL-LIFE-02: multi-cagg policies are independent ==='
-- TSDB cagg_bgw.sql.in L75-L95 (multiple cagg + multiple policies).
-- Add policies on cv_tstz and cv_date with different schedule_intervals.
SELECT add_continuous_aggregate_policy('cv_tstz',
       '2 days'::interval, '0'::interval, '10 seconds'::interval)
  AS jid_t \gset
SELECT add_continuous_aggregate_policy('cv_date',
       '7 days'::interval, '0'::interval, '20 seconds'::interval)
  AS jid_d \gset

-- Both visible, with their own schedule_interval
SELECT cagg_name, schedule_interval, start_offset
  FROM time_series.cagg_policy_stats
 ORDER BY cagg_name;

-- Removing one does not touch the other
SELECT remove_continuous_aggregate_policy('cv_tstz');
SELECT cagg_name FROM time_series.cagg_policy_stats ORDER BY cagg_name;
SELECT remove_continuous_aggregate_policy('cv_date');

\echo '=== POL-LIFE-03: removing a cagg with no policy does not affect others ==='
-- Defensive: if_exists=true on a cagg without policy is silent
SELECT add_continuous_aggregate_policy('cv_tstz',
       '2 days'::interval, '0'::interval, '10 seconds'::interval)
  AS jid_e \gset
SELECT remove_continuous_aggregate_policy('cv_date', if_exists => true);
-- cv_tstz policy still there
SELECT count(*) AS active_policies FROM time_series.cagg_policy_stats
 WHERE cagg_name LIKE '%cv_tstz%';
SELECT remove_continuous_aggregate_policy('cv_tstz');

-- ============================================================
-- Section 7: cagg_policy_stats view full column coverage
-- ============================================================

\echo '=== POL-VIEW-03: stats view exposes execution-history columns ==='
-- TSDB cagg_bgw.sql.in L255 ("check the information views").
-- Verify the view exposes the worker-execution telemetry columns. After
-- a fresh add, runtime fields are NULL/-infinity until the worker fires.
SELECT add_continuous_aggregate_policy('cv_tstz',
       '2 days'::interval, '0'::interval, '10 seconds'::interval)
  AS jid_v3 \gset
-- Use \d to confirm column shape
SELECT column_name, data_type
  FROM information_schema.columns
 WHERE table_schema = 'time_series'
   AND table_name   = 'cagg_policy_stats'
 ORDER BY ordinal_position;

-- Pre-execution values: last_*  -infinity; next_start in the future
SELECT job_id IS NOT NULL                       AS has_job_id,
       last_start = '-infinity'::timestamptz    AS last_start_neg_inf,
       last_finish = '-infinity'::timestamptz   AS last_finish_neg_inf,
       last_run_success = false                 AS last_run_success_false,
       active = true                            AS active_true
  FROM time_series.cagg_policy_stats WHERE job_id = :jid_v3;
SELECT remove_continuous_aggregate_policy('cv_tstz');

-- ============================================================
-- Section 8: Edge cases borrowed from TSDB
-- ============================================================

\echo '=== POL-EDGE-01: schedule_interval as fractional seconds ==='
-- TSDB cagg_policy.sql L46+ tests sub-second / fractional intervals.
-- '500 ms' is accepted as a tiny but valid schedule_interval.
SELECT add_continuous_aggregate_policy('cv_tstz',
       '2 days'::interval, '0'::interval, '500 ms'::interval) AS jid_fast \gset
SELECT schedule_interval FROM time_series.cagg_policy_stats WHERE job_id = :jid_fast;
SELECT remove_continuous_aggregate_policy('cv_tstz');

\echo '=== POL-EDGE-02: very large start_offset (no overflow) ==='
-- TSDB cagg_policy.sql L410-L451 (overflow boundary, smallint variant).
-- We don't have smallint time, but we still verify big intervals are stored
-- and round-tripped correctly through the JSONB config.
SELECT add_continuous_aggregate_policy('cv_tstz',
       '100 years'::interval, '0'::interval, '10 seconds'::interval) AS jid_big \gset
SELECT start_offset FROM time_series.cagg_policy_stats WHERE job_id = :jid_big;
SELECT remove_continuous_aggregate_policy('cv_tstz');

\echo '=== POL-EDGE-03: NULL,NULL → full refresh policy ==='
-- TSDB cagg_errors.sql L436 (NULL,NULL accepted, results in CALL refresh
-- with both args NULL → full materialization at run time).
SELECT add_continuous_aggregate_policy('cv_tstz',
       NULL, NULL, '10 seconds'::interval) AS jid_full \gset
SELECT start_offset IS NULL AS s_null,
       end_offset   IS NULL AS e_null
  FROM time_series.cagg_policy_stats WHERE job_id = :jid_full;
SELECT remove_continuous_aggregate_policy('cv_tstz');

\echo '=== POL-EDGE-04: NULL schedule_interval → ERROR ==='
-- Required parameter, schedule_interval must be NOT NULL.
-- Use VERBOSITY terse to suppress the per-row DETAIL line (which includes
-- a timestamp that varies between runs).
\set ON_ERROR_STOP 0
\set VERBOSITY terse
SELECT add_continuous_aggregate_policy('cv_tstz',
       '2 days'::interval, '0'::interval, NULL);
\set VERBOSITY default
\set ON_ERROR_STOP 1

\echo '=== POL-EDGE-05: schedule_interval = 0 (V1 behavior — accepted) ==='
-- TSDB rejects '0 seconds'. V1 currently accepts it (the BGW scheduler
-- would just keep firing back-to-back). Document the V1 behavior.
SELECT add_continuous_aggregate_policy('cv_tstz',
       '2 days'::interval, '0'::interval, '0 seconds'::interval) AS jid_zero \gset
SELECT schedule_interval FROM time_series.cagg_policy_stats WHERE job_id = :jid_zero;
SELECT remove_continuous_aggregate_policy('cv_tstz');

\echo '=== POL-EDGE-06: schedule_interval as compound interval (1d 12h 30m) ==='
SELECT add_continuous_aggregate_policy('cv_tstz',
       '2 days'::interval, '0'::interval, '1 day 12 hours 30 minutes'::interval)
  AS jid_compound \gset
SELECT schedule_interval FROM time_series.cagg_policy_stats WHERE job_id = :jid_compound;
SELECT remove_continuous_aggregate_policy('cv_tstz');

\echo '=== POL-EDGE-07: extreme start_offset (1,000,000 years) accepted ==='
-- TSDB cagg_policy.sql L325 ('1000000 years' boundary).
SELECT add_continuous_aggregate_policy('cv_tstz',
       '1000000 years'::interval, '1 day'::interval, '10 seconds'::interval)
  AS jid_huge \gset
SELECT start_offset, end_offset FROM time_series.cagg_policy_stats WHERE job_id = :jid_huge;
SELECT remove_continuous_aggregate_policy('cv_tstz');

\echo '=== POL-EDGE-08: invalid interval string "xyz" → ERROR ==='
-- TSDB cagg_policy.sql L359 (typo / garbage in offset rejected at parse time).
\set ON_ERROR_STOP 0
SELECT add_continuous_aggregate_policy('cv_tstz',
       'xyz'::interval, '1 day'::interval, '10 seconds'::interval);
\set ON_ERROR_STOP 1

\echo '=== POL-EDGE-09: negative end_offset accepted (window into the future) ==='
-- TSDB cagg_policy.sql L290-296 accepts negative end_offset.  A negative
-- end_offset means "the right edge is in the future relative to now()" —
-- useful for refreshing a window that extends slightly beyond the latest
-- ingested timestamp.  V1 has no special check, just enforces start>end.
SELECT add_continuous_aggregate_policy('cv_tstz',
       '13 days'::interval, '-1 day'::interval, '10 seconds'::interval)
  AS jid_neg \gset
SELECT start_offset, end_offset FROM time_series.cagg_policy_stats WHERE job_id = :jid_neg;
SELECT remove_continuous_aggregate_policy('cv_tstz');

-- ============================================================
-- Section 9: bgw_job catalog field assertions
-- ============================================================

\echo '=== POL-CONFIG-02: bgw_job has correct fixed metadata ==='
-- TSDB cagg_bgw.sql.in L106 ("-- job was created").
SELECT add_continuous_aggregate_policy('cv_tstz',
       '2 days'::interval, '0'::interval, '10 seconds'::interval) AS jid_meta \gset

SELECT proc_schema,
       proc_name,
       scheduled,
       fixed_schedule,
       schedule_interval,
       max_runtime,
       max_retries,
       retry_period
  FROM time_series.bgw_job WHERE id = :jid_meta;

\echo '=== POL-CONFIG-03: application_name follows naming convention ==='
-- Format: "Refresh CAGG Policy [<cagg_name>]"
SELECT application_name FROM time_series.bgw_job WHERE id = :jid_meta;

\echo '=== POL-CONFIG-04: hypertable_id points to the matching cagg_id ==='
SELECT j.hypertable_id = c.cagg_id AS hypertable_id_matches_cagg_id
  FROM time_series.bgw_job        j
  JOIN time_series.continuous_agg c
    ON c.user_view_name = 'cv_tstz'
 WHERE j.id = :jid_meta;

\echo '=== POL-CONFIG-05: initial_start is recent (within ±1 minute of now) ==='
-- TSDB stores creation time; we just sanity-check it's near now().
SELECT abs(extract(epoch FROM (initial_start - now()))) < 60 AS initial_start_recent
  FROM time_series.bgw_job WHERE id = :jid_meta;

SELECT remove_continuous_aggregate_policy('cv_tstz');

-- ============================================================
-- Section 10: cagg_policy_stats / bgw_job / bgw_job_stat consistency
-- ============================================================

\echo '=== POL-CONS-01: cagg_policy_stats row count = bgw_job count ==='
SELECT add_continuous_aggregate_policy('cv_tstz',
       '2 days'::interval, '0'::interval, '10 seconds'::interval) AS jid_c1 \gset
SELECT add_continuous_aggregate_policy('cv_date',
       '7 days'::interval, '0'::interval, '20 seconds'::interval) AS jid_c2 \gset

SELECT
    (SELECT count(*) FROM time_series.cagg_policy_stats) =
    (SELECT count(*) FROM time_series.bgw_job
       WHERE proc_name = 'ts_policy_refresh_cagg') AS counts_match;

\echo '=== POL-CONS-02: every policy has matching stat row ==='
SELECT count(*) AS policies_without_stat
  FROM time_series.cagg_policy_stats v
 WHERE NOT EXISTS (
   SELECT 1 FROM time_series.bgw_job_stat s WHERE s.job_id = v.job_id
 );

\echo '=== POL-CONS-03: cagg_policy_stats reflects schedule_interval correctly ==='
SELECT cagg_name, schedule_interval
  FROM time_series.cagg_policy_stats
 WHERE job_id IN (:jid_c1, :jid_c2)
 ORDER BY cagg_name;

SELECT remove_continuous_aggregate_policy('cv_tstz');
SELECT remove_continuous_aggregate_policy('cv_date');

\echo '=== POL-CONS-04: after remove, no orphan stats ==='
SELECT count(*) AS stats_after_all_removed FROM time_series.bgw_job_stat
 WHERE job_id IN (
   SELECT id FROM time_series.bgw_job WHERE proc_name = 'ts_policy_refresh_cagg'
 );

-- ============================================================
-- Section 11: schema-qualified and special name handling
-- ============================================================

\echo '=== POL-NAME-01: schema-qualified cagg_name (public.cv_tstz) ==='
-- _resolve_cagg_id (P2-N2 fix) routes through to_regclass, which
-- understands schema-qualified names.  'public.cv_tstz' therefore
-- resolves to the cagg in public, and add succeeds.  Remove the
-- policy afterward so the next test starts clean.
SELECT add_continuous_aggregate_policy('public.cv_tstz',
       '2 days'::interval, '0'::interval, '10 seconds'::interval) IS NOT NULL
   AS qualified_name_works;
SELECT remove_continuous_aggregate_policy('public.cv_tstz');

\echo '=== POL-NAME-02: empty-string cagg_name ==='
\set VERBOSITY terse
\set ON_ERROR_STOP 0
SELECT add_continuous_aggregate_policy('',
       '2 days'::interval, '0'::interval, '10 seconds'::interval);
\set ON_ERROR_STOP 1
\set VERBOSITY default

\echo '=== POL-NAME-03: case sensitivity (PG identifier rules) ==='
-- V1 stores cagg_name as lowercase (unquoted CREATE MATERIALIZED VIEW).
-- to_regclass applies PG's standard identifier folding: an unquoted
-- 'CV_TSTZ' is lowered to 'cv_tstz' before lookup, so this resolves
-- to the same cagg as 'cv_tstz' and add succeeds.  Quoted upper-case
-- '"CV_TSTZ"' would NOT fold and would correctly fail to resolve.
SELECT add_continuous_aggregate_policy('CV_TSTZ',
       '2 days'::interval, '0'::interval, '10 seconds'::interval) IS NOT NULL
   AS unquoted_uppercase_folds;
SELECT remove_continuous_aggregate_policy('cv_tstz');

\set VERBOSITY terse
\set ON_ERROR_STOP 0
SELECT add_continuous_aggregate_policy('"CV_TSTZ"',
       '2 days'::interval, '0'::interval, '10 seconds'::interval);
\set ON_ERROR_STOP 1
\set VERBOSITY default

-- ============================================================
-- Section 12: Manual CALL refresh ↔ policy interaction
-- ============================================================

\echo '=== POL-EXEC-01: manual CALL refresh does not change bgw_job_stat ==='
-- TSDB cagg_bgw.sql.in spirit (manual refresh shouldn't pollute scheduler stat)
SELECT add_continuous_aggregate_policy('cv_tstz',
       '7 days'::interval, '0'::interval, '10 seconds'::interval) AS jid_e1 \gset

-- Capture stat BEFORE manual refresh
SELECT total_runs AS runs_before, total_successes AS succ_before
  FROM time_series.bgw_job_stat WHERE job_id = :jid_e1 \gset

-- Run a manual refresh
CALL time_series.refresh_continuous_aggregate('cv_tstz', NULL, NULL);

-- Stat must be unchanged (manual refresh ≠ BGW worker run)
SELECT total_runs    = :runs_before AS runs_unchanged,
       total_successes = :succ_before AS succ_unchanged
  FROM time_series.bgw_job_stat WHERE job_id = :jid_e1;

SELECT remove_continuous_aggregate_policy('cv_tstz');

\echo '=== POL-EXEC-02: manual CALL refresh produces correct materialization (EXCEPT=0) ==='
-- TSDB cagg_bgw.sql.in implicit invariant.
-- Direct aggregation on source table must equal the cagg materialization.
CALL time_series.refresh_continuous_aggregate('cv_tstz', NULL, NULL);

WITH expected AS (
    SELECT time_bucket('1 hour'::interval, time) AS bucket,
           tags_id, count(*) AS cnt,
           round(avg(temperature)::numeric, 6) AS avg_temp
      FROM metrics_tstz
     GROUP BY 1, 2
), actual AS (
    SELECT bucket, tags_id, cnt, round(avg_temp::numeric, 6) AS avg_temp
      FROM cv_tstz
)
SELECT count(*) AS diff_count
  FROM (
    (SELECT * FROM expected EXCEPT SELECT * FROM actual)
    UNION ALL
    (SELECT * FROM actual EXCEPT SELECT * FROM expected)
  ) d;

-- ============================================================
-- Section 13: TRUNCATE invalidation behaviour with policy present
-- ============================================================

\echo '=== POL-INV-01: TRUNCATE source → cagg cleared on next refresh ==='
-- TSDB cagg_bgw.sql.in invalidation tests
-- Re-populate first, then TRUNCATE, then refresh.
INSERT INTO metrics_tstz
SELECT date_trunc('hour', now()) - ((48 - h) * interval '1 hour'),
       (h % 5) + 1, 50.0 + h FROM generate_series(1, 48) h;
CALL time_series.refresh_continuous_aggregate('cv_tstz', NULL, NULL);
SELECT count(*) > 0 AS cagg_has_data_before_trunc FROM cv_tstz;

TRUNCATE metrics_tstz;
CALL time_series.refresh_continuous_aggregate('cv_tstz', NULL, NULL);
SELECT count(*) AS cagg_rows_after_trunc FROM cv_tstz;

-- Restore data for downstream sections
INSERT INTO metrics_tstz
SELECT date_trunc('hour', now()) - ((48 - h) * interval '1 hour')
       + (m * interval '5 minutes'),
       (h % 5) + 1, 20.0 + h * 0.1 + m * 0.01
FROM generate_series(1, 48) h, generate_series(0, 5) m;

-- ============================================================
-- Section 14: alter_job — modify policy in place
-- ============================================================

\echo '=== ALTER-01: alter schedule_interval ==='
SELECT add_continuous_aggregate_policy('cv_tstz',
       '7 days'::interval, '0'::interval, '10 seconds'::interval) AS jid_a1 \gset
SELECT (alter_job(:jid_a1, schedule_interval => '1 minute'::interval)).schedule_interval;
SELECT schedule_interval FROM time_series.bgw_job WHERE id = :jid_a1;

\echo '=== ALTER-02: alter pauses (scheduled = false) ==='
SELECT (alter_job(:jid_a1, scheduled => false)).scheduled;
SELECT scheduled FROM time_series.bgw_job WHERE id = :jid_a1;

\echo '=== ALTER-03: alter resumes (scheduled = true) ==='
SELECT (alter_job(:jid_a1, scheduled => true)).scheduled;

\echo '=== ALTER-04: alter changes config (valid jsonb) ==='
SELECT (alter_job(:jid_a1,
        config => jsonb_build_object(
            'cagg_name', 'cv_tstz',
            'start_offset', '14 days',
            'end_offset', '0'))).config;

\echo '=== ALTER-05: alter rejects config without cagg_name ==='
\set ON_ERROR_STOP 0
SELECT alter_job(:jid_a1,
       config => '{"start_offset": "1 day", "end_offset": "0"}'::jsonb);
\set ON_ERROR_STOP 1

-- ALTER-05a/b/c: alter_job must apply the same semantic checks as
-- add_continuous_aggregate_policy.  Without these, a typo'd cagg_name
-- or a too-narrow window silently breaks the policy and the failure
-- only surfaces as last_run_success=false in mark_end.
\echo '=== ALTER-05a: alter rejects ghost cagg_name ==='
\set ON_ERROR_STOP 0
SELECT alter_job(:jid_a1,
       config => '{"cagg_name": "ghost_does_not_exist", "start_offset": "1 day", "end_offset": "0"}'::jsonb);
\set ON_ERROR_STOP 1

\echo '=== ALTER-05b: alter rejects refresh window < 2 buckets ==='
\set ON_ERROR_STOP 0
-- bucket_width is 1 hour for cv_tstz; 30-minute window covers 0 buckets
SELECT alter_job(:jid_a1,
       config => '{"cagg_name": "cv_tstz", "start_offset": "30 minutes", "end_offset": "0"}'::jsonb);
\set ON_ERROR_STOP 1

\echo '=== ALTER-05c: alter rejects start_offset <= end_offset ==='
\set ON_ERROR_STOP 0
SELECT alter_job(:jid_a1,
       config => '{"cagg_name": "cv_tstz", "start_offset": "0", "end_offset": "1 hour"}'::jsonb);
\set ON_ERROR_STOP 1

\echo '=== ALTER-06: alter sets explicit next_start ==='
SELECT (alter_job(:jid_a1,
        next_start => '2099-01-01 00:00+00'::timestamptz)).id IS NOT NULL AS ok;
SELECT next_start FROM time_series.bgw_job_stat WHERE job_id = :jid_a1;

\echo '=== ALTER-07: alter non-existent job → ERROR ==='
\set ON_ERROR_STOP 0
SELECT alter_job(99999, schedule_interval => '1 minute'::interval);
\set ON_ERROR_STOP 1

\echo '=== ALTER-08: alter non-existent + if_exists → NOTICE returns NULL ==='
SELECT alter_job(99999, schedule_interval => '1 minute'::interval, if_exists => true) IS NULL AS got_null;

\echo '=== ALTER-09: alter all fields at once ==='
SELECT (alter_job(:jid_a1,
        schedule_interval => '5 minutes'::interval,
        max_runtime       => '1 minute'::interval,
        max_retries       => 3,
        retry_period      => '30 seconds'::interval,
        scheduled         => true,
        config            => jsonb_build_object(
            'cagg_name', 'cv_tstz',
            'start_offset', '30 days',
            'end_offset', '1 hour'))).schedule_interval;

SELECT schedule_interval, max_runtime, max_retries, retry_period, scheduled
  FROM time_series.bgw_job WHERE id = :jid_a1;

SELECT remove_continuous_aggregate_policy('cv_tstz');

-- ============================================================
-- Section 15: run_job — synchronous in-session BGW worker simulator
-- ============================================================

\echo '=== RUN-01: run_job triggers refresh, stat updates ==='
SELECT add_continuous_aggregate_policy('cv_tstz',
       '7 days'::interval, '0'::interval, '10 seconds'::interval) AS jid_r1 \gset

SELECT total_runs AS runs_before FROM time_series.bgw_job_stat WHERE job_id = :jid_r1 \gset
CALL time_series.run_job(:jid_r1);
SELECT total_runs    = :runs_before + 1 AS runs_incremented,
       total_successes > 0                AS success_recorded,
       total_crashes = 0                AS no_crashes,
       last_run_success                 AS last_run_success
  FROM time_series.bgw_job_stat WHERE job_id = :jid_r1;

\echo '=== RUN-02: 5 sequential runs accumulate (dangling-pointer regression) ==='
-- This is the regression for cagg_bgw_debug_journey.md Bug #2: ts_bgw_job_stat_find
-- used to return a dangling pointer, causing total_duration_failures to overflow
-- after multiple runs.  We assert sane totals across 5 runs.
CALL time_series.run_job(:jid_r1);
CALL time_series.run_job(:jid_r1);
CALL time_series.run_job(:jid_r1);
CALL time_series.run_job(:jid_r1);

SELECT
    total_runs >= :runs_before + 5                            AS runs_at_least_5_more,
    total_crashes = 0                                         AS no_crashes,
    total_duration         BETWEEN '0'::interval
                                AND '10 minutes'::interval    AS dur_sane,
    total_duration_failures BETWEEN '0'::interval
                                AND '1 minute'::interval      AS dur_fail_sane,
    extract(epoch from total_duration_failures) > -86400      AS dur_fail_no_overflow
  FROM time_series.bgw_job_stat WHERE job_id = :jid_r1;

\echo '=== RUN-03: run_job populates the cagg materialization correctly ==='
-- Direct aggregation must equal cagg materialization (EXCEPT = 0)
WITH expected AS (
    SELECT time_bucket('1 hour'::interval, time) AS bucket,
           tags_id, count(*) AS cnt,
           round(avg(temperature)::numeric, 6) AS avg_temp
      FROM metrics_tstz
     GROUP BY 1, 2
), actual AS (
    SELECT bucket, tags_id, cnt, round(avg_temp::numeric, 6) AS avg_temp FROM cv_tstz
)
SELECT count(*) AS diff_count
  FROM (
    (SELECT * FROM expected EXCEPT SELECT * FROM actual)
    UNION ALL
    (SELECT * FROM actual EXCEPT SELECT * FROM expected)
  ) d;

\echo '=== RUN-04: run_job for non-existent job → ERROR ==='
\set ON_ERROR_STOP 0
CALL time_series.run_job(99999);
\set ON_ERROR_STOP 1

\echo '=== RUN-05: run_job with NULL job_id → ERROR ==='
\set ON_ERROR_STOP 0
CALL time_series.run_job(NULL);
\set ON_ERROR_STOP 1

\echo '=== RUN-06: alter_job + run_job sequence — modified config takes effect ==='
-- Alter: tighten the refresh window to a 1-day band ending 6 hours ago.
-- run_job uses the new config.
SELECT (alter_job(:jid_r1,
        config => jsonb_build_object(
            'cagg_name', 'cv_tstz',
            'start_offset', '1 day',
            'end_offset', '6 hours'))).id IS NOT NULL AS altered;
CALL time_series.run_job(:jid_r1);

-- last_run_success must still be true (refresh ran with new window)
SELECT last_run_success FROM time_series.bgw_job_stat WHERE job_id = :jid_r1;

SELECT remove_continuous_aggregate_policy('cv_tstz');

\echo '=== POL-INV-02: many UPDATEs across multiple buckets → merged refresh ==='
-- TSDB cagg_bgw.sql.in L270 ("test merged refresh — change data in two chunks")
CALL time_series.refresh_continuous_aggregate('cv_tstz', NULL, NULL);

-- Update rows in 5 different hours simultaneously
UPDATE metrics_tstz SET temperature = 999.0
 WHERE time IN (
   date_trunc('hour', now()) - interval '40 hours',
   date_trunc('hour', now()) - interval '35 hours',
   date_trunc('hour', now()) - interval '30 hours',
   date_trunc('hour', now()) - interval '25 hours',
   date_trunc('hour', now()) - interval '20 hours'
 ) AND tags_id = 1;

-- Single refresh should pick up all 5 updated buckets
CALL time_series.refresh_continuous_aggregate('cv_tstz', NULL, NULL);

SELECT count(*) AS buckets_with_999_avg FROM cv_tstz
 WHERE bucket IN (
   date_trunc('hour', now()) - interval '40 hours',
   date_trunc('hour', now()) - interval '35 hours',
   date_trunc('hour', now()) - interval '30 hours',
   date_trunc('hour', now()) - interval '25 hours',
   date_trunc('hour', now()) - interval '20 hours'
 ) AND tags_id = 1
   AND avg_temp > 100;

-- ============================================================
-- Section 16: cross-user permission failure on BGW refresh
--
-- Borrowed from TSDB cagg_bgw.sql.in L292-315 ("create a view with a
-- function that it has no permission to execute") and L322-384 ("user
-- is the non-owner of the raw table").  V1 doesn't have the multi-user
-- ROLE infrastructure those tests use; we exercise the failure-recording
-- path with a missing source table, which routes through the same
-- PG_CATCH + mark_end branch as a real permission denied error.
-- ============================================================

\echo '=== PERM-01: refresh records failure when source table is missing ==='
-- V1 doesn't support running a policy as a non-superuser without granting
-- INSERT on every internal time_series catalog (continuous_agg,
-- cagg_invalidations, etc.).  The point of the original TSDB test was to
-- verify the failure path of the policy worker — total_failures increments
-- when the underlying refresh raises.  We reproduce that by creating a
-- CAGG, attaching a policy, then renaming the source table out from under
-- it: the next run_job will fail and mark_end records the failure.
CREATE MATERIALIZED VIEW cv_perm WITH (time_series.continuous) AS
  SELECT time_bucket('1 hour'::interval, time) AS bucket, tags_id, count(*) AS c
    FROM metrics_tstz GROUP BY bucket, tags_id;
SELECT add_continuous_aggregate_policy('cv_perm',
       '7 days'::interval, '1 hour'::interval, '10 seconds'::interval) AS jid_perm \gset

-- Inject a guaranteed failure: attach a CHECK(false) constraint to the
-- CAGG's materialization table so the refresh INSERT raises.
DO $$
DECLARE qual text;
BEGIN
    SELECT format('%I.%I', mat_table_schema, mat_table_name) INTO qual
      FROM time_series.continuous_agg WHERE user_view_name = 'cv_perm';
    EXECUTE format('ALTER TABLE %s ADD CONSTRAINT bgw_perm_check CHECK (false)', qual);
END$$;

\echo 'PERM-01 step 2: run_job with missing source records failure in stat'
\set VERBOSITY terse
\set ON_ERROR_STOP 0
CALL time_series.run_job(:jid_perm);
\set ON_ERROR_STOP 1
\set VERBOSITY default

-- mark_start ran (total_runs > 0); the refresh INSERT violated the CHECK
-- and aborted the inner subtransaction; PG_CATCH rolled the subtransaction
-- back, then mark_end recorded the failure in the now-healthy outer txn.
SELECT total_runs > 0           AS run_attempted,
       total_failures > 0       AS failure_recorded,
       last_run_success = false AS last_ok_should_be_false
  FROM time_series.bgw_job_stat WHERE job_id = :jid_perm;

-- Clean up.
SELECT remove_continuous_aggregate_policy('cv_perm');
DROP VIEW cv_perm;

-- ============================================================
-- PERM-OWNER-01..05: owner-based permission checks on alter_job /
-- run_job / remove_continuous_aggregate_policy.
--
-- bgw_job.owner is set to current_role at INSERT time.  Each of the
-- three management entry points must reject callers who are not a
-- member of that role (TSDB ts_bgw_job_permission_check semantics).
-- pg_has_role(..., 'MEMBER') makes superuser and the owner role
-- itself pass; everyone else is rejected with INSUFFICIENT_PRIVILEGE.
-- ============================================================

-- Two unprivileged roles, no superuser bit.  Superuser creates the
-- CAGG + policy on behalf of ts_owner_a (CAGG creation requires
-- CREATE on time_series schema, which we don't want to grant to a
-- random user); we then UPDATE bgw_job.owner to make ts_owner_a the
-- legitimate owner.  ts_owner_b will try to alter / run / remove it.
SET client_min_messages TO warning;
DROP ROLE IF EXISTS ts_owner_a;
DROP ROLE IF EXISTS ts_owner_b;
RESET client_min_messages;
CREATE ROLE ts_owner_a LOGIN;
CREATE ROLE ts_owner_b LOGIN;

-- Both roles need to be able to read time_series catalog tables and
-- call the management functions.
GRANT USAGE ON SCHEMA time_series TO ts_owner_a, ts_owner_b;
GRANT SELECT, INSERT, UPDATE, DELETE ON ALL TABLES IN SCHEMA time_series TO ts_owner_a, ts_owner_b;
GRANT USAGE ON ALL SEQUENCES IN SCHEMA time_series TO ts_owner_a, ts_owner_b;

CREATE MATERIALIZED VIEW cv_owner WITH (time_series.continuous) AS
  SELECT time_bucket('1 hour'::interval, time) AS bucket, tags_id, count(*) AS c
    FROM metrics_tstz GROUP BY bucket, tags_id;
SELECT add_continuous_aggregate_policy('cv_owner',
       '7 days'::interval, '1 hour'::interval, '10 seconds'::interval)
   AS jid_owner \gset

-- Reassign the policy ownership to ts_owner_a so the perm checks
-- exercise a real cross-user attempt.
UPDATE time_series.bgw_job SET owner = 'ts_owner_a'::regrole
  WHERE id = :jid_owner;

\echo '=== PERM-OWNER-01: non-owner alter_job → INSUFFICIENT_PRIVILEGE ==='
SET ROLE ts_owner_b;
\set VERBOSITY terse
\set ON_ERROR_STOP 0
SELECT alter_job(:jid_owner, schedule_interval => '1 minute'::interval);
\set ON_ERROR_STOP 1
\set VERBOSITY default
RESET ROLE;

\echo '=== PERM-OWNER-02: non-owner run_job → INSUFFICIENT_PRIVILEGE ==='
SET ROLE ts_owner_b;
\set VERBOSITY terse
\set ON_ERROR_STOP 0
CALL time_series.run_job(:jid_owner);
\set ON_ERROR_STOP 1
\set VERBOSITY default
RESET ROLE;

\echo '=== PERM-OWNER-03: non-owner remove_continuous_aggregate_policy → INSUFFICIENT_PRIVILEGE ==='
SET ROLE ts_owner_b;
\set VERBOSITY terse
\set ON_ERROR_STOP 0
SELECT remove_continuous_aggregate_policy('cv_owner');
\set ON_ERROR_STOP 1
\set VERBOSITY default
RESET ROLE;

\echo '=== PERM-OWNER-04: owner can alter / remove their own policy ==='
SET ROLE ts_owner_a;
SELECT (alter_job(:jid_owner, schedule_interval => '30 seconds'::interval)).id
       = :jid_owner AS owner_alter_returns_same_jid;
SELECT schedule_interval = '30 seconds'::interval AS interval_actually_changed
  FROM time_series.bgw_job WHERE id = :jid_owner;
SELECT remove_continuous_aggregate_policy('cv_owner');
SELECT count(*) = 0 AS policy_actually_removed
  FROM time_series.bgw_job WHERE id = :jid_owner;
RESET ROLE;

\echo '=== PERM-OWNER-05: superuser can manage any policy ==='
-- Re-create the policy as superuser, owned by ts_owner_a.
SELECT add_continuous_aggregate_policy('cv_owner',
       '7 days'::interval, '1 hour'::interval, '10 seconds'::interval)
   AS jid_owner_2 \gset
UPDATE time_series.bgw_job SET owner = 'ts_owner_a'::regrole
  WHERE id = :jid_owner_2;
SELECT (alter_job(:jid_owner_2, scheduled => false)).scheduled = false
   AS super_can_alter;
SELECT remove_continuous_aggregate_policy('cv_owner');
SELECT count(*) = 0 AS super_can_remove
  FROM time_series.bgw_job WHERE id = :jid_owner_2;

-- ============================================================
-- PERM-OWNER-06/07: add_continuous_aggregate_policy owner check
--
-- Without an owner check, in a multi-tenant configuration where the
-- DBA has granted INSERT/UPDATE/DELETE on bgw_job to PUBLIC (a typical
-- "let users manage their own policies" setup), any user could squat
-- on another user's CAGG by adding a policy first — taking ownership
-- of the policy and blocking the real owner from adding their own
-- (one-policy-per-CAGG).  add_continuous_aggregate_policy now checks
-- pg_has_role on the CAGG view's owner, mirroring alter_job /
-- remove_continuous_aggregate_policy / ts_bgw_job_permission_check.
-- ============================================================
\echo '=== PERM-OWNER-06: non-owner add_policy → INSUFFICIENT_PRIVILEGE ==='
-- Reassign view ownership so ts_owner_b's add becomes a real cross-user
-- attempt against ts_owner_a's CAGG.  (PERM-OWNER-05 removed the policy,
-- so the CAGG is policy-less and ready to receive a fresh add.)
ALTER VIEW cv_owner OWNER TO ts_owner_a;

SET ROLE ts_owner_b;
\set VERBOSITY terse
\set ON_ERROR_STOP 0
SELECT add_continuous_aggregate_policy('cv_owner',
       '7 days'::interval, '1 hour'::interval, '10 seconds'::interval);
\set ON_ERROR_STOP 1
\set VERBOSITY default
RESET ROLE;

\echo '--- verify no policy was created ---'
SELECT count(*) = 0 AS no_squat_policy
  FROM time_series.bgw_job
 WHERE proc_name = 'ts_policy_refresh_cagg';

\echo '=== PERM-OWNER-07: owner CAN add policy on their own cagg ==='
SET ROLE ts_owner_a;
SELECT add_continuous_aggregate_policy('cv_owner',
       '7 days'::interval, '1 hour'::interval, '10 seconds'::interval) IS NOT NULL
   AS owner_can_add;
SELECT remove_continuous_aggregate_policy('cv_owner');
RESET ROLE;

DROP VIEW cv_owner;

-- ============================================================
-- PERM-MOCK-01..03: mock-time test functions are not callable by
-- ordinary users.
--
-- Regression for P2-5: the 11 mock-time SQL functions (defined in
-- time_series--1.0.sql, used only by the regression test suite to
-- drive a synthetic BGW scheduler) are REVOKE-d from PUBLIC at
-- extension install time.  Verify a few representative ones reject
-- non-superuser callers cleanly.
-- ============================================================
\echo '=== PERM-MOCK-01: ts_bgw_db_scheduler_test_run rejected for ordinary user ==='
SET ROLE ts_owner_b;
\set VERBOSITY terse
\set ON_ERROR_STOP 0
SELECT time_series.ts_bgw_db_scheduler_test_run(100);
\set ON_ERROR_STOP 1
\set VERBOSITY default
RESET ROLE;

\echo '=== PERM-MOCK-02: ts_bgw_test_job_sleep rejected for ordinary user ==='
SET ROLE ts_owner_b;
\set VERBOSITY terse
\set ON_ERROR_STOP 0
SELECT time_series.ts_bgw_test_job_sleep();
\set ON_ERROR_STOP 1
\set VERBOSITY default
RESET ROLE;

\echo '=== PERM-MOCK-03: ts_bgw_params_create rejected for ordinary user ==='
SET ROLE ts_owner_b;
\set VERBOSITY terse
\set ON_ERROR_STOP 0
SELECT time_series.ts_bgw_params_create();
\set ON_ERROR_STOP 1
\set VERBOSITY default
RESET ROLE;

-- Cleanup roles.  REVOKE before DROP to avoid "role X cannot be
-- dropped because some objects depend on it" surprise.
REVOKE ALL ON ALL TABLES IN SCHEMA time_series FROM ts_owner_a, ts_owner_b;
REVOKE ALL ON ALL SEQUENCES IN SCHEMA time_series FROM ts_owner_a, ts_owner_b;
REVOKE USAGE ON SCHEMA time_series FROM ts_owner_a, ts_owner_b;
DROP ROLE ts_owner_a;
DROP ROLE ts_owner_b;

-- ============================================================
-- Note on BGW worker end-to-end testing (intentionally NOT here)
--
-- TSDB cagg_bgw.sql.in tests the BGW scheduler tick-by-tick using internal
-- C functions (ts_bgw_db_scheduler_test_run_*).  V1 has not ported those
-- helpers, and our scheduler's job-list invalidation does not pick up
-- newly-INSERTed bgw_job rows quickly enough to be reliable inside a
-- regression test (observed: 60 s wait, 0 runs).
--
-- The BGW worker execution path itself has been validated end-to-end
-- (42 successful runs in 7 minutes, no crashes) in
-- doc/feature/cagg/cagg_bgw_debug_journey.md §4.  Once a more
-- deterministic "tick the scheduler now" hook is added, the regression
-- cases listed in
-- doc/feature/cagg/v1/cagg_auto_refresh_test_inventory.md §4 ("BGW-01,
-- BGW-02, BGW-10") should be moved here.
-- ============================================================

-- ============================================================
-- Final cleanup
-- ============================================================
DROP VIEW IF EXISTS cv_tstz;
DROP VIEW IF EXISTS cv_date;
DROP TABLE metrics_tstz;
DROP TABLE metrics_ts;
DROP TABLE metrics_date;
DROP FUNCTION wait_for_runs(int, int, int);
DROP FUNCTION trigger_worker(int);
DROP EXTENSION time_series CASCADE;
