--
-- date_trunc vectorization (pax storage).
--
-- Covers commits:
--   9530f82d9d2  Feature: Add DATE_TRUNC vectorized execution
--   59247cd8ba2  Fix date_trunc vectorization for non-const unit arg
--
-- Exercised code paths:
--   contrib/vectorization/src/backend/utils/arrow/fmgr.c       : check_floor_temporal
--   contrib/vectorization/src/backend/vecexecutor/execMain.c   : build_floor_temporal_expr
--   contrib/vectorization/pg_vec.dat                           : procoid 2020 mapping
--

-- ============================================================================
-- Setup: pax tables. timestamptz table exists only to drive the negative test
-- for the unmapped procoid 1217.
-- ============================================================================
CREATE TABLE dt_ts (id int, ts timestamp)   USING pax DISTRIBUTED BY (id);
CREATE TABLE dt_d  (id int, d  date)        USING pax DISTRIBUTED BY (id);
CREATE TABLE dt_tz (id int, tz timestamptz) USING pax DISTRIBUTED BY (id);

INSERT INTO dt_ts VALUES
    (1, '2024-06-15 12:34:56.789123'),
    (2, '2024-01-01 00:00:00'),
    (3, '1999-12-31 23:59:59.999999'),
    (4, NULL);

INSERT INTO dt_d VALUES
    (1, '2024-06-15'),
    (2, '2024-01-01'),
    (3, '1999-12-31'),
    (4, NULL);

INSERT INTO dt_tz VALUES
    (1, '2024-06-15 12:34:56.789123+00'),
    (2, '2024-01-01 00:00:00+00'),
    (3, '1999-12-31 23:59:59.999999+00');

-- ============================================================================
-- A. Vectorized path: check_floor_temporal accepts -> Arrow floor_temporal.
--    Each SELECT drives one unit branch in build_floor_temporal_expr.
-- ============================================================================

-- A1. Every supported unit (covers all 10 branches of the unit if/else chain).
--     'nanosecond' is not accepted by the PG row engine, so this query path
--     relies on the vectorized mapping working.  'week' is intentionally
--     excluded from the vectorized whitelist (see B2); PG's Monday-based
--     semantics cannot be reproduced cheaply by Arrow's floor_temporal.
SELECT id, date_trunc('nanosecond',  ts) FROM dt_ts ORDER BY id;
SELECT id, date_trunc('microsecond', ts) FROM dt_ts ORDER BY id;
SELECT id, date_trunc('millisecond', ts) FROM dt_ts ORDER BY id;
SELECT id, date_trunc('second',      ts) FROM dt_ts ORDER BY id;
SELECT id, date_trunc('minute',      ts) FROM dt_ts ORDER BY id;
SELECT id, date_trunc('hour',        ts) FROM dt_ts ORDER BY id;
SELECT id, date_trunc('day',         ts) FROM dt_ts ORDER BY id;
SELECT id, date_trunc('month',       ts) FROM dt_ts ORDER BY id;
SELECT id, date_trunc('quarter',     ts) FROM dt_ts ORDER BY id;
SELECT id, date_trunc('year',        ts) FROM dt_ts ORDER BY id;

-- Plan assertion: vectorized scan + floor_temporal projection.
EXPLAIN (COSTS OFF) SELECT date_trunc('hour', ts) FROM dt_ts;

-- A2. Plural aliases and case-insensitive matching (pg_strcasecmp).
SELECT id, date_trunc('SECONDS', ts) FROM dt_ts ORDER BY id;
SELECT id, date_trunc('Hours',   ts) FROM dt_ts ORDER BY id;
SELECT id, date_trunc('days',    ts) FROM dt_ts ORDER BY id;
SELECT id, date_trunc('MONTHS',  ts) FROM dt_ts ORDER BY id;

