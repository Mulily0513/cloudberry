/* contrib/time_series/time_series--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION time_series" to load this file. \quit

-- ============================================================
-- time_bucket: time bucketing functions (Apache 2.0 from TimescaleDB)
-- ============================================================

-- time_bucket(smallint, smallint)
CREATE FUNCTION time_bucket(bucket_width SMALLINT, ts SMALLINT)
RETURNS SMALLINT
AS 'MODULE_PATHNAME', 'ts_int16_bucket'
LANGUAGE C STRICT IMMUTABLE PARALLEL SAFE;

-- time_bucket(smallint, smallint, smallint)
CREATE FUNCTION time_bucket(bucket_width SMALLINT, ts SMALLINT, "offset" SMALLINT)
RETURNS SMALLINT
AS 'MODULE_PATHNAME', 'ts_int16_bucket'
LANGUAGE C STRICT IMMUTABLE PARALLEL SAFE;

-- time_bucket(int, int)
CREATE FUNCTION time_bucket(bucket_width INT, ts INT)
RETURNS INT
AS 'MODULE_PATHNAME', 'ts_int32_bucket'
LANGUAGE C STRICT IMMUTABLE PARALLEL SAFE;

-- time_bucket(int, int, int)
CREATE FUNCTION time_bucket(bucket_width INT, ts INT, "offset" INT)
RETURNS INT
AS 'MODULE_PATHNAME', 'ts_int32_bucket'
LANGUAGE C STRICT IMMUTABLE PARALLEL SAFE;

-- time_bucket(bigint, bigint)
CREATE FUNCTION time_bucket(bucket_width BIGINT, ts BIGINT)
RETURNS BIGINT
AS 'MODULE_PATHNAME', 'ts_int64_bucket'
LANGUAGE C STRICT IMMUTABLE PARALLEL SAFE;

-- time_bucket(bigint, bigint, bigint)
CREATE FUNCTION time_bucket(bucket_width BIGINT, ts BIGINT, "offset" BIGINT)
RETURNS BIGINT
AS 'MODULE_PATHNAME', 'ts_int64_bucket'
LANGUAGE C STRICT IMMUTABLE PARALLEL SAFE;

-- time_bucket(interval, timestamp)
CREATE FUNCTION time_bucket(bucket_width INTERVAL, ts TIMESTAMP)
RETURNS TIMESTAMP
AS 'MODULE_PATHNAME', 'ts_timestamp_bucket'
LANGUAGE C STRICT IMMUTABLE PARALLEL SAFE;

-- time_bucket(interval, timestamp, timestamp)
CREATE FUNCTION time_bucket(bucket_width INTERVAL, ts TIMESTAMP, origin TIMESTAMP)
RETURNS TIMESTAMP
AS 'MODULE_PATHNAME', 'ts_timestamp_bucket'
LANGUAGE C STRICT IMMUTABLE PARALLEL SAFE;

-- time_bucket(interval, timestamp, interval) -- offset variant
CREATE FUNCTION time_bucket(bucket_width INTERVAL, ts TIMESTAMP, "offset" INTERVAL)
RETURNS TIMESTAMP
AS 'MODULE_PATHNAME', 'ts_timestamp_offset_bucket'
LANGUAGE C STRICT IMMUTABLE PARALLEL SAFE;

-- time_bucket(interval, timestamptz)
CREATE FUNCTION time_bucket(bucket_width INTERVAL, ts TIMESTAMPTZ)
RETURNS TIMESTAMPTZ
AS 'MODULE_PATHNAME', 'ts_timestamptz_bucket'
LANGUAGE C STRICT IMMUTABLE PARALLEL SAFE;

-- time_bucket(interval, timestamptz, timestamptz)
CREATE FUNCTION time_bucket(bucket_width INTERVAL, ts TIMESTAMPTZ, origin TIMESTAMPTZ)
RETURNS TIMESTAMPTZ
AS 'MODULE_PATHNAME', 'ts_timestamptz_bucket'
LANGUAGE C STRICT IMMUTABLE PARALLEL SAFE;

-- time_bucket(interval, timestamptz, interval) -- offset variant
CREATE FUNCTION time_bucket(bucket_width INTERVAL, ts TIMESTAMPTZ, "offset" INTERVAL)
RETURNS TIMESTAMPTZ
AS 'MODULE_PATHNAME', 'ts_timestamptz_offset_bucket'
LANGUAGE C STRICT IMMUTABLE PARALLEL SAFE;

-- time_bucket(interval, timestamptz, text, timestamptz, interval) -- timezone variant
CREATE FUNCTION time_bucket(bucket_width INTERVAL, ts TIMESTAMPTZ, timezone TEXT,
                            origin TIMESTAMPTZ DEFAULT NULL, "offset" INTERVAL DEFAULT NULL)
RETURNS TIMESTAMPTZ
AS 'MODULE_PATHNAME', 'ts_timestamptz_timezone_bucket'
LANGUAGE C IMMUTABLE PARALLEL SAFE;

-- time_bucket(interval, date)
CREATE FUNCTION time_bucket(bucket_width INTERVAL, ts DATE)
RETURNS DATE
AS 'MODULE_PATHNAME', 'ts_date_bucket'
LANGUAGE C STRICT IMMUTABLE PARALLEL SAFE;

-- time_bucket(interval, date, date)
CREATE FUNCTION time_bucket(bucket_width INTERVAL, ts DATE, origin DATE)
RETURNS DATE
AS 'MODULE_PATHNAME', 'ts_date_bucket'
LANGUAGE C STRICT IMMUTABLE PARALLEL SAFE;

-- time_bucket(interval, date, interval) -- offset variant
CREATE FUNCTION time_bucket(bucket_width INTERVAL, ts DATE, "offset" INTERVAL)
RETURNS DATE
AS 'MODULE_PATHNAME', 'ts_date_offset_bucket'
LANGUAGE C STRICT IMMUTABLE PARALLEL SAFE;

-- ============================================================
-- time_bucket_gapfill: gap-filling time bucket functions
-- ============================================================

CREATE FUNCTION time_bucket_gapfill(bucket_width INTERVAL, ts TIMESTAMP,
                                     start TIMESTAMP DEFAULT NULL,
                                     finish TIMESTAMP DEFAULT NULL)
RETURNS TIMESTAMP
AS 'MODULE_PATHNAME', 'ht_gapfill_timestamp_bucket'
LANGUAGE C VOLATILE PARALLEL SAFE;

CREATE FUNCTION time_bucket_gapfill(bucket_width INTERVAL, ts TIMESTAMPTZ,
                                     start TIMESTAMPTZ DEFAULT NULL,
                                     finish TIMESTAMPTZ DEFAULT NULL)
RETURNS TIMESTAMPTZ
AS 'MODULE_PATHNAME', 'ht_gapfill_timestamptz_bucket'
LANGUAGE C VOLATILE PARALLEL SAFE;

CREATE FUNCTION time_bucket_gapfill(bucket_width SMALLINT, ts SMALLINT,
                                     start SMALLINT DEFAULT NULL,
                                     finish SMALLINT DEFAULT NULL)
RETURNS SMALLINT
AS 'MODULE_PATHNAME', 'ht_gapfill_int16_bucket'
LANGUAGE C VOLATILE PARALLEL SAFE;

CREATE FUNCTION time_bucket_gapfill(bucket_width INT, ts INT,
                                     start INT DEFAULT NULL,
                                     finish INT DEFAULT NULL)
RETURNS INT
AS 'MODULE_PATHNAME', 'ht_gapfill_int32_bucket'
LANGUAGE C VOLATILE PARALLEL SAFE;

CREATE FUNCTION time_bucket_gapfill(bucket_width BIGINT, ts BIGINT,
                                     start BIGINT DEFAULT NULL,
                                     finish BIGINT DEFAULT NULL)
RETURNS BIGINT
AS 'MODULE_PATHNAME', 'ht_gapfill_int64_bucket'
LANGUAGE C VOLATILE PARALLEL SAFE;

CREATE FUNCTION time_bucket_gapfill(bucket_width INTERVAL, ts DATE,
                                     start DATE DEFAULT NULL,
                                     finish DATE DEFAULT NULL)
RETURNS DATE
AS 'MODULE_PATHNAME', 'ht_gapfill_date_bucket'
LANGUAGE C VOLATILE PARALLEL SAFE;

CREATE FUNCTION time_bucket_gapfill(bucket_width INTERVAL, ts TIMESTAMPTZ,
                                     timezone TEXT,
                                     start TIMESTAMPTZ DEFAULT NULL,
                                     finish TIMESTAMPTZ DEFAULT NULL)
RETURNS TIMESTAMPTZ
AS 'MODULE_PATHNAME', 'ht_gapfill_timestamptz_timezone_bucket'
LANGUAGE C VOLATILE PARALLEL SAFE;

-- ============================================================
-- locf: last observation carried forward (gap fill marker)
-- ============================================================

CREATE FUNCTION locf(value ANYELEMENT)
RETURNS ANYELEMENT
AS 'MODULE_PATHNAME', 'ht_gapfill_marker'
LANGUAGE C VOLATILE PARALLEL SAFE;

-- ============================================================
-- interpolate: linear interpolation (gap fill marker)
-- ============================================================

CREATE FUNCTION interpolate(value SMALLINT)
RETURNS SMALLINT
AS 'MODULE_PATHNAME', 'ht_gapfill_marker'
LANGUAGE C VOLATILE PARALLEL SAFE;

CREATE FUNCTION interpolate(value INT)
RETURNS INT
AS 'MODULE_PATHNAME', 'ht_gapfill_marker'
LANGUAGE C VOLATILE PARALLEL SAFE;

CREATE FUNCTION interpolate(value BIGINT)
RETURNS BIGINT
AS 'MODULE_PATHNAME', 'ht_gapfill_marker'
LANGUAGE C VOLATILE PARALLEL SAFE;

CREATE FUNCTION interpolate(value REAL)
RETURNS REAL
AS 'MODULE_PATHNAME', 'ht_gapfill_marker'
LANGUAGE C VOLATILE PARALLEL SAFE;

CREATE FUNCTION interpolate(value DOUBLE PRECISION)
RETURNS DOUBLE PRECISION
AS 'MODULE_PATHNAME', 'ht_gapfill_marker'
LANGUAGE C VOLATILE PARALLEL SAFE;

CREATE FUNCTION interpolate(value NUMERIC)
RETURNS NUMERIC
AS 'MODULE_PATHNAME', 'ht_gapfill_marker'
LANGUAGE C VOLATILE PARALLEL SAFE;

-- ============================================================
-- Function comments for discoverability (\df+)
-- ============================================================

COMMENT ON FUNCTION time_bucket(SMALLINT, SMALLINT) IS
  'Bucket a smallint value into fixed-width intervals';
COMMENT ON FUNCTION time_bucket(SMALLINT, SMALLINT, SMALLINT) IS
  'Bucket a smallint value into fixed-width intervals with offset';
COMMENT ON FUNCTION time_bucket(INT, INT) IS
  'Bucket an integer value into fixed-width intervals';
COMMENT ON FUNCTION time_bucket(INT, INT, INT) IS
  'Bucket an integer value into fixed-width intervals with offset';
COMMENT ON FUNCTION time_bucket(BIGINT, BIGINT) IS
  'Bucket a bigint value into fixed-width intervals';
COMMENT ON FUNCTION time_bucket(BIGINT, BIGINT, BIGINT) IS
  'Bucket a bigint value into fixed-width intervals with offset';
COMMENT ON FUNCTION time_bucket(INTERVAL, TIMESTAMP) IS
  'Bucket a timestamp into fixed-width time intervals';
COMMENT ON FUNCTION time_bucket(INTERVAL, TIMESTAMP, TIMESTAMP) IS
  'Bucket a timestamp into fixed-width time intervals with custom origin';
COMMENT ON FUNCTION time_bucket(INTERVAL, TIMESTAMP, INTERVAL) IS
  'Bucket a timestamp into fixed-width time intervals with offset';
COMMENT ON FUNCTION time_bucket(INTERVAL, TIMESTAMPTZ) IS
  'Bucket a timestamptz into fixed-width time intervals';
COMMENT ON FUNCTION time_bucket(INTERVAL, TIMESTAMPTZ, TIMESTAMPTZ) IS
  'Bucket a timestamptz into fixed-width time intervals with custom origin';
COMMENT ON FUNCTION time_bucket(INTERVAL, TIMESTAMPTZ, INTERVAL) IS
  'Bucket a timestamptz into fixed-width time intervals with offset';
COMMENT ON FUNCTION time_bucket(INTERVAL, TIMESTAMPTZ, TEXT, TIMESTAMPTZ, INTERVAL) IS
  'Bucket a timestamptz into fixed-width time intervals with timezone, optional origin and offset';
COMMENT ON FUNCTION time_bucket(INTERVAL, DATE) IS
  'Bucket a date into fixed-width time intervals';
COMMENT ON FUNCTION time_bucket(INTERVAL, DATE, DATE) IS
  'Bucket a date into fixed-width time intervals with custom origin';
COMMENT ON FUNCTION time_bucket(INTERVAL, DATE, INTERVAL) IS
  'Bucket a date into fixed-width time intervals with offset';

COMMENT ON FUNCTION time_bucket_gapfill(INTERVAL, TIMESTAMP, TIMESTAMP, TIMESTAMP) IS
  'Bucket timestamps with automatic gap detection and synthetic row generation';
COMMENT ON FUNCTION time_bucket_gapfill(INTERVAL, TIMESTAMPTZ, TIMESTAMPTZ, TIMESTAMPTZ) IS
  'Bucket timestamptz values with automatic gap detection and synthetic row generation';
COMMENT ON FUNCTION time_bucket_gapfill(SMALLINT, SMALLINT, SMALLINT, SMALLINT) IS
  'Bucket smallint values with automatic gap detection and synthetic row generation';
COMMENT ON FUNCTION time_bucket_gapfill(INT, INT, INT, INT) IS
  'Bucket integer values with automatic gap detection and synthetic row generation';
COMMENT ON FUNCTION time_bucket_gapfill(BIGINT, BIGINT, BIGINT, BIGINT) IS
  'Bucket bigint values with automatic gap detection and synthetic row generation';
COMMENT ON FUNCTION time_bucket_gapfill(INTERVAL, DATE, DATE, DATE) IS
  'Bucket date values with automatic gap detection and synthetic row generation';
COMMENT ON FUNCTION time_bucket_gapfill(INTERVAL, TIMESTAMPTZ, TEXT, TIMESTAMPTZ, TIMESTAMPTZ) IS
  'Bucket timestamptz values with timezone-aware gap detection and synthetic row generation';

COMMENT ON FUNCTION locf(ANYELEMENT) IS
  'Last observation carried forward — fills gaps with the most recent non-NULL value';
COMMENT ON FUNCTION interpolate(SMALLINT) IS
  'Linear interpolation for smallint — fills gaps between known data points';
COMMENT ON FUNCTION interpolate(INT) IS
  'Linear interpolation for integer — fills gaps between known data points';
COMMENT ON FUNCTION interpolate(BIGINT) IS
  'Linear interpolation for bigint — fills gaps between known data points';
COMMENT ON FUNCTION interpolate(REAL) IS
  'Linear interpolation for real — fills gaps between known data points';
COMMENT ON FUNCTION interpolate(DOUBLE PRECISION) IS
  'Linear interpolation for double precision — fills gaps between known data points';
COMMENT ON FUNCTION interpolate(NUMERIC) IS
  'Linear interpolation for numeric — fills gaps between known data points';

-- =========================================================================
--  Continuous Aggregate (CAGG) Catalog Tables
-- =========================================================================

-- 1. continuous_agg — CAGG main registry (REPLICATED)
CREATE TABLE time_series.continuous_agg (
    cagg_id             SERIAL PRIMARY KEY,
    user_view_schema    name NOT NULL,
    user_view_name      name NOT NULL,
    source_table_schema name NOT NULL,
    source_table_name   name NOT NULL,
    source_table_oid    oid  NOT NULL,
    mat_table_schema    name NOT NULL DEFAULT '',
    mat_table_name      name NOT NULL DEFAULT '',
    partial_view_schema name NOT NULL DEFAULT '',
    partial_view_name   name NOT NULL DEFAULT '',
    direct_view_schema  name NOT NULL DEFAULT '',
    direct_view_name    name NOT NULL DEFAULT '',
    bucket_width        interval NOT NULL,
    bucket_column       name NOT NULL,
    materialized_only   bool NOT NULL DEFAULT false,
    created_at          timestamptz NOT NULL DEFAULT now()
) DISTRIBUTED REPLICATED;

-- 2. cagg_watermark — per-segment materialization progress (RANDOMLY)
CREATE TABLE time_series.cagg_watermark (
    cagg_id     int         NOT NULL,
    watermark   timestamptz NOT NULL
) DISTRIBUTED RANDOMLY;

-- 3. cagg_bucket_function — bucket parameters (REPLICATED)
CREATE TABLE time_series.cagg_bucket_function (
    cagg_id         int       PRIMARY KEY,
    bucket_func     text      DEFAULT 'time_bucket',
    bucket_width    interval  NOT NULL,
    bucket_origin   timestamptz,
    bucket_offset   interval,
    bucket_timezone text,
    time_type       oid       NOT NULL
) DISTRIBUTED REPLICATED;

-- 4. cagg_invalidation_log — L1 shared invalidation log (RANDOMLY)
CREATE TABLE time_series.cagg_invalidation_log (
    source_table_oid    oid         NOT NULL,
    lowest_modified     timestamptz NOT NULL,
    greatest_modified   timestamptz NOT NULL
) DISTRIBUTED RANDOMLY;

-- 5. cagg_materialization_log — L2 per-CAGG invalidation log (RANDOMLY)
CREATE TABLE time_series.cagg_materialization_log (
    cagg_id             int         NOT NULL,
    lowest_modified     timestamptz NOT NULL,
    greatest_modified   timestamptz NOT NULL
) DISTRIBUTED RANDOMLY;

-- NOTE: No index on L2 — _cagg_move_l1_to_l2 uses simple_heap_insert
-- which bypasses index maintenance.  L2 is consumed by REFRESH via
-- sequential scan with WHERE cagg_id = $1; rows are short-lived.

-- 5b. cagg_invalidation_threshold — per-source threshold (RANDOMLY)
-- Stores MAX(watermark) across all CAGGs on the same source table.
-- Pre-computed during REFRESH so trigger only needs one heap scan.
-- One row per source per segment (DISTRIBUTED RANDOMLY).
CREATE TABLE time_series.cagg_invalidation_threshold (
    source_table_oid    oid         NOT NULL,
    threshold           timestamptz NOT NULL DEFAULT '-infinity'
) DISTRIBUTED RANDOMLY;

-- NOTE: There is NO separate cagg_policy table.  Policy parameters
-- (schedule_interval / start_offset / end_offset / cagg_name) live
-- exclusively in bgw_job:
--   - bgw_job.schedule_interval     ── how often to fire
--   - bgw_job.config (jsonb)        ── start_offset / end_offset / cagg_name
--   - bgw_job.scheduled (bool)      ── active flag
--   - bgw_job.hypertable_id         ── cagg_id (column name kept from TSDB)
-- This matches TSDB's design (single source of truth).  The
-- cagg_policy_stats view (defined later) joins bgw_job ↔ continuous_agg
-- and exposes the jsonb fields as relational columns for convenience.

-- =========================================================================
--  BGW Job Scheduling Tables (generic framework, matches TSDB bgw_job)
-- =========================================================================

-- 7. bgw_job — Generic background job definitions (REPLICATED)
-- One row per scheduled job. CAGG refresh policies create rows here
-- with proc_name='ts_cagg_refresh_policy'.
CREATE SEQUENCE IF NOT EXISTS time_series.bgw_job_id_seq;

CREATE TABLE time_series.bgw_job (
    id                  int       NOT NULL DEFAULT nextval('time_series.bgw_job_id_seq'),
    application_name    name      NOT NULL,
    schedule_interval   interval  NOT NULL,
    max_runtime         interval  NOT NULL DEFAULT '0'::interval,
    max_retries         int       NOT NULL DEFAULT -1,
    retry_period        interval  NOT NULL DEFAULT '5 minutes'::interval,
    proc_schema         name      NOT NULL,
    proc_name           name      NOT NULL,
    owner               regrole   NOT NULL DEFAULT current_role::regrole,
    scheduled           bool      NOT NULL DEFAULT true,
    fixed_schedule      bool      NOT NULL DEFAULT true,
    initial_start       timestamptz,
    hypertable_id       int,
    config              jsonb,
    check_schema        name,
    check_name          name,
    timezone            text,
    CONSTRAINT bgw_job_pkey PRIMARY KEY (id)
) DISTRIBUTED REPLICATED;

-- 8. bgw_job_stat — Job execution statistics (REPLICATED)
-- One row per job, tracks runs/failures/crashes and next_start.
CREATE TABLE time_series.bgw_job_stat (
    job_id                  int       NOT NULL,
    last_start              timestamptz NOT NULL DEFAULT '-infinity',
    last_finish             timestamptz NOT NULL DEFAULT '-infinity',
    next_start              timestamptz NOT NULL DEFAULT '-infinity',
    last_successful_finish  timestamptz NOT NULL DEFAULT '-infinity',
    last_run_success        bool      NOT NULL DEFAULT true,
    total_runs              bigint    NOT NULL DEFAULT 0,
    total_duration          interval  NOT NULL DEFAULT '0'::interval,
    total_duration_failures interval  NOT NULL DEFAULT '0'::interval,
    total_successes         bigint    NOT NULL DEFAULT 0,
    total_failures          bigint    NOT NULL DEFAULT 0,
    total_crashes           bigint    NOT NULL DEFAULT 0,
    consecutive_failures    int       NOT NULL DEFAULT 0,
    consecutive_crashes     int       NOT NULL DEFAULT 0,
    flags                   int       NOT NULL DEFAULT 0,
    CONSTRAINT bgw_job_stat_pkey PRIMARY KEY (job_id),
    CONSTRAINT bgw_job_stat_job_id_fkey FOREIGN KEY (job_id)
        REFERENCES time_series.bgw_job(id) ON DELETE CASCADE
)
-- Hash-distributed on job_id (NOT REPLICATED).  REPLICATED writes
-- escalate to a table-level ExclusiveLock on every segment, which
-- bottlenecks every BGW worker to single-writer throughput.  Hash
-- distribution lets concurrent UPDATE/INSERT on different job_ids
-- proceed in parallel.  scheduler SELECTs gather across segments,
-- which for a small table is negligible.
DISTRIBUTED BY (job_id);

-- 9. bgw_job_stat_history — Per-execution audit log
-- Each row records one job invocation: start/finish, success, snapshot
-- of the job config and (on failure) the captured error data.
--
-- V1 always records every job execution (no opt-in GUC).  See
-- src/bgw/job_stat_history.c file header for why we don't follow
-- TSDB's optional/track-only-errors design.
--
-- The `data` JSONB column has the shape
--   {"job": {<snapshot of bgw_job row>}, "error_data": {<edata>}?}
-- so users can debug *which* version of the policy ran when, and
-- correlate failures with specific config changes.
CREATE SEQUENCE IF NOT EXISTS time_series.bgw_job_stat_history_id_seq;

CREATE TABLE time_series.bgw_job_stat_history (
    id                  bigint    NOT NULL DEFAULT
                            nextval('time_series.bgw_job_stat_history_id_seq'),
    job_id              int       NOT NULL,
    pid                 int,
    execution_start     timestamptz NOT NULL,
    execution_finish    timestamptz,
    succeeded           bool,
    data                jsonb
    -- No PK: an exclusive table lock on a REPLICATED table serializes
    -- every BGW worker INSERT (PG's relation-level ExclusiveLock for
    -- REPLICATED writes blocks parallel writers).  History writes are
    -- append-only audit data — distribute by id and let writes go to
    -- whichever segment owns the partition.  Queries scan all segments
    -- via standard MPP gather, no consistency issue.
) DISTRIBUTED BY (id);

CREATE INDEX bgw_job_stat_history_job_id_idx
    ON time_series.bgw_job_stat_history (job_id, execution_start DESC);

-- User-facing view: most useful columns extracted from `data` JSONB +
-- duration computed.  Excludes still-running rows (execution_finish IS
-- NULL).  Mirrors TSDB's timescaledb_information.job_history view —
-- including the owner filter, the security_barrier option, and the
-- "let the database owner see everything" carve-out:
--
--   * pg_has_role(current_user, owner, 'MEMBER') — job owner and
--     anyone in their role chain (incl. superuser) sees their rows.
--   * pg_has_role(current_user, <db owner>, 'MEMBER') — the DBA who
--     owns this database also sees every row, satisfying the typical
--     operational need of "let DBA inspect any job's history".
--   * WITH (security_barrier = true) — keep PG's optimizer from
--     pushing user-supplied predicates inside the WHERE clause, which
--     would let a malicious caller leak rows it shouldn't see via
--     side-effects of a custom function reading config / error_data
--     before the owner check has filtered the row out.
CREATE VIEW time_series.job_history
WITH (security_barrier = true) AS
SELECT h.id,
       h.job_id,
       h.pid,
       h.execution_start,
       h.execution_finish,
       h.execution_finish - h.execution_start AS duration,
       h.succeeded,
       -- A "ghost" row: mark_start ran (execution_start written) but
       -- mark_end never did (execution_finish stays NULL forever).
       -- Diagnostic: worker was SIGKILL'd / coordinator restarted /
       -- OOM-killer fired before the policy proc finished.  These rows
       -- have NULL data + NULL succeeded; surfacing the distinction lets
       -- operators tell "policy raised ERROR" apart from "process died".
       (h.execution_finish IS NULL AND h.execution_start IS NOT NULL)
                                             AS is_crashed,
       (h.data->'job'->>'proc_schema')::name AS proc_schema,
       (h.data->'job'->>'proc_name')::name   AS proc_name,
       h.data->'job'->'config'               AS config,
       h.data->'error_data'                  AS error_data
  FROM time_series.bgw_job_stat_history h
  LEFT JOIN time_series.bgw_job j ON j.id = h.job_id
 WHERE pg_catalog.pg_has_role(current_user::name,
                              (SELECT pg_catalog.pg_get_userbyid(datdba)
                                 FROM pg_catalog.pg_database
                                WHERE datname = current_database()),
                              'MEMBER') IS TRUE
    OR pg_catalog.pg_has_role(current_user::name, j.owner, 'MEMBER') IS TRUE;

-- bgw_job_stat_history grows unbounded by design: every job execution
-- appends one row, including a JSONB snapshot of the bgw_job + edata
-- on failure (commonly a few KB).  At a 1-minute schedule across 100
-- policies that's ~150 MB/day.  V1 has no built-in retention policy
-- (mirrors TSDB upstream's posture), so DBAs need to prune the table
-- themselves.  This helper does the prune in one statement, returning
-- the row count so cron / pg_cron can log progress.
--
-- Usage:
--   SELECT time_series.bgw_job_stat_history_purge('30 days');
--   -- → returns count of rows deleted with execution_finish older
--   --   than 30 days
--
-- Rows whose execution_finish is NULL (still running, or row written
-- by mark_start before mark_end) are deliberately preserved.
CREATE OR REPLACE FUNCTION time_series.bgw_job_stat_history_purge(
    p_older_than interval
) RETURNS bigint LANGUAGE plpgsql AS $$
DECLARE
    v_deleted bigint;
BEGIN
    IF p_older_than IS NULL OR p_older_than <= '0'::interval THEN
        RAISE EXCEPTION 'p_older_than must be a positive interval';
    END IF;

    -- Two clauses:
    --   (a) closed rows: execution_finish set by mark_end → compare it.
    --   (b) ghost rows:  worker crashed (SIGKILL / coordinator restart /
    --       OOM) before mark_end ran, so execution_finish stays NULL
    --       forever.  Without clause (b), every crash leaves a permanent
    --       row that retention can never delete — bgw_job_stat_history
    --       grows unbounded across long-stability runs even with the
    --       retention policy enabled.  Compare execution_start instead
    --       (set by mark_start before the policy proc fired).
    DELETE FROM time_series.bgw_job_stat_history
     WHERE (execution_finish IS NOT NULL
            AND execution_finish < now() - p_older_than)
        OR (execution_finish IS NULL
            AND execution_start  < now() - p_older_than);
    GET DIAGNOSTICS v_deleted = ROW_COUNT;
    RETURN v_deleted;
END $$;

-- Restrict to superuser by default (the table is hash-distributed +
-- contains every user's job history; an ordinary user purging it
-- could destroy other users' audit logs).  Operators can GRANT to
-- a dedicated maintenance role if needed.
REVOKE EXECUTE ON FUNCTION time_series.bgw_job_stat_history_purge(interval) FROM PUBLIC;

-- =========================================================================
--  CAGG Functions
-- =========================================================================

-- Row-level trigger function: writes dirty time ranges to L1 (cagg_insert.c)
CREATE FUNCTION time_series.cagg_invalidation_trigfn()
RETURNS trigger LANGUAGE C AS 'MODULE_PATHNAME', 'cagg_invalidation_trigfn';

-- Segment-local watermark initialization (called on each segment via
-- SELECT _cagg_init_segment_watermark(cagg_id) FROM gp_dist_random('gp_id'))
CREATE FUNCTION time_series._cagg_init_segment_watermark(cagg_id int)
RETURNS void LANGUAGE C AS 'MODULE_PATHNAME', 'cagg_init_segment_watermark';

-- Segment-local threshold initialization (called on each segment via
-- SELECT _cagg_init_segment_threshold(source_oid) FROM gp_dist_random('gp_id'))
CREATE FUNCTION time_series._cagg_init_segment_threshold(source_oid oid)
RETURNS void LANGUAGE C AS 'MODULE_PATHNAME', 'cagg_init_segment_threshold';

-- Segment-local L1 → L2 migration function (called internally by REFRESH;
-- dispatched to each segment via SELECT ... FROM cagg_watermark trick)
CREATE FUNCTION time_series._cagg_move_l1_to_l2(source_oid oid)
RETURNS void LANGUAGE C AS 'MODULE_PATHNAME', 'cagg_segment_move_l1_to_l2';

-- REFRESH procedure for continuous aggregates (cagg_refresh.c)
--
-- force=TRUE forces re-materialization of every bucket in [start, end)
-- even when L2 is empty / watermark already covers it.  Recovery
-- escape hatch for data drift (trigger missed an INSERT, mat table
-- got manipulated directly, etc.).  Mirrors TSDB
-- @extschema@.refresh_continuous_aggregate(..., force BOOLEAN = FALSE).
CREATE PROCEDURE time_series.refresh_continuous_aggregate(
    cagg_name text,
    window_start timestamptz DEFAULT NULL,
    window_end   timestamptz DEFAULT NULL,
    force        boolean     DEFAULT false
) LANGUAGE C AS 'MODULE_PATHNAME', 'cagg_refresh';

-- Watermark function: returns per-segment watermark for real-time UNION ALL.
--
-- Implemented in C with direct heap scan (no SPI) because:
--   1. CBDB segment QEs cannot execute SPI on distributed tables
--      (cagg_watermark is DISTRIBUTED RANDOMLY).
--   2. LANGUAGE SQL gets inlined + constant-folded by eval_const_expressions.
--   3. LANGUAGE plpgsql uses SPI internally → same QE restriction.
--
-- MUST be VOLATILE (not STABLE) to prevent eval_const_expressions from
-- evaluating it at plan time on QD.  VOLATILE guarantees each segment's
-- executor calls it at runtime, reading the LOCAL cagg_watermark row.
CREATE FUNCTION time_series.cagg_watermark(cagg_id int)
RETURNS timestamptz LANGUAGE C VOLATILE
AS 'MODULE_PATHNAME', 'cagg_watermark_fn';

-- ============================================================
-- materialized_only mode toggle
--
-- User-facing syntax:
--   ALTER VIEW cv_name SET (time_series.materialized_only = true);
--
-- The ALTER VIEW command is intercepted by the ProcessUtility hook
-- (cagg_create.c) which runs the toggle logic in C via SPI.  No public
-- function or procedure is exposed.
-- ============================================================

-- ============================================================
-- CAGG cleanup (event trigger)
--
-- Handles two DROP scenarios:
--
-- 1. DROP TABLE source_table CASCADE — PostgreSQL only cascades to objects
--    that hold an explicit pg_depend reference to the source; partial/direct
--    views get dropped, but the user view, mat table, and catalog rows do
--    NOT.  We match by source_table_oid.
--
-- 2. DROP VIEW user_view CASCADE — the user drops the CAGG user view
--    directly.  We match by (schema, name) against continuous_agg and
--    clean up the mat table, catalog rows, and source-table trigger.
--
-- ============================================================
CREATE FUNCTION time_series.cagg_handle_source_drop()
RETURNS event_trigger LANGUAGE plpgsql AS $$
DECLARE
    obj      record;
    cagg_rec record;
    other_count int;
BEGIN
    FOR obj IN
        SELECT objid, object_type, schema_name, object_name
        FROM pg_event_trigger_dropped_objects()
        WHERE object_type IN ('table', 'view')
    LOOP
        IF obj.object_type = 'table' THEN
            -- Source table dropped: clean up all CAGGs that reference it
            FOR cagg_rec IN
                SELECT * FROM time_series.continuous_agg
                WHERE source_table_oid = obj.objid
            LOOP
                EXECUTE format('DROP VIEW IF EXISTS %I.%I CASCADE',
                    cagg_rec.user_view_schema, cagg_rec.user_view_name);
                EXECUTE format('DROP TABLE IF EXISTS %I.%I CASCADE',
                    cagg_rec.mat_table_schema, cagg_rec.mat_table_name);
                PERFORM time_series._cagg_cleanup_catalog(cagg_rec.cagg_id,
                    cagg_rec.source_table_oid);
            END LOOP;

        ELSIF obj.object_type = 'view' THEN
            -- User view dropped: clean up matching CAGG
            FOR cagg_rec IN
                SELECT * FROM time_series.continuous_agg
                WHERE user_view_schema = obj.schema_name
                  AND user_view_name   = obj.object_name
            LOOP
                -- Drop internal views (they depend on source table, not user view,
                -- so CASCADE from DROP VIEW user_view does NOT reach them)
                EXECUTE format('DROP VIEW IF EXISTS %I.%I CASCADE',
                    cagg_rec.partial_view_schema, cagg_rec.partial_view_name);
                EXECUTE format('DROP VIEW IF EXISTS %I.%I CASCADE',
                    cagg_rec.direct_view_schema, cagg_rec.direct_view_name);
                EXECUTE format('DROP TABLE IF EXISTS %I.%I CASCADE',
                    cagg_rec.mat_table_schema, cagg_rec.mat_table_name);
                PERFORM time_series._cagg_cleanup_catalog(cagg_rec.cagg_id,
                    cagg_rec.source_table_oid);
            END LOOP;
        END IF;
    END LOOP;
END
$$;

-- Helper: clean catalog rows and optionally remove source trigger
CREATE FUNCTION time_series._cagg_cleanup_catalog(
    p_cagg_id int, p_source_oid oid
) RETURNS void LANGUAGE plpgsql AS $$
DECLARE
    other_count int;
BEGIN
    DELETE FROM time_series.cagg_watermark
        WHERE cagg_id = p_cagg_id;
    DELETE FROM time_series.cagg_bucket_function
        WHERE cagg_id = p_cagg_id;
    DELETE FROM time_series.cagg_invalidation_log
        WHERE source_table_oid = p_source_oid;
    DELETE FROM time_series.cagg_materialization_log
        WHERE cagg_id = p_cagg_id;
    -- Delete BGW jobs associated with this CAGG (cascades to bgw_job_stat)
    DELETE FROM time_series.bgw_job
        WHERE hypertable_id = p_cagg_id;
    DELETE FROM time_series.continuous_agg
        WHERE cagg_id = p_cagg_id;

    -- Remove trigger from source table if no other CAGGs reference it
    SELECT count(*) INTO other_count
    FROM time_series.continuous_agg
    WHERE source_table_oid = p_source_oid;

    IF other_count = 0 AND p_source_oid IS NOT NULL THEN
        -- Last CAGG on this source → clean up threshold rows
        DELETE FROM time_series.cagg_invalidation_threshold
            WHERE source_table_oid = p_source_oid;

        -- Only attempt DROP TRIGGER if source table still exists
        IF EXISTS (SELECT 1 FROM pg_class WHERE oid = p_source_oid) THEN
            EXECUTE format(
                'DROP TRIGGER IF EXISTS ts_cagg_invalidation_trigger ON %s',
                p_source_oid::regclass);
        END IF;
    ELSIF other_count > 0 AND p_source_oid IS NOT NULL THEN
        -- Other CAGGs remain → recalculate threshold from remaining watermarks
        -- (the dropped CAGG may have had the highest watermark)
        UPDATE time_series.cagg_invalidation_threshold
        SET threshold = COALESCE((
            SELECT MAX(w.watermark)
            FROM time_series.cagg_watermark w
            JOIN time_series.continuous_agg c ON w.cagg_id = c.cagg_id
            WHERE c.source_table_oid = p_source_oid
        ), '-infinity'::timestamptz)
        WHERE source_table_oid = p_source_oid;
    END IF;
END
$$;

CREATE EVENT TRIGGER cagg_source_drop_handler
    ON sql_drop
    EXECUTE FUNCTION time_series.cagg_handle_source_drop();

-- ============================================================
-- Note: RENAME COLUMN of a CAGG bucket_column is handled by the
-- ProcessUtility hook in cagg_create.c — it auto-updates the
-- continuous_agg.bucket_column registry to follow the rename
-- (more user-friendly than blocking).  DROP COLUMN and
-- ALTER COLUMN TYPE on the bucket column are blocked by the
-- same hook because they cannot be safely followed.
-- ============================================================

-- ============================================================
-- TRUNCATE invalidation: handled via ProcessUtility hook in
-- cagg_create.c (not via triggers).  CBDB blocks both STATEMENT
-- triggers and event triggers for TRUNCATE, so the hook intercepts
-- TruncateStmt on QD and writes {-infinity, +infinity} to L1
-- before passing through to the standard TRUNCATE handler.
-- ============================================================

-- =========================================================================
--  BGW Functions
-- =========================================================================

-- BGW job worker entry point (called by dynamic background workers)
CREATE FUNCTION time_series.ts_bgw_job_entrypoint(int4)
RETURNS void LANGUAGE C AS 'MODULE_PATHNAME', 'ts_bgw_job_entrypoint';

-- BGW scheduler main (called by the static background worker)
CREATE FUNCTION time_series.ts_bgw_scheduler_main(int4)
RETURNS void LANGUAGE C AS 'MODULE_PATHNAME', 'ts_bgw_scheduler_main';

-- CAGG refresh policy procedure (called by BGW workers)
CREATE PROCEDURE time_series.ts_policy_refresh_cagg(job_id int, config jsonb)
LANGUAGE C AS 'MODULE_PATHNAME', 'ts_policy_refresh_cagg';

-- Broadcast a relcache invalidation for bgw_job so the scheduler reloads
-- its job list on the next iteration.  Called from add/remove/alter after
-- INSERT/UPDATE/DELETE on bgw_job.
CREATE FUNCTION time_series.bgw_invalidate_cache()
RETURNS void LANGUAGE C AS 'MODULE_PATHNAME', 'ts_bgw_invalidate_cache';
-- Note: deliberately NOT REVOKE-d from PUBLIC.  add/remove/alter
-- policy plpgsql functions run SECURITY INVOKER and PERFORM this
-- inside their own body — revoking PUBLIC EXECUTE would break the
-- normal user-driven call path.  Theoretical DoS surface (call in
-- a tight loop to flood the SI queue) exists but is not worse than
-- equivalent direct catalog DML the same user could already perform.

-- =========================================================================
--  CAGG Policy Management API
-- =========================================================================

-- Resolve a possibly-unqualified continuous-aggregate name into a
-- single (cagg_id, schema, name) row using PG's search_path semantics.
--
-- Earlier versions did `WHERE user_view_name = cagg_name` which is
-- ambiguous when two different schemas hold a CAGG with the same view
-- name — SELECT INTO would pick whichever row sorted first, silently
-- targeting the wrong CAGG.  Route the name through to_regclass so PG
-- itself applies the search_path resolution rules (consistent with
-- every other SQL command that references a relation).  Returns NULL
-- in v_cagg_id if the name doesn't resolve to a known CAGG; callers
-- decide whether that's an error or a no-op.
CREATE OR REPLACE FUNCTION time_series._resolve_cagg_id(p_cagg_name text)
RETURNS int LANGUAGE plpgsql AS $$
DECLARE
    v_view_oid    oid;
    v_view_schema name;
    v_view_name   name;
    v_cagg_id     int;
BEGIN
    IF p_cagg_name IS NULL OR p_cagg_name = '' THEN
        RAISE EXCEPTION 'cagg_name must be non-empty';
    END IF;

    -- to_regclass: schema-qualified names resolve directly; bare names
    -- resolve via search_path; non-existent names return NULL (no
    -- ERROR — we want a clean caller-driven message instead).
    v_view_oid := to_regclass(p_cagg_name)::oid;
    IF v_view_oid IS NULL THEN
        RETURN NULL;
    END IF;

    SELECT n.nspname, c.relname INTO v_view_schema, v_view_name
      FROM pg_class c JOIN pg_namespace n ON c.relnamespace = n.oid
     WHERE c.oid = v_view_oid;

    SELECT cagg_id INTO v_cagg_id
      FROM time_series.continuous_agg
     WHERE user_view_schema = v_view_schema
       AND user_view_name   = v_view_name;

    RETURN v_cagg_id;
END $$;

-- Shared validation for ts_policy_refresh_cagg config.  Used by both
-- add_continuous_aggregate_policy (which builds the config) and alter_job
-- (which lets the user replace it wholesale).  Without this, alter_job
-- would silently accept ghost cagg_names, empty/inverted refresh windows,
-- and windows narrower than 2 × bucket_width — letting users break a
-- working policy without any feedback until the BGW worker fails to run.
CREATE OR REPLACE FUNCTION time_series._validate_cagg_policy_config(
    p_cagg_name      text,
    p_start_offset   interval,
    p_end_offset     interval
) RETURNS void LANGUAGE plpgsql AS $$
DECLARE
    v_cagg_id      int;
    v_bucket_width interval;
BEGIN
    -- 1. cagg_name resolves to an existing CAGG (search_path-aware)
    v_cagg_id := time_series._resolve_cagg_id(p_cagg_name);
    IF v_cagg_id IS NULL THEN
        RAISE EXCEPTION 'continuous aggregate "%" does not exist', p_cagg_name;
    END IF;

    -- 2. fetch bucket_width for the resolved cagg
    SELECT bucket_width INTO v_bucket_width
      FROM time_series.continuous_agg WHERE cagg_id = v_cagg_id;

    -- 3. start_offset > end_offset (window non-empty)
    IF p_start_offset IS NOT NULL AND p_end_offset IS NOT NULL
       AND p_start_offset <= p_end_offset THEN
        RAISE EXCEPTION
            'start_offset must be greater than end_offset (refresh window must be non-empty)'
            USING HINT = 'Set start_offset to an earlier time than end_offset, '
                         'e.g. start_offset => ''2 days'', end_offset => ''0''.';
    END IF;

    -- 4. window covers at least 2 buckets
    IF p_start_offset IS NOT NULL AND p_end_offset IS NOT NULL
       AND v_bucket_width IS NOT NULL
       AND (p_start_offset - p_end_offset) < (v_bucket_width * 2) THEN
        RAISE EXCEPTION
            'refresh window (%) must cover at least 2 buckets (2 × % = %)',
            p_start_offset - p_end_offset,
            v_bucket_width,
            v_bucket_width * 2
            USING HINT = 'Increase start_offset or decrease end_offset '
                         'so the window spans more than two buckets.';
    END IF;
END $$;

CREATE OR REPLACE FUNCTION time_series.add_continuous_aggregate_policy(
    cagg_name           text,
    start_offset        interval,
    end_offset          interval,
    schedule_interval   interval,
    if_not_exists       bool        DEFAULT false,
    initial_start       timestamptz DEFAULT NULL,
    timezone            text        DEFAULT NULL
) RETURNS int AS $$
DECLARE
    v_cagg_id   int;
    v_job_id    int;
    v_app_name  text;
    v_config    jsonb;
BEGIN
    -- Look up CAGG (schema-qualified via to_regclass to disambiguate
    -- same-named CAGGs in different schemas — see _resolve_cagg_id).
    v_cagg_id := time_series._resolve_cagg_id(cagg_name);

    IF v_cagg_id IS NULL THEN
        RAISE EXCEPTION 'continuous aggregate "%" does not exist', cagg_name;
    END IF;

    -- Owner check: caller must be a member of the CAGG view's owner role
    -- (which transitively includes the owner itself and superuser).
    -- Without this, in a multi-tenant setup where the DBA has granted
    -- table-level INSERT/UPDATE/DELETE on bgw_job + bgw_job_stat to PUBLIC
    -- (a typical "let users manage their own policies" configuration), an
    -- unrelated user could squat on another user's CAGG by adding a policy
    -- before its real owner does — taking ownership of the policy and
    -- blocking the real owner (one-policy-per-CAGG) from adding their own.
    -- Mirrors the owner check in alter_job / remove_continuous_aggregate_policy
    -- and the C-side ts_bgw_job_permission_check.
    DECLARE
        v_view_schema name;
        v_view_name   name;
        v_view_owner  regrole;
    BEGIN
        SELECT user_view_schema, user_view_name
          INTO v_view_schema, v_view_name
          FROM time_series.continuous_agg WHERE cagg_id = v_cagg_id;

        SELECT relowner::regrole INTO v_view_owner
          FROM pg_class c JOIN pg_namespace n ON c.relnamespace = n.oid
         WHERE n.nspname = v_view_schema AND c.relname = v_view_name;

        IF NOT pg_has_role(current_user::name, v_view_owner, 'MEMBER') THEN
            RAISE EXCEPTION USING
                ERRCODE = 'insufficient_privilege',
                MESSAGE = format('insufficient permissions to add policy '
                                 'on continuous aggregate "%s"', cagg_name),
                DETAIL  = format('Continuous aggregate is owned by role "%s" '
                                 'but user "%s" does not belong to that role.',
                                 v_view_owner, current_user);
        END IF;
    END;

    -- Validate offset semantics: when both are non-NULL, the refresh window
    -- spans (now() - start_offset, now() - end_offset], so we require
    -- start_offset > end_offset (i.e. start_time < end_time).  Mirrors
    -- TimescaleDB's add_continuous_aggregate_policy validation.
    IF start_offset IS NOT NULL AND end_offset IS NOT NULL
       AND start_offset <= end_offset THEN
        RAISE EXCEPTION
            'start_offset must be greater than end_offset (refresh window must be non-empty)'
            USING HINT = 'Set start_offset to an earlier time than end_offset, '
                         'e.g. start_offset => ''2 days'', end_offset => ''0''.';
    END IF;

    -- Validate refresh window covers at least 2 buckets when both offsets
    -- are non-NULL.  A window smaller than 2*bucket_width can refresh at
    -- most one bucket and is almost always a configuration mistake.
    -- Mirrors TimescaleDB's "refresh window too small" check
    -- (cagg_policy.sql L118-119: rejects ('mat_m1', 11, 10, '1h')).
    --
    -- Rules:
    --   either offset NULL  → open window, allowed (no validation)
    --   start - end >= 2 * bucket_width → accept
    --   start - end <  2 * bucket_width → reject
    IF start_offset IS NOT NULL AND end_offset IS NOT NULL THEN
        DECLARE
            v_bucket_width interval;
        BEGIN
            SELECT bucket_width INTO v_bucket_width
              FROM time_series.continuous_agg
             WHERE cagg_id = v_cagg_id;

            IF v_bucket_width IS NOT NULL
               AND (start_offset - end_offset) < (v_bucket_width * 2) THEN
                RAISE EXCEPTION
                    'refresh window (%) must cover at least 2 buckets '
                    '(2 × % = %)',
                    start_offset - end_offset,
                    v_bucket_width,
                    v_bucket_width * 2
                    USING HINT = 'Increase start_offset or decrease end_offset '
                                 'so the window spans more than two buckets.';
            END IF;
        END;
    END IF;

    -- Validate timezone string by attempting to set it.  Mirrors
    -- TimescaleDB's "invalid timezone" check in policy_utils.c — gives a
    -- clear error at policy-creation time instead of failing later inside
    -- the BGW worker when the scheduler tries to compute next_start.
    IF timezone IS NOT NULL THEN
        BEGIN
            PERFORM set_config('timezone', timezone, true);
        EXCEPTION WHEN OTHERS THEN
            RAISE EXCEPTION 'invalid timezone "%"', timezone
                USING HINT = 'Pick a value from pg_timezone_names.';
        END;
    END IF;

    -- Check for existing policy (one policy per CAGG)
    SELECT id INTO v_job_id
    FROM time_series.bgw_job
    WHERE hypertable_id = v_cagg_id
      AND proc_name = 'ts_policy_refresh_cagg';

    IF v_job_id IS NOT NULL THEN
        IF if_not_exists THEN
            RAISE NOTICE 'policy already exists for "%", skipping', cagg_name;
            RETURN v_job_id;
        END IF;
        RAISE EXCEPTION 'refresh policy already exists for continuous aggregate "%"', cagg_name;
    END IF;

    -- Build JSONB config
    v_config := jsonb_build_object(
        'cagg_name', cagg_name,
        'start_offset', start_offset::text,
        'end_offset', end_offset::text
    );

    -- Insert job
    v_app_name := 'Refresh CAGG Policy [' || cagg_name || ']';
    -- max_runtime / max_retries default to TimescaleDB-compatible values
    -- ('0' = no timeout, -1 = unlimited retries) but operators can
    -- override via the time_series.cagg_default_max_runtime and
    -- time_series.cagg_default_max_retries GUCs (see scheduler.c).
    --
    -- initial_start / timezone:
    --   - NULL initial_start  → anchor to now() so the first run fires
    --     immediately (legacy behavior).
    --   - non-NULL            → align fixed-schedule ticks at that anchor.
    --   - timezone is honored by the scheduler's next_start computation
    --     (see job_stat.c: ts_calculate_next_start_on_success/failure).
    INSERT INTO time_series.bgw_job
        (application_name, schedule_interval, max_runtime, max_retries,
         retry_period, proc_schema, proc_name, scheduled, fixed_schedule,
         initial_start, hypertable_id, config, timezone)
    VALUES
        (v_app_name::name,
         schedule_interval,
         current_setting('time_series.cagg_default_max_runtime')::interval,
         current_setting('time_series.cagg_default_max_retries')::int,
         '5 minutes'::interval, 'time_series'::name, 'ts_policy_refresh_cagg'::name,
         true, true,
         COALESCE(initial_start, now()), v_cagg_id, v_config, timezone)
    RETURNING id INTO v_job_id;

    -- Initialize job stat.  When the caller provided an explicit
    -- initial_start in the future, honor it so the worker doesn't fire
    -- immediately; otherwise default to now() to mirror the legacy
    -- "fire on first tick" behavior.
    INSERT INTO time_series.bgw_job_stat (job_id, next_start)
    VALUES (v_job_id, COALESCE(initial_start, now()));

    -- Broadcast relcache invalidation so the scheduler reloads on next tick
    PERFORM time_series.bgw_invalidate_cache();

    RETURN v_job_id;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION time_series.remove_continuous_aggregate_policy(
    cagg_name   text,
    if_exists   bool DEFAULT false
) RETURNS void AS $$
DECLARE
    v_cagg_id   int;
    v_job_id    int;
BEGIN
    -- Resolve via search_path-aware helper (see _resolve_cagg_id).
    v_cagg_id := time_series._resolve_cagg_id(cagg_name);

    IF v_cagg_id IS NULL THEN
        IF if_exists THEN RETURN; END IF;
        RAISE EXCEPTION 'continuous aggregate "%" does not exist', cagg_name;
    END IF;

    DECLARE
        v_owner regrole;
    BEGIN
        SELECT id, owner INTO v_job_id, v_owner
        FROM time_series.bgw_job
        WHERE hypertable_id = v_cagg_id
          AND proc_name = 'ts_policy_refresh_cagg';

        IF v_job_id IS NULL THEN
            IF if_exists THEN RETURN; END IF;
            RAISE EXCEPTION 'no refresh policy exists for continuous aggregate "%"', cagg_name;
        END IF;

        -- Owner check: caller must be a member of the policy's owner role.
        IF NOT pg_has_role(current_user::name, v_owner, 'MEMBER') THEN
            RAISE EXCEPTION USING
                ERRCODE = 'insufficient_privilege',
                MESSAGE = format('insufficient permissions to remove policy on continuous aggregate "%s"', cagg_name),
                DETAIL  = format('Policy is owned by role "%s" but user "%s" does not belong to that role.',
                                 v_owner, current_user);
        END IF;
    END;

    -- Delete stat first (FK), then job
    DELETE FROM time_series.bgw_job_stat WHERE job_id = v_job_id;
    DELETE FROM time_series.bgw_job WHERE id = v_job_id;

    -- Broadcast relcache invalidation so the scheduler drops the removed job
    PERFORM time_series.bgw_invalidate_cache();
END;
$$ LANGUAGE plpgsql;

-- =========================================================================
--  Job Stat History Retention Policy (built-in)
-- =========================================================================
--
-- 1:1 port of TSDB sql/job_stat_history_log_retention.sql.
--
-- Design points (verbatim from TSDB):
--   - Built-in policy registered at extension install (id = 1) — there is
--     no add_*/remove_* helper.  Operators tune it via alter_job(1, ...).
--   - policy proc is a FUNCTION RETURNS integer (deleted row count), not
--     a PROCEDURE.  Generic dispatcher in ts_bgw_job_execute_real handles
--     both prokinds.
--   - Separate _check function validates config and is wired through
--     bgw_job.check_schema/check_name (see TSDB tsl/src/bgw_policy/job.c
--     ::policy_invoke_check).
--   - SET search_path TO pg_catalog, pg_temp inside both functions —
--     standard PG hardening so an attacker who controls search_path on
--     a calling session can't shadow built-in functions.
CREATE OR REPLACE FUNCTION time_series.policy_job_stat_history_retention(
    job_id integer, config jsonb) RETURNS integer
