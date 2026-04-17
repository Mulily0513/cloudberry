-- performance_helpers.sql
-- Performance measurement utilities for datalake_fdw tests
-- Requires: common_setup.sql to be loaded first
--
-- This file provides:
--   Tables:
--     perf_results          - per-iteration timing samples
--     perf_results_summary  - aggregated p50/p95/min/max per (test, operation)
--     perf_plans            - captured EXPLAIN output for plan stability
--     perf_timer_context    - active manual timers (legacy API)
--
--   Modern API (preferred):
--     perf_run_iterations(test, op, sql, iterations, warmup, rows, notes)
--                           - run SQL N times with warmup, record p50/min/max
--     perf_capture_explain(test, op, sql)
--                           - capture EXPLAIN (ANALYZE, BUFFERS, COSTS off,
--                             TIMING off) into perf_plans
--
--   Legacy API (kept for backward-compat with tests not yet refactored):
--     perf_start_timer / perf_end_timer
--
--   Reporting / cleanup:
--     perf_report           - human-readable summary of perf_results
--     perf_summary_report   - aggregated report (p50/min/max)
--     perf_clear            - clear timing data

-- Create performance results table
CREATE TABLE IF NOT EXISTS public.perf_results (
    id SERIAL PRIMARY KEY,
    test_name TEXT NOT NULL,
    operation TEXT NOT NULL,
    start_time TIMESTAMP WITH TIME ZONE,
    end_time TIMESTAMP WITH TIME ZONE,
    duration_ms NUMERIC,
    rows_count BIGINT,
    throughput_rows_per_sec NUMERIC,
    notes TEXT,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT clock_timestamp()
);

-- Create performance timer context table (stores active timers)
CREATE TABLE IF NOT EXISTS public.perf_timer_context (
    test_name TEXT NOT NULL,
    operation TEXT NOT NULL,
    start_time TIMESTAMP WITH TIME ZONE NOT NULL,
    PRIMARY KEY (test_name, operation)
);

-- Aggregated summary table - one row per perf_run_iterations call
CREATE TABLE IF NOT EXISTS public.perf_results_summary (
    id SERIAL PRIMARY KEY,
    test_name TEXT NOT NULL,
    operation TEXT NOT NULL,
    iterations INT NOT NULL,
    warmup INT NOT NULL,
    p50_ms NUMERIC,
    p95_ms NUMERIC,
    min_ms NUMERIC,
    max_ms NUMERIC,
    avg_ms NUMERIC,
    rows_count BIGINT,
    throughput_rows_per_sec NUMERIC,
    notes TEXT,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT clock_timestamp()
);

-- Captured EXPLAIN output for plan stability regression detection
CREATE TABLE IF NOT EXISTS public.perf_plans (
    id SERIAL PRIMARY KEY,
    test_name TEXT NOT NULL,
    operation TEXT NOT NULL,
    plan_text TEXT,
    captured_at TIMESTAMP WITH TIME ZONE DEFAULT clock_timestamp()
);

-- Function: Start a performance timer
-- Usage: SELECT perf_start_timer('test_name', 'operation_name');
CREATE OR REPLACE FUNCTION perf_start_timer(
    p_test_name TEXT,
    p_operation TEXT
) RETURNS void AS $$
BEGIN
    -- Delete any existing timer for this test/operation
    DELETE FROM public.perf_timer_context
    WHERE test_name = p_test_name AND operation = p_operation;

    -- Insert new timer
    INSERT INTO public.perf_timer_context (test_name, operation, start_time)
    VALUES (p_test_name, p_operation, clock_timestamp());

    RAISE NOTICE '[PERF] Started timer: %.%', p_test_name, p_operation;
END;
$$ LANGUAGE plpgsql;

