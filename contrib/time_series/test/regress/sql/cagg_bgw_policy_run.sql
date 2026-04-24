-- ============================================================
-- cagg_bgw_policy_run.sql
--
-- 1:1 port of TimescaleDB tsl/test/sql/cagg_policy_run.sql
-- (Apache 2.0).  Exercises run_job + materialization for both
-- date and timestamp time-column types.
--
-- Differences vs upstream:
--   - schema names: _timescaledb_config → time_series
--   - We don't have timescaledb.current_timestamp_mock; the
--     refresh window is computed from real now() instead.  Our
--     test data is therefore anchored relative to now() rather
--     than '2019-09-01'.
--   - create_hypertable replaced with plain CREATE TABLE +
--     DISTRIBUTED BY (V1 doesn't have hypertables)
--   - timescaledb.continuous → time_series.continuous
-- ============================================================

SET optimizer = off;
SET timezone = 'UTC';

DROP EXTENSION IF EXISTS time_series CASCADE;
CREATE EXTENSION time_series;
SET search_path TO public, time_series;

-- ============================================================
-- Test 1: date-typed time column, daily bucket
-- (TSDB cagg_policy_run.sql L11-32)
-- ============================================================
CREATE TABLE continuous_agg_max_mat_date(
    time DATE,
    tags_id INT NOT NULL DEFAULT 1
) DISTRIBUTED BY (tags_id);

CREATE MATERIALIZED VIEW max_mat_view_date
    WITH (time_series.continuous, time_series.materialized_only=true)
    AS SELECT time_bucket('1 days'::interval, time) AS bucket, count(*) AS c
        FROM continuous_agg_max_mat_date
        GROUP BY bucket WITH NO DATA;

SELECT add_continuous_aggregate_policy('max_mat_view_date',
       '3 days'::interval, '1 day'::interval, '1 day'::interval)
   AS job_id \gset

SELECT (config->>'cagg_name')   AS cagg_name,
       (config->>'start_offset') AS start_offset,
       (config->>'end_offset')   AS end_offset
  FROM time_series.bgw_job WHERE id = :job_id;

INSERT INTO continuous_agg_max_mat_date(time)
SELECT (now() - i * interval '1 day')::date
FROM generate_series(1, 10) i;

SET client_min_messages TO warning;
CALL time_series.run_job(:job_id);
RESET client_min_messages;

-- Materialization should have populated the buckets that fall in the
-- [now()-3 days, now()-1 day] refresh window.
SELECT count(*) > 0 AS materialized FROM max_mat_view_date;

SELECT remove_continuous_aggregate_policy('max_mat_view_date');
DROP VIEW max_mat_view_date;
DROP TABLE continuous_agg_max_mat_date CASCADE;

-- ============================================================
-- Test 2: timestamp-typed time column, weekly bucket
-- (TSDB cagg_policy_run.sql L34-51)
-- ============================================================
CREATE TABLE continuous_agg_timestamp(
    time TIMESTAMP,
    tags_id INT NOT NULL DEFAULT 1
) DISTRIBUTED BY (tags_id);

CREATE MATERIALIZED VIEW max_mat_view_timestamp
    WITH (time_series.continuous, time_series.materialized_only=true)
    AS SELECT time_bucket('7 days'::interval, time) AS bucket, count(*) AS c
        FROM continuous_agg_timestamp
        GROUP BY bucket WITH NO DATA;

SELECT add_continuous_aggregate_policy('max_mat_view_timestamp',
       '30 days'::interval, '1 hour'::interval, '1 hour'::interval)
   AS job_id \gset

INSERT INTO continuous_agg_timestamp(time)
SELECT (now() - i * interval '1 day')::timestamp
FROM generate_series(1, 20) i;

SET client_min_messages TO warning;
CALL time_series.run_job(:job_id);
RESET client_min_messages;

SELECT count(*) > 0 AS materialized FROM max_mat_view_timestamp;

SELECT remove_continuous_aggregate_policy('max_mat_view_timestamp');
DROP VIEW max_mat_view_timestamp;
DROP TABLE continuous_agg_timestamp CASCADE;

-- ============================================================
-- Test 3: bgw_job_stat_history (audit log) — always populated in V1
--
-- V1 always records every job execution (no opt-in GUC; see
-- src/bgw/job_stat_history.c file header for the design rationale).
-- ============================================================
CREATE TABLE m_history(
    time TIMESTAMPTZ NOT NULL,
    v INT NOT NULL DEFAULT 1
) DISTRIBUTED BY (v);

CREATE MATERIALIZED VIEW cv_history
    WITH (time_series.continuous, time_series.materialized_only=true)
    AS SELECT time_bucket('1 hour'::interval, time) AS bucket,
              count(*) AS c
       FROM m_history GROUP BY bucket WITH NO DATA;

INSERT INTO m_history(time)
SELECT now() - i * interval '1 hour' FROM generate_series(1, 5) i;

SELECT add_continuous_aggregate_policy('cv_history',
       '1 day'::interval, '0'::interval, '1 hour'::interval)
   AS hist_jid \gset

\echo '=== HIST-01: every run_job appends a history row ==='
TRUNCATE time_series.bgw_job_stat_history;
SET client_min_messages TO warning;
CALL time_series.run_job(:hist_jid);
RESET client_min_messages;
SELECT count(*) AS history_rows,
       bool_and(succeeded) AS all_succeeded,
       bool_and(execution_finish > execution_start) AS finish_after_start,
       bool_and(data ? 'job') AS data_has_job_key
  FROM time_series.bgw_job_stat_history WHERE job_id = :hist_jid;

\echo '=== HIST-02: job_history view exposes structured columns ==='
SELECT count(*) AS rows_in_view,
       bool_and(proc_name = 'ts_policy_refresh_cagg') AS proc_matches,
       bool_and(duration > '0'::interval) AS has_duration
  FROM time_series.job_history WHERE job_id = :hist_jid;

\echo '=== HIST-03: failures are recorded with succeeded=false ==='
-- Force a failure on the next refresh by attaching CHECK(false) NOT VALID
-- to the CAGG materialization table.  NOT VALID skips the existing-row
-- check (HIST-01 already populated the table) but blocks new inserts.
DO $$
DECLARE qual text;
BEGIN
    SELECT format('%I.%I', mat_table_schema, mat_table_name) INTO qual
      FROM time_series.continuous_agg WHERE user_view_name = 'cv_history';
    EXECUTE format('ALTER TABLE %s ADD CONSTRAINT hist_check CHECK (false) NOT VALID', qual);
END$$;
-- Push more source rows so the next refresh has new buckets to materialize,
-- which will trip the CHECK constraint.
INSERT INTO m_history(time)
SELECT now() - interval '1 minute' * i FROM generate_series(1, 5) i;
TRUNCATE time_series.bgw_job_stat_history;
\set VERBOSITY terse
\set ON_ERROR_STOP 0
SET client_min_messages TO warning;
CALL time_series.run_job(:hist_jid);
RESET client_min_messages;
\set ON_ERROR_STOP 1
\set VERBOSITY default
SELECT count(*) AS failure_rows,
       bool_and(NOT succeeded) AS all_failed
  FROM time_series.bgw_job_stat_history WHERE job_id = :hist_jid;

SELECT remove_continuous_aggregate_policy('cv_history');
DROP VIEW cv_history;
DROP TABLE m_history CASCADE;

-- ============================================================
-- Test 4: cagg_refresh must not silently flip caller's optimizer GUC
--
-- cagg_refresh sets the global ORCA flag to off internally to dodge
-- "Operator Update on replicated tables not supported" + downstream
-- crash.  That side effect is acceptable on the BGW worker path
-- (process exits) but would silently corrupt user sessions calling
-- CALL refresh_continuous_aggregate / run_job from psql — leaving
-- the caller without ORCA for the rest of the session.  PG_FINALLY
-- in cagg_refresh restores the prior value.  This test pins the
-- behavior so a future careless edit can't reintroduce the leak.
-- ============================================================
CREATE TABLE m_orca(
    time TIMESTAMPTZ NOT NULL,
    v INT NOT NULL DEFAULT 1
) DISTRIBUTED BY (v);

CREATE MATERIALIZED VIEW cv_orca
    WITH (time_series.continuous, time_series.materialized_only=true)
    AS SELECT time_bucket('1 hour'::interval, time) AS bucket,
              count(*) AS c
       FROM m_orca GROUP BY bucket WITH NO DATA;

INSERT INTO m_orca(time)
SELECT now() - i * interval '1 hour' FROM generate_series(1, 3) i;

\echo '=== ORCA-01: optimizer=on preserved across CALL refresh_continuous_aggregate ==='
SET optimizer = on;
SELECT current_setting('optimizer') AS optimizer_before;
SET client_min_messages TO warning;
CALL time_series.refresh_continuous_aggregate('cv_orca', NULL, NULL);
RESET client_min_messages;
SELECT current_setting('optimizer') AS optimizer_after_call;

\echo '=== ORCA-02: optimizer=on preserved across run_job ==='
SELECT add_continuous_aggregate_policy('cv_orca',
       '1 day'::interval, '0'::interval, '1 hour'::interval)
   AS orca_jid \gset
SET optimizer = on;
SELECT current_setting('optimizer') AS optimizer_before;
SET client_min_messages TO warning;
CALL time_series.run_job(:orca_jid);
RESET client_min_messages;
SELECT current_setting('optimizer') AS optimizer_after_run_job;

\echo '=== ORCA-03: optimizer=off caller stays off (no spurious flip-on) ==='
SET optimizer = off;
SET client_min_messages TO warning;
CALL time_series.refresh_continuous_aggregate('cv_orca', NULL, NULL);
RESET client_min_messages;
SELECT current_setting('optimizer') AS optimizer_after;

RESET optimizer;
SELECT remove_continuous_aggregate_policy('cv_orca');
DROP VIEW cv_orca;
DROP TABLE m_orca CASCADE;

DROP EXTENSION time_series CASCADE;