LANGUAGE plpgsql AS
$BODY$
DECLARE
    drop_after INTERVAL;
    numrows INTEGER;
BEGIN
    drop_after := config->>'drop_after';

    -- Delete BOTH closed rows (execution_finish set by mark_end) AND
    -- ghost rows (worker crashed mid-run; execution_finish stays NULL
    -- forever).  TSDB's upstream retention only checks execution_finish,
    -- which leaves crashed-run rows permanently in the table — a real
    -- bug under sustained operation when coordinator restarts / OOM
    -- kills accumulate over time.  We diverge from TSDB here on purpose;
    -- the divergence is a single OR clause and is documented inline.
    DELETE
    FROM time_series.bgw_job_stat_history
    WHERE (execution_finish < (now() - drop_after))
       OR (execution_finish IS NULL
           AND execution_start < (now() - drop_after));

    GET DIAGNOSTICS numrows = ROW_COUNT;

    RETURN numrows;
END;
$BODY$ SET search_path TO pg_catalog, pg_temp;

CREATE OR REPLACE FUNCTION time_series.policy_job_stat_history_retention_check(
    config jsonb) RETURNS VOID
LANGUAGE plpgsql AS
$BODY$
BEGIN
    IF config IS NULL THEN
        RAISE EXCEPTION 'config cannot be NULL, and must contain drop_after';
    END IF;

    IF config->>'drop_after' IS NULL THEN
        RAISE EXCEPTION 'drop_after interval not provided';
    END IF;