-- A3. Negative case: date column input falls back to the row engine.
--     PG has no date_trunc(text, date) overload. Both implicit casts
--       date -> timestamp              (not preferred)
--       date -> timestamp with time zone  (preferred in category D)
--     are available, and PG's function resolution picks the preferred-type
--     candidate, i.e. procoid 1217 (timestamptz). Since pg_vec.dat only
--     maps procoid 2020, the timestamptz path has no vectorization entry
--     and the projection evaluates in the row engine.
SELECT id, date_trunc('month', d) FROM dt_d ORDER BY id;
EXPLAIN (COSTS OFF) SELECT date_trunc('month', d) FROM dt_d;

-- ============================================================================
-- B. Planner fallback: check_floor_temporal returns false -> row engine.
-- ============================================================================

-- B1. First argument not a Const (comes from a column).
--     check_floor_temporal rejects (!IsA(first_expr, Const)); row engine runs.
CREATE TABLE dt_units (u text) USING pax DISTRIBUTED RANDOMLY;
INSERT INTO dt_units VALUES ('hour');
SELECT date_trunc(u, ts) FROM dt_ts, dt_units WHERE dt_ts.id = 1;
EXPLAIN (COSTS OFF) SELECT date_trunc(u, ts) FROM dt_ts, dt_units;

-- B2. Units excluded from the vectorized whitelist.
--     'millennium'/'century'/'decade' are absent from Arrow's floor_temporal.
--     'week'/'weeks' are supported by Arrow but diverge from PG semantics:
--     Arrow anchors to its Unix epoch (1970-01-01, Thursday); PG stores
--     timestamps since 2000-01-01 (Saturday); the 10957-day offset is not
--     a multiple of 7, so the result lands on Saturday in PG's frame
--     instead of Monday.  All four fall back to the row engine, which
--     yields correct PG values.
SELECT id, date_trunc('millennium', ts) FROM dt_ts WHERE id IN (1, 2) ORDER BY id;
SELECT id, date_trunc('century',    ts) FROM dt_ts WHERE id IN (1, 2) ORDER BY id;
SELECT id, date_trunc('decade',     ts) FROM dt_ts WHERE id IN (1, 2) ORDER BY id;
SELECT id, date_trunc('week',       ts) FROM dt_ts ORDER BY id;
EXPLAIN (COSTS OFF) SELECT date_trunc('week', ts) FROM dt_ts;

-- B3. timestamptz column: procoid 1217 is not mapped in pg_vec.dat, so the
--     projection falls back to the row engine (not via check_floor_temporal).
SELECT id, date_trunc('hour', tz) FROM dt_tz ORDER BY id;
EXPLAIN (COSTS OFF) SELECT date_trunc('hour', tz) FROM dt_tz;

-- ============================================================================
-- C. Result parity: pax (vectorized) vs. heap (row engine) agree on values.
-- ============================================================================
CREATE TABLE dt_ts_heap (id int, ts timestamp) USING heap DISTRIBUTED BY (id);
INSERT INTO dt_ts_heap SELECT * FROM dt_ts;

SELECT 'mismatch: day'   AS tag, id, v FROM (
    SELECT id, date_trunc('day',   ts) AS v FROM dt_ts
    EXCEPT
    SELECT id, date_trunc('day',   ts)      FROM dt_ts_heap) x;

SELECT 'mismatch: month' AS tag, id, v FROM (
    SELECT id, date_trunc('month', ts) AS v FROM dt_ts
    EXCEPT
    SELECT id, date_trunc('month', ts)      FROM dt_ts_heap) x;

-- week is the unit that made us fall back; pin down parity post-fallback.
SELECT 'mismatch: week'  AS tag, id, v FROM (
    SELECT id, date_trunc('week',  ts) AS v FROM dt_ts
    EXCEPT
    SELECT id, date_trunc('week',  ts)      FROM dt_ts_heap) x;

-- Cleanup
DROP TABLE dt_ts, dt_d, dt_tz, dt_units, dt_ts_heap;
