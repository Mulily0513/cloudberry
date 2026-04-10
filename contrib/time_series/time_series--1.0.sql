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

-- 6. cagg_policy — BGW refresh policies (REPLICATED)
CREATE TABLE time_series.cagg_policy (
    policy_id         SERIAL PRIMARY KEY,
    cagg_id           int       NOT NULL UNIQUE,
    schedule_interval interval  NOT NULL,
    start_offset      interval  NOT NULL,
    end_offset        interval  NOT NULL,
    active            bool      NOT NULL DEFAULT true
) DISTRIBUTED REPLICATED;

-- =========================================================================
--  CAGG Functions
-- =========================================================================

-- Row-level trigger function: writes dirty time ranges to L1 (cagg_insert.c)
CREATE FUNCTION time_series.cagg_invalidation_trigfn()
RETURNS trigger LANGUAGE C AS 'MODULE_PATHNAME', 'cagg_invalidation_trigfn';

-- Statement-level trigger function for TRUNCATE: full-range L1 invalidation
CREATE FUNCTION time_series.cagg_invalidation_truncate_trigfn()
RETURNS trigger LANGUAGE C AS 'MODULE_PATHNAME', 'cagg_invalidation_truncate_trigfn';

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
CREATE PROCEDURE time_series.refresh_continuous_aggregate(
    cagg_name text,
    window_start timestamptz DEFAULT NULL,
    window_end   timestamptz DEFAULT NULL
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
    DELETE FROM time_series.cagg_policy
        WHERE cagg_id = p_cagg_id;
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
-- TRUNCATE invalidation: handled via ProcessUtility hook in
-- cagg_create.c (not via triggers).  CBDB blocks both STATEMENT
-- triggers and event triggers for TRUNCATE, so the hook intercepts
-- TruncateStmt on QD and writes {-infinity, +infinity} to L1
-- before passing through to the standard TRUNCATE handler.
-- ============================================================
