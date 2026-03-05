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