END;
$BODY$ SET search_path TO pg_catalog, pg_temp;

-- Insert the singleton retention job at install time.  ON CONFLICT (id)
-- DO NOTHING is the idempotency guard for re-installs / future upgrade
-- migrations (mirrors TSDB).
INSERT INTO time_series.bgw_job (
    id,
    application_name,
    schedule_interval,
    max_runtime,
    max_retries,
    retry_period,
    proc_schema,
    proc_name,
    owner,
    scheduled,
    config,
    check_schema,
    check_name,
    fixed_schedule,
    initial_start
)
VALUES
(
    1,
    'Job History Log Retention Policy [1]',
    INTERVAL '1 month',
    INTERVAL '1 hour',
    -1,
    INTERVAL '1h',
    'time_series',
    'policy_job_stat_history_retention',
    pg_catalog.quote_ident(current_role)::regrole,
    true,
    '{"drop_after":"1 month"}',
    'time_series',
    'policy_job_stat_history_retention_check',
    true,
    '2000-01-01 00:00:00+00'::timestamptz
) ON CONFLICT (id) DO NOTHING;

-- Advance the user-job sequence past the reserved built-in id range so
-- nextval() doesn't collide with the id=1 retention job.  Safe even on
-- re-install: setval is idempotent w.r.t. is_called=true.
SELECT pg_catalog.setval('time_series.bgw_job_id_seq', 1000, true);