-- Function: End a performance timer and record results
-- Usage: SELECT perf_end_timer('test_name', 'operation_name', row_count, 'optional notes');
CREATE OR REPLACE FUNCTION perf_end_timer(
    p_test_name TEXT,
    p_operation TEXT,
    p_rows_count BIGINT DEFAULT NULL,
    p_notes TEXT DEFAULT NULL
) RETURNS void AS $$
DECLARE
    v_start_time TIMESTAMP WITH TIME ZONE;
    v_end_time TIMESTAMP WITH TIME ZONE;
    v_duration_ms NUMERIC;
    v_throughput NUMERIC;
BEGIN
    -- Get start time from context
    SELECT start_time INTO v_start_time
    FROM public.perf_timer_context
    WHERE test_name = p_test_name AND operation = p_operation;

    IF v_start_time IS NULL THEN
        RAISE WARNING '[PERF] No timer found for: %.%', p_test_name, p_operation;
        RETURN;
    END IF;

    -- Calculate duration
    v_end_time := clock_timestamp();
    v_duration_ms := EXTRACT(EPOCH FROM (v_end_time - v_start_time)) * 1000;

    -- Calculate throughput if row count provided
    IF p_rows_count IS NOT NULL AND p_rows_count > 0 THEN
        v_throughput := p_rows_count / (v_duration_ms / 1000.0);
    END IF;

    -- Insert result
    INSERT INTO public.perf_results (
        test_name, operation, start_time, end_time,
        duration_ms, rows_count, throughput_rows_per_sec, notes
    ) VALUES (
        p_test_name, p_operation, v_start_time, v_end_time,
        v_duration_ms, p_rows_count, v_throughput, p_notes
    );

    -- Remove timer from context
    DELETE FROM public.perf_timer_context
    WHERE test_name = p_test_name AND operation = p_operation;

    RAISE NOTICE '[PERF] Completed: %.% - Duration: % ms, Rows: %, Throughput: % rows/sec',
        p_test_name, p_operation, ROUND(v_duration_ms, 2),
        COALESCE(p_rows_count, 0), ROUND(v_throughput, 2);
END;
$$ LANGUAGE plpgsql;

-- Function: Generate performance report
-- Usage: SELECT * FROM perf_report(); or SELECT * FROM perf_report('test_name_filter');
CREATE OR REPLACE FUNCTION perf_report(
    p_test_filter TEXT DEFAULT NULL
) RETURNS TABLE (
    test_name TEXT,
    operation TEXT,
    duration_ms NUMERIC,
    rows_count BIGINT,
    throughput_rows_per_sec NUMERIC,
    notes TEXT,
    captured_at TIMESTAMP WITH TIME ZONE
) AS $$
BEGIN
    RETURN QUERY
    SELECT
        r.test_name,
        r.operation,
        ROUND(r.duration_ms, 2) as duration_ms,
        r.rows_count,
        ROUND(r.throughput_rows_per_sec, 2) as throughput_rows_per_sec,
        r.notes,
        r.created_at as captured_at
    FROM public.perf_results r
    WHERE p_test_filter IS NULL OR r.test_name LIKE '%' || p_test_filter || '%'
    ORDER BY r.created_at DESC;
END;
$$ LANGUAGE plpgsql;

-- Function: Clear performance results
-- Usage: SELECT perf_clear(); or SELECT perf_clear('test_name_filter');
CREATE OR REPLACE FUNCTION perf_clear(
    p_test_filter TEXT DEFAULT NULL
) RETURNS INTEGER AS $$
DECLARE
    v_deleted INTEGER;
BEGIN
    IF p_test_filter IS NULL THEN
        DELETE FROM public.perf_results;
        DELETE FROM public.perf_results_summary;
        DELETE FROM public.perf_plans;
    ELSE
        DELETE FROM public.perf_results
        WHERE test_name LIKE '%' || p_test_filter || '%';
        DELETE FROM public.perf_results_summary
        WHERE test_name LIKE '%' || p_test_filter || '%';
        DELETE FROM public.perf_plans
        WHERE test_name LIKE '%' || p_test_filter || '%';
    END IF;

    GET DIAGNOSTICS v_deleted = ROW_COUNT;
    RAISE NOTICE '[PERF] Cleared % performance result(s)', v_deleted;
    RETURN v_deleted;
