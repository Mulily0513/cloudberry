-- stability_helpers.sql
-- Helpers for stability tests: catalog snapshots for drift detection,
-- bounded loops for repeat tests.
--
-- Relies on: common_setup.sql already loaded.

-- Snapshot of catalog-shape counts - used to detect drift across repeated
-- create/drop cycles. Takes a label so multiple snapshots can coexist in
-- the same session.
CREATE TABLE IF NOT EXISTS public.stability_catalog_snap (
    snap_label TEXT NOT NULL,
    metric TEXT NOT NULL,
    value_cnt BIGINT NOT NULL,
    snap_at TIMESTAMP WITH TIME ZONE DEFAULT clock_timestamp(),
    PRIMARY KEY (snap_label, metric)
);

CREATE OR REPLACE FUNCTION stability_snapshot_catalog(p_label TEXT)
RETURNS void AS $$
BEGIN
    -- Remove any previous snapshot with this label
    DELETE FROM public.stability_catalog_snap WHERE snap_label = p_label;

    INSERT INTO public.stability_catalog_snap(snap_label, metric, value_cnt)
    VALUES
        (p_label, 'pg_class_total',
         (SELECT count(*) FROM pg_class)),
        (p_label, 'pg_class_relkind_r',  -- regular + foreign tables
         (SELECT count(*) FROM pg_class WHERE relkind IN ('r','f'))),
        (p_label, 'pg_namespace_total',
         (SELECT count(*) FROM pg_namespace)),
        (p_label, 'pg_attribute_nonsystem',
         (SELECT count(*) FROM pg_attribute a
          JOIN pg_class c ON a.attrelid=c.oid
          WHERE c.relkind IN ('r','f') AND a.attnum > 0)),
        (p_label, 'foreign_servers',
         (SELECT count(*) FROM pg_foreign_server)),
        (p_label, 'user_mappings',
         (SELECT count(*) FROM pg_user_mapping));

    RAISE NOTICE '[STAB] Catalog snapshot % taken', p_label;
END;
$$ LANGUAGE plpgsql;

-- Diff two snapshots. Returns rows WHERE counts differ (empty = no drift).
CREATE OR REPLACE FUNCTION stability_diff_catalog(
    p_before TEXT,
    p_after  TEXT
) RETURNS TABLE (metric TEXT, before_cnt BIGINT, after_cnt BIGINT, delta BIGINT) AS $$
BEGIN
    RETURN QUERY
    SELECT b.metric,
           b.value_cnt,
           a.value_cnt,
           a.value_cnt - b.value_cnt
    FROM public.stability_catalog_snap b
    JOIN public.stability_catalog_snap a
      ON b.metric = a.metric
    WHERE b.snap_label = p_before
      AND a.snap_label = p_after
      AND b.value_cnt <> a.value_cnt
    ORDER BY b.metric;
END;
$$ LANGUAGE plpgsql;

-- Clear snapshots by label, or all if no label given
CREATE OR REPLACE FUNCTION stability_clear_snapshots(p_label TEXT DEFAULT NULL)
RETURNS void AS $$
BEGIN
    IF p_label IS NULL THEN
        DELETE FROM public.stability_catalog_snap;
    ELSE
        DELETE FROM public.stability_catalog_snap WHERE snap_label = p_label;
    END IF;
END;
$$ LANGUAGE plpgsql;

SELECT test_log('Stability helpers loaded');