-- Policy status view — analogous to TSDB's timescaledb_information.job_stats.
--
-- Intentionally NOT filtered by ownership.  TSDB's job_stats / jobs /
-- continuous_aggregates views are public to anyone with SELECT, on the
-- design judgment that policy metadata (cagg_name, schedule, run counts)
-- is operational status info, not sensitive data.  job_history /
-- job_errors are the views that DO filter, because they expose config
-- jsonb and error messages.  We mirror that split: this view is
-- unfiltered; job_history above filters by owner + database owner.
CREATE VIEW time_series.cagg_policy_stats AS
SELECT ca.user_view_schema || '.' || ca.user_view_name AS cagg_name,
       j.id AS job_id,
       j.schedule_interval,
       j.max_runtime,
       j.max_retries,
       j.config->>'start_offset' AS start_offset,
       j.config->>'end_offset' AS end_offset,
       j.scheduled AS active,
       s.last_start,
       s.last_finish,
       s.last_successful_finish,
       s.next_start,
       s.last_run_success,
       s.total_runs,
       s.total_successes,
       s.total_failures,
       s.total_crashes,
       s.consecutive_failures
FROM time_series.bgw_job j
JOIN time_series.continuous_agg ca ON ca.cagg_id = j.hypertable_id
LEFT JOIN time_series.bgw_job_stat s ON s.job_id = j.id
WHERE j.proc_name = 'ts_policy_refresh_cagg';