END;
$$ LANGUAGE plpgsql;

-- ============================================================================
-- Modern API: perf_run_iterations + perf_capture_explain
-- ============================================================================

-- Function: Run a SQL statement N times with optional warmup, record per-iteration
-- timing into perf_results, and write aggregated p50/p95/min/max/avg into
-- perf_results_summary.
--
-- The SQL string is executed via PL/pgSQL EXECUTE which fully runs the query
-- server-side (rows from SELECT are discarded but materialized). PL/pgSQL
-- EXECUTE only accepts a single statement, so for multi-statement workloads
-- (e.g. TRUNCATE before INSERT) use the p_setup_sql parameter — it runs once
-- before each measured iteration and is NOT included in the timing.
--
-- Usage:
--   SELECT perf_run_iterations(
--       'iceberg_read', 'full_scan_5k',
--       'SELECT COUNT(*) FROM iceberg_perf_read',
--       3,        -- iterations
--       1,        -- warmup
--       5000,     -- rows_count (for throughput; NULL if unknown)
--       'note',   -- notes
--       NULL);    -- p_setup_sql (optional, runs before each iteration)
--
-- Emits a single deterministic NOTICE line so pg_regress diffs stay stable
-- (no timing values in the message).
CREATE OR REPLACE FUNCTION perf_run_iterations(
    p_test_name TEXT,
    p_operation TEXT,
    p_sql TEXT,
    p_iterations INT DEFAULT 3,
    p_warmup INT DEFAULT 1,
    p_rows_count BIGINT DEFAULT NULL,
    p_notes TEXT DEFAULT NULL,
    p_setup_sql TEXT DEFAULT NULL
) RETURNS void AS $$
DECLARE
    v_start_time TIMESTAMP WITH TIME ZONE;
    v_end_time TIMESTAMP WITH TIME ZONE;
    v_duration_ms NUMERIC;
    v_durations NUMERIC[] := ARRAY[]::NUMERIC[];
    v_throughput NUMERIC;
    v_p50 NUMERIC;
    v_p95 NUMERIC;
    v_min NUMERIC;
    v_max NUMERIC;
    v_avg NUMERIC;
BEGIN
    -- Warmup runs (not recorded)
    FOR i IN 1..p_warmup LOOP
        IF p_setup_sql IS NOT NULL THEN
            EXECUTE p_setup_sql;
        END IF;
        EXECUTE p_sql;
    END LOOP;

    -- Measured iterations
    FOR i IN 1..p_iterations LOOP
        IF p_setup_sql IS NOT NULL THEN
            EXECUTE p_setup_sql;  -- not timed
        END IF;
        v_start_time := clock_timestamp();
        EXECUTE p_sql;
        v_end_time := clock_timestamp();
        v_duration_ms := EXTRACT(EPOCH FROM (v_end_time - v_start_time)) * 1000;
        v_durations := array_append(v_durations, v_duration_ms);

        IF p_rows_count IS NOT NULL AND p_rows_count > 0 AND v_duration_ms > 0 THEN
            v_throughput := p_rows_count / (v_duration_ms / 1000.0);
        ELSE
            v_throughput := NULL;
        END IF;

        INSERT INTO public.perf_results (
            test_name, operation, start_time, end_time,
            duration_ms, rows_count, throughput_rows_per_sec, notes
        ) VALUES (
            p_test_name, p_operation, v_start_time, v_end_time,
            v_duration_ms, p_rows_count, v_throughput, p_notes
        );
    END LOOP;

    -- Aggregate
    SELECT
        percentile_cont(0.50) WITHIN GROUP (ORDER BY d),
        percentile_cont(0.95) WITHIN GROUP (ORDER BY d),
        MIN(d), MAX(d), AVG(d)
    INTO v_p50, v_p95, v_min, v_max, v_avg
    FROM unnest(v_durations) AS t(d);

    IF p_rows_count IS NOT NULL AND p_rows_count > 0 AND v_p50 > 0 THEN
        v_throughput := p_rows_count / (v_p50 / 1000.0);
    ELSE
        v_throughput := NULL;
    END IF;

    INSERT INTO public.perf_results_summary (
        test_name, operation, iterations, warmup,
        p50_ms, p95_ms, min_ms, max_ms, avg_ms,
        rows_count, throughput_rows_per_sec, notes
    ) VALUES (
        p_test_name, p_operation, p_iterations, p_warmup,
        v_p50, v_p95, v_min, v_max, v_avg,
        p_rows_count, v_throughput, p_notes
    );

    -- Stable, deterministic NOTICE (no timing values - safe for pg_regress diff)
    RAISE NOTICE '[PERF-RUN] %.% completed (% iterations, % warmup)',
        p_test_name, p_operation, p_iterations, p_warmup;
