-- Vectorized HashJoin spill regression test.
--
-- Exercises four independent HashJoin-spill bugs that, together, caused
-- hangs, crashes, or silent data loss when multiple hash joins spilled
-- concurrently in the same query:
--
-- 1. Spill file-name collision. Multiple hash-join nodes in the same
--    query shared the prefix pgsql_tmp_vec_spill_{PID}, so one node
--    OpenOutputStream(truncate=true) overwrote another's spill files.
--    Fix: include plan_node_id in the prefix.
--
-- 2. HashJoinSpillState thread-safety / deadlock under concurrent Arrow
--    worker threads. Requires pool_threads > 1.
--
-- 3. Spill replay correctness. Previous TOP-bit spill partitioning +
--    hash_shift_bits_ compensation had a build/probe mismatch that
--    caused up to 93.8% data loss in join results. A count + sum
--    check against the no-spill baseline catches any regression.
--
-- 4. SourceNode async probe-before-build race. With async SourceNode,
--    probe-side InputFinished can arrive before build-side InputFinished;
--    if spill triggers mid-build, OnBuildSideFinished now checks
--    probe_side_finished_ before starting partition processing.
--
-- A 3-way join has two HashJoin nodes; with a 1 MB spill budget both
-- build sides spill, which is the minimum plan shape that reproduces
-- bug #1.
SET vector.enable_vectorization = on;
SET default_table_access_method = pax;
DROP TABLE IF EXISTS hj_spill_a;
DROP TABLE IF EXISTS hj_spill_b;
DROP TABLE IF EXISTS hj_spill_c;
CREATE TABLE hj_spill_a (id int, val int) DISTRIBUTED BY (id);
CREATE TABLE hj_spill_b (id int, val int) DISTRIBUTED BY (id);
CREATE TABLE hj_spill_c (id int, val int) DISTRIBUTED BY (id);
INSERT INTO hj_spill_a SELECT i, i   FROM generate_series(1, 300000) i;
INSERT INTO hj_spill_b SELECT i, i*2 FROM generate_series(1, 300000) i;
INSERT INTO hj_spill_c SELECT i, i*3 FROM generate_series(1, 300000) i;
ANALYZE hj_spill_a;
ANALYZE hj_spill_b;
ANALYZE hj_spill_c;
-- No-spill baseline: 3-way hash join, verify aggregate on the non-spill path.
SET vector.hashjoin_spill_memory_mb = 0;
SELECT count(*) AS c, sum(a.val + b.val + c.val) AS s
FROM hj_spill_a a
  JOIN hj_spill_b b USING (id)
  JOIN hj_spill_c c USING (id);
-- Force spill in both hash joins with concurrent Arrow workers.
-- The expected result is identical to the no-spill baseline -- any data
-- loss (bug #3) or file-name collision (bug #1) would show up as a
-- count/sum mismatch or an IOError.
SET vector.hashjoin_spill_memory_mb = 1;
SET vector.pool_threads = 4;
SELECT count(*) AS c, sum(a.val + b.val + c.val) AS s
FROM hj_spill_a a
  JOIN hj_spill_b b USING (id)
  JOIN hj_spill_c c USING (id);
DROP TABLE hj_spill_a;
DROP TABLE hj_spill_b;
DROP TABLE hj_spill_c;
RESET vector.pool_threads;
RESET vector.hashjoin_spill_memory_mb;
RESET default_table_access_method;
RESET vector.enable_vectorization;