-- =========================================================================
--  continuous_aggregates: per-CAGG overview view
--
--  Mirrors TimescaleDB's timescaledb_information.continuous_aggregates.
--  One row per CAGG with all the diagnostic dimensions a DBA needs:
--    * Identity (schema, name, source table)
--    * Bucketing parameters (width, origin, offset, timezone)
--    * Watermark range (min / max across segments — uneven across MPP
--      segments would indicate L1->L2 migration drift)
--    * Real-time mode (materialized_only)
--    * Refresh policy (active or not, schedule, last/next run)
--
--  The watermark min/max comes from cagg_watermark, which is per-segment
--  in CBDB; min<max means some segments are behind, useful for spotting
--  partial-refresh issues.  TSDB's single-node version reports a single
--  watermark — we expose both bounds so MPP-specific drift is visible.
-- =========================================================================
CREATE VIEW time_series.continuous_aggregates
WITH (security_barrier = true) AS
SELECT ca.user_view_schema::name      AS view_schema,
       ca.user_view_name::name        AS view_name,
       ca.user_view_schema || '.' || ca.user_view_name AS view_full_name,
       ca.cagg_id,
       (ca.source_table_oid::regclass)::text AS source_table,
       ca.bucket_column,
       ca.materialized_only,
       ca.created_at,
       bf.bucket_width,
       bf.bucket_origin,
       bf.bucket_offset,
       bf.bucket_timezone,
       (SELECT min(watermark) FROM time_series.cagg_watermark
         WHERE cagg_id = ca.cagg_id)  AS min_watermark,
       (SELECT max(watermark) FROM time_series.cagg_watermark
         WHERE cagg_id = ca.cagg_id)  AS max_watermark,
       j.id                            AS policy_job_id,
       j.scheduled                     AS policy_active,
       j.schedule_interval             AS policy_schedule_interval,
       (j.config->>'start_offset')::interval AS policy_start_offset,
       (j.config->>'end_offset')::interval   AS policy_end_offset,
       s.last_start                    AS policy_last_start,
       s.last_finish                   AS policy_last_finish,
       s.last_successful_finish        AS policy_last_successful_finish,
       s.next_start                    AS policy_next_start,
       s.last_run_success              AS policy_last_run_success,
       s.total_runs                    AS policy_total_runs,
       s.total_successes               AS policy_total_successes,
       s.total_failures                AS policy_total_failures,
       s.consecutive_failures          AS policy_consecutive_failures
  FROM time_series.continuous_agg ca
  LEFT JOIN time_series.cagg_bucket_function bf
         ON bf.cagg_id = ca.cagg_id
  LEFT JOIN time_series.bgw_job j
         ON j.hypertable_id = ca.cagg_id
        AND j.proc_name = 'ts_policy_refresh_cagg'
  LEFT JOIN time_series.bgw_job_stat s
         ON s.job_id = j.id
 WHERE pg_catalog.pg_has_role(current_user::name,
                              (SELECT pg_catalog.pg_get_userbyid(datdba)
                                 FROM pg_catalog.pg_database
                                WHERE datname = current_database()),
                              'MEMBER') IS TRUE
    OR pg_catalog.has_table_privilege(current_user::name,
                                      ca.source_table_oid,
                                      'SELECT') IS TRUE;

