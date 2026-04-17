-- iceberg_plan_stability.sql
-- Capture EXPLAIN (COSTS off) for a set of representative queries on an
-- Iceberg table. pg_regress will fail diff if the PLAN SHAPE changes between
-- runs, detecting optimizer regressions (e.g., loss of predicate pushdown
-- or switch from HashAgg to GroupAgg).
--
-- Note: we use bare EXPLAIN (not perf_capture_explain) so the plan text is
-- part of the pg_regress diff - expected/ captures the blessed plan.

\i ../../../lib/sql/common_setup.sql

DROP SERVER IF EXISTS pl_cat_srv CASCADE;
CREATE SERVER pl_cat_srv FOREIGN DATA WRAPPER iceberg_catalog_fdw;
CREATE USER MAPPING FOR current_user SERVER pl_cat_srv;
CREATE FOREIGN CATALOG pl_cat SERVER pl_cat_srv;
SET iceberg_default_catalog = 'pl_cat';

DROP SERVER IF EXISTS pl_vol_srv CASCADE;
CREATE SERVER pl_vol_srv FOREIGN DATA WRAPPER iceberg_volume_fdw
OPTIONS (type 's3', endpoint 'http://lakehouse:9100', region 'us-east-1',
         bucket_name 'warehouse', path_style_access 'true');
CREATE USER MAPPING FOR current_user SERVER pl_vol_srv
OPTIONS (access_key_id 'admin', secret_access_key 'password');
CREATE FOREIGN VOLUME pl_vol SERVER pl_vol_srv OPTIONS (base_path '/pl_test/');
SET iceberg_default_volume = 'pl_vol';

CREATE ICEBERG TABLE plan_tbl (id int, cat text, val numeric(10,2));
INSERT INTO plan_tbl SELECT g, 'cat_' || (g % 5), g*1.1
FROM generate_series(1, 1000) AS g;

-- Representative query shapes. COSTS off + VERBOSE off keeps output stable
-- across runs - no estimate numbers, no buffer counts.

\echo '--- q1: full scan count ---'
EXPLAIN (COSTS off, VERBOSE off) SELECT COUNT(*) FROM plan_tbl;

\echo '--- q2: filter with pushdown candidate ---'
EXPLAIN (COSTS off, VERBOSE off) SELECT * FROM plan_tbl WHERE id = 500;

\echo '--- q3: range filter ---'
EXPLAIN (COSTS off, VERBOSE off)
    SELECT COUNT(*) FROM plan_tbl WHERE id BETWEEN 100 AND 200;

\echo '--- q4: group by ---'
EXPLAIN (COSTS off, VERBOSE off)
    SELECT cat, COUNT(*), SUM(val) FROM plan_tbl GROUP BY cat ORDER BY cat;

\echo '--- q5: order by limit ---'
EXPLAIN (COSTS off, VERBOSE off)
    SELECT id, val FROM plan_tbl ORDER BY val DESC LIMIT 10;

-- Cleanup
DROP TABLE plan_tbl;
DROP VOLUME pl_vol;
DROP USER MAPPING FOR current_user SERVER pl_vol_srv;
DROP SERVER pl_vol_srv;
DROP CATALOG pl_cat;
DROP USER MAPPING FOR current_user SERVER pl_cat_srv;
DROP SERVER pl_cat_srv;