END;
$$ LANGUAGE plpgsql;

-- Function: Capture EXPLAIN output for a SQL statement into perf_plans.
-- Uses (COSTS off, VERBOSE off) so the captured text is stable across runs
-- (no costs, no timings, no buffer counts, no row estimates).
--
-- Note: plain EXPLAIN (no ANALYZE) is used so the plan is shape-only and
-- doesn't depend on data statistics or buffer state. For execution-time
-- comparisons use perf_run_iterations instead.
--
-- Usage:
--   SELECT perf_capture_explain(
--       'iceberg_read', 'full_scan_5k',
--       'SELECT COUNT(*) FROM iceberg_perf_read');
CREATE OR REPLACE FUNCTION perf_capture_explain(
    p_test_name TEXT,
    p_operation TEXT,
    p_sql TEXT
) RETURNS void AS $$
DECLARE
    v_plan_line TEXT;
    v_plan_text TEXT := '';
BEGIN
    FOR v_plan_line IN
        EXECUTE 'EXPLAIN (COSTS off, VERBOSE off) ' || p_sql
    LOOP
        v_plan_text := v_plan_text || v_plan_line || E'\n';
    END LOOP;

    INSERT INTO public.perf_plans (test_name, operation, plan_text)
    VALUES (p_test_name, p_operation, v_plan_text);

    RAISE NOTICE '[PERF-EXPLAIN] %.% plan captured', p_test_name, p_operation;
END;
$$ LANGUAGE plpgsql;

-- Function: aggregated summary report (one row per (test, operation) pair)
-- Usage: SELECT * FROM perf_summary_report();
--        SELECT * FROM perf_summary_report('iceberg_read');
CREATE OR REPLACE FUNCTION perf_summary_report(
    p_test_filter TEXT DEFAULT NULL
) RETURNS TABLE (
    test_name TEXT,
    operation TEXT,
    iterations INT,
    p50_ms NUMERIC,
    p95_ms NUMERIC,
    min_ms NUMERIC,
    max_ms NUMERIC,
    rows_count BIGINT,
    throughput_rows_per_sec NUMERIC,
    notes TEXT
) AS $$
BEGIN
    RETURN QUERY
    SELECT
        s.test_name, s.operation, s.iterations,
        ROUND(s.p50_ms, 2), ROUND(s.p95_ms, 2),
        ROUND(s.min_ms, 2), ROUND(s.max_ms, 2),
        s.rows_count, ROUND(s.throughput_rows_per_sec, 2),
        s.notes
    FROM public.perf_results_summary s
    WHERE p_test_filter IS NULL OR s.test_name LIKE '%' || p_test_filter || '%'
    ORDER BY s.created_at DESC;
END;
$$ LANGUAGE plpgsql;

-- Log performance helpers setup
SELECT test_log('Performance helpers loaded');