GRANT SELECT ON time_series.continuous_aggregates TO PUBLIC;

-- =========================================================================
--  job_errors: convenience view filtering job_history to failures only
--
--  Mirrors TimescaleDB's timescaledb_information.job_errors.  Equivalent
--  to `SELECT * FROM job_history WHERE NOT succeeded` but gives DBAs a
--  more discoverable name and pulls out the most useful error fields
--  to top level so they don't have to ->> through JSONB by hand.
-- =========================================================================
CREATE VIEW time_series.job_errors
WITH (security_barrier = true) AS
SELECT h.id,
       h.job_id,
       h.proc_schema,
       h.proc_name,
       h.pid,
       h.execution_start    AS start_time,
       h.execution_finish   AS finish_time,
       h.duration,
       h.is_crashed,
       h.error_data->>'sqlerrcode'   AS sqlerrcode,
       h.error_data->>'message'      AS err_message,
       h.error_data->>'detail'       AS err_detail,
       h.error_data->>'hint'         AS err_hint,
       h.error_data->>'funcname'     AS err_funcname,
       h.error_data->>'filename'     AS err_filename,
       h.error_data->>'lineno'       AS err_lineno,
       h.error_data->>'schema_name'  AS err_schema_name,
       h.error_data->>'table_name'   AS err_table_name,
       h.error_data                  AS error_data,
       h.config
  FROM time_series.job_history h
 WHERE h.succeeded IS NOT TRUE;

GRANT SELECT ON time_series.job_errors TO PUBLIC;

-- =========================================================================
--  alter_job: modify a policy's schedule / config in place
--
--  Supports the parameters most commonly needed for BGW policy maintenance.
--  All COALESCE-style: pass NULL (or omit) to leave a field untouched.
--
--  Notes:
--   - For CAGG policies, modifying `config` re-validates that `cagg_name`
--     is still present.  Other fields are not validated structurally.
--   - When `schedule_interval` is changed, the next_start in bgw_job_stat is
--     recomputed to (now() + schedule_interval) so the change takes effect on
--     the next scheduler tick.
--   - `next_start` overrides any other recomputation when explicitly given.
-- =========================================================================
CREATE OR REPLACE FUNCTION time_series.alter_job(
    job_id              int,
    schedule_interval   interval     DEFAULT NULL,
    max_runtime         interval     DEFAULT NULL,
    max_retries         int          DEFAULT NULL,
    retry_period        interval     DEFAULT NULL,
    scheduled           bool         DEFAULT NULL,
    config              jsonb        DEFAULT NULL,
    next_start          timestamptz  DEFAULT NULL,
    if_exists           bool         DEFAULT false
) RETURNS time_series.bgw_job AS $$
DECLARE
    v_job time_series.bgw_job;
BEGIN
    SELECT * INTO v_job FROM time_series.bgw_job WHERE id = alter_job.job_id;

    IF NOT FOUND THEN
        IF if_exists THEN
            RAISE NOTICE 'job % does not exist, skipping', job_id;
            RETURN NULL;
        END IF;
        RAISE EXCEPTION 'job % does not exist', job_id;
    END IF;

    -- Owner check: caller must be a member of the job's owner role
    -- (which includes the owner itself and superuser).  Mirrors TSDB
    -- ts_bgw_job_permission_check in src/bgw/job.c.
    IF NOT pg_has_role(current_user::name, v_job.owner, 'MEMBER') THEN
        RAISE EXCEPTION USING
            ERRCODE = 'insufficient_privilege',
            MESSAGE = format('insufficient permissions to alter job %s', job_id),
            DETAIL  = format('Job %s is owned by role "%s" but user "%s" does not belong to that role.',
                             job_id, v_job.owner, current_user);
    END IF;

    -- Validate config for known proc types.  The user-supplied config
    -- replaces the existing one wholesale (see COALESCE below), so we
    -- must check it as a complete spec — anything alter_job accepts
    -- here, the BGW worker will execute next tick.  Without these checks
    -- a typo (ghost cagg_name) or a too-narrow window silently breaks
    -- a working policy and the failure only surfaces in mark_end as
    -- last_run_success=false, which DBAs find hard to diagnose.
    IF config IS NOT NULL AND v_job.proc_name = 'ts_policy_refresh_cagg' THEN
        PERFORM time_series._validate_cagg_policy_config(
            config->>'cagg_name',
            (config->>'start_offset')::interval,
            (config->>'end_offset')::interval);
    END IF;

    -- Apply COALESCE-style updates.
    UPDATE time_series.bgw_job
       SET schedule_interval = COALESCE(alter_job.schedule_interval, bgw_job.schedule_interval),
           max_runtime       = COALESCE(alter_job.max_runtime,       bgw_job.max_runtime),
           max_retries       = COALESCE(alter_job.max_retries,       bgw_job.max_retries),
           retry_period      = COALESCE(alter_job.retry_period,      bgw_job.retry_period),
           scheduled         = COALESCE(alter_job.scheduled,         bgw_job.scheduled),
           config            = COALESCE(alter_job.config,            bgw_job.config)
     WHERE id = alter_job.job_id;

    -- Recompute next_start.  Explicit next_start wins; otherwise, if the
    -- schedule_interval was modified, slide next_start forward.
    IF alter_job.next_start IS NOT NULL THEN
        UPDATE time_series.bgw_job_stat
           SET next_start = alter_job.next_start
         WHERE bgw_job_stat.job_id = alter_job.job_id;
    ELSIF alter_job.schedule_interval IS NOT NULL THEN
        UPDATE time_series.bgw_job_stat
           SET next_start = now() + alter_job.schedule_interval
         WHERE bgw_job_stat.job_id = alter_job.job_id;
    END IF;

    SELECT * INTO v_job FROM time_series.bgw_job WHERE id = alter_job.job_id;

    -- Broadcast relcache invalidation so the scheduler picks up changes
    PERFORM time_series.bgw_invalidate_cache();

    RETURN v_job;
END;
$$ LANGUAGE plpgsql;

-- =========================================================================
--  run_job: synchronously run a policy in the calling backend
--
--  Equivalent to having the BGW worker fire once now: invokes
--  mark_start → ts_bgw_job_execute → mark_end in the current session.
--
--  This is intentionally a thin wrapper around the same C path used by the
--  real BGW worker (direct C call to cagg_refresh, see
--  doc/feature/cagg/cagg_bgw_debug_journey.md for design rationale).  It
--  enables deterministic regression testing of the worker code path
--  without requiring scheduler tick timing.
--
--  Difference from a real BGW run: this function runs in the caller's
--  session, so SET options (search_path, optimizer, etc.) inherit from
--  the caller.  The bgw_job_stat row is updated identically.
-- =========================================================================
-- Defined as PROCEDURE so that ts_bgw_job_execute() can control transactions
-- (StartTransactionCommand / CommitTransactionCommand) the same way the BGW
-- worker entrypoint does.  Call as: CALL time_series.run_job(jid);
CREATE PROCEDURE time_series.run_job(job_id int)
LANGUAGE C AS 'MODULE_PATHNAME', 'ts_run_job';

-- =========================================================================
--  Mock-time test infrastructure (Apache 2.0; ported from TimescaleDB)
--
--  These functions and tables are used ONLY by the regression test suite
--  to drive a virtual-clock BGW scheduler in <1 second per test case.
--  They are unconditionally installed (just like other test helpers) so
--  cagg_bgw_mock.sql does not need a separate setup step.
--
--  Setup pattern in tests:
--    SELECT time_series.ts_bgw_params_create();   -- once per test
--    -- run a mock scheduler tick:
--    SELECT time_series.ts_bgw_db_scheduler_test_run_and_wait_for_scheduler_finish(60000000);
--    SELECT * FROM public.sorted_bgw_log;          -- inspect events
-- =========================================================================

-- Test scheduler / params functions (from test/src/bgw/scheduler_mock.c
-- and test/src/bgw/params.c).
CREATE FUNCTION time_series.ts_bgw_db_scheduler_test_main()
RETURNS void LANGUAGE C AS 'MODULE_PATHNAME', 'ts_bgw_db_scheduler_test_main';

CREATE FUNCTION time_series.ts_bgw_db_scheduler_test_run_and_wait_for_scheduler_finish(int4)
RETURNS void LANGUAGE C AS 'MODULE_PATHNAME',
'ts_bgw_db_scheduler_test_run_and_wait_for_scheduler_finish';

CREATE FUNCTION time_series.ts_bgw_db_scheduler_test_run(int4)
RETURNS void LANGUAGE C AS 'MODULE_PATHNAME', 'ts_bgw_db_scheduler_test_run';

CREATE FUNCTION time_series.ts_bgw_db_scheduler_test_wait_for_scheduler_finish()
RETURNS void LANGUAGE C AS 'MODULE_PATHNAME',
'ts_bgw_db_scheduler_test_wait_for_scheduler_finish';

CREATE FUNCTION time_series.ts_bgw_params_create()
RETURNS void LANGUAGE C AS 'MODULE_PATHNAME', 'ts_bgw_params_create';

CREATE FUNCTION time_series.ts_bgw_params_destroy()
RETURNS void LANGUAGE C AS 'MODULE_PATHNAME', 'ts_bgw_params_destroy';

CREATE FUNCTION time_series.ts_bgw_params_reset_time(int8, bool)
RETURNS void LANGUAGE C AS 'MODULE_PATHNAME', 'ts_bgw_params_reset_time';

CREATE FUNCTION time_series.ts_bgw_params_mock_wait_returns_immediately(int4)
RETURNS void LANGUAGE C AS 'MODULE_PATHNAME',
'ts_bgw_params_mock_wait_returns_immediately';

-- 1:1-with-TSDB test infrastructure: synthetic job dispatcher used by
-- the heavy bgw_db_scheduler tests.  Tests register jobs in bgw_job
-- with proc_name in {bgw_test_job_1, bgw_test_job_2_error,
-- bgw_test_job_3_long, bgw_test_job_4} and the dispatcher routes to
-- the matching local function.  ts_bgw_job_execute_test is the
-- entrypoint installed by the mock scheduler.

CREATE FUNCTION time_series.ts_bgw_job_execute_test()
RETURNS void LANGUAGE C AS 'MODULE_PATHNAME', 'ts_bgw_job_execute_test';

CREATE FUNCTION time_series.ts_bgw_test_job_sleep()
RETURNS void LANGUAGE C AS 'MODULE_PATHNAME', 'ts_bgw_test_job_sleep';

CREATE FUNCTION time_series.ts_test_next_scheduled_execution_slot(
    schedule_interval interval,
    finish_time       timestamptz,
    initial_start     timestamptz,
    timezone          text DEFAULT NULL)
RETURNS timestamptz LANGUAGE C AS 'MODULE_PATHNAME',
'ts_test_next_scheduled_execution_slot';

-- Lock down mock-time test infrastructure to superuser only.  These
-- functions exist for the regression test suite to drive a synthetic
-- BGW scheduler against mocked time, but they are not meant for end
-- users:
--   * ts_bgw_db_scheduler_test_run / _and_wait_for_scheduler_finish
--     fork a real BackgroundWorker that reads bgw_job and dispatches
--     synthetic test_job_* — anyone can consume worker slots.
--   * ts_bgw_test_job_sleep blocks for an interval, holding a slot.
--   * ts_bgw_params_* mutate process-shared mock state.
--
-- Tests run as superuser and are unaffected.  Production users have
-- no legitimate reason to call any of these.
REVOKE EXECUTE ON FUNCTION time_series.ts_bgw_db_scheduler_test_main()                              FROM PUBLIC;
REVOKE EXECUTE ON FUNCTION time_series.ts_bgw_db_scheduler_test_run_and_wait_for_scheduler_finish(int4) FROM PUBLIC;
REVOKE EXECUTE ON FUNCTION time_series.ts_bgw_db_scheduler_test_run(int4)                           FROM PUBLIC;
REVOKE EXECUTE ON FUNCTION time_series.ts_bgw_db_scheduler_test_wait_for_scheduler_finish()         FROM PUBLIC;
REVOKE EXECUTE ON FUNCTION time_series.ts_bgw_params_create()                                       FROM PUBLIC;
REVOKE EXECUTE ON FUNCTION time_series.ts_bgw_params_destroy()                                      FROM PUBLIC;
REVOKE EXECUTE ON FUNCTION time_series.ts_bgw_params_reset_time(int8, bool)                         FROM PUBLIC;
REVOKE EXECUTE ON FUNCTION time_series.ts_bgw_params_mock_wait_returns_immediately(int4)            FROM PUBLIC;
REVOKE EXECUTE ON FUNCTION time_series.ts_bgw_job_execute_test()                                    FROM PUBLIC;
REVOKE EXECUTE ON FUNCTION time_series.ts_bgw_test_job_sleep()                                      FROM PUBLIC;
REVOKE EXECUTE ON FUNCTION time_series.ts_test_next_scheduled_execution_slot(interval, timestamptz, timestamptz, text) FROM PUBLIC;
