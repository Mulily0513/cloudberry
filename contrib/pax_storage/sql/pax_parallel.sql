set enable_parallel = on;
set max_parallel_workers_per_gather=2;
set min_parallel_table_scan_size=0;
set parallel_setup_cost=0;
set optimizer_enable_indexonlyscan = off;
set optimizer_enable_indexscan = off;
set enable_indexonlyscan = off;
set enable_indexscan = off;
set enable_bitmapscan = off;

CREATE TABLE foo (a INT, b INT, c CHAR(128)) using pax;
CREATE INDEX foo_index ON foo(b);

INSERT INTO foo SELECT i as a, i as b, 'hello world' as c FROM generate_series(1, 100000) AS i;
INSERT INTO foo SELECT i as a, i as b, 'hello world' as c FROM generate_series(1, 100000) AS i;
INSERT INTO foo SELECT i as a, i as b, 'hello world' as c FROM generate_series(1, 100000) AS i;
INSERT INTO foo SELECT i as a, i as b, 'hello world' as c FROM generate_series(1, 100000) AS i;
INSERT INTO foo SELECT i as a, i as b, 'hello world' as c FROM generate_series(1, 100000) AS i;
INSERT INTO foo SELECT i as a, i as b, 'hello world' as c FROM generate_series(1, 100000) AS i;
INSERT INTO foo SELECT i as a, i as b, 'hello world' as c FROM generate_series(1, 100000) AS i;
INSERT INTO foo SELECT i as a, i as b, 'hello world' as c FROM generate_series(1, 100000) AS i;
INSERT INTO foo SELECT i as a, i as b, 'hello world' as c FROM generate_series(1, 100000) AS i;
INSERT INTO foo SELECT i as a, i as b, 'hello world' as c FROM generate_series(1, 100000) AS i;
INSERT INTO foo SELECT i as a, i as b, 'hello world' as c FROM generate_series(1, 100000) AS i;

ANALYZE foo;
EXPLAIN SELECT COUNT(*) FROM foo;
SELECT COUNT(*) FROM foo;

EXPLAIN SELECT COUNT(*) FROM foo WHERE b = 17;
SELECT COUNT(*) FROM foo WHERE b = 17;

DELETE FROM foo WHERE b = 17;

SELECT COUNT(*) FROM foo WHERE b = 17;

SELECT COUNT(*) FROM foo;
DELETE FROM foo WHERE b = 16 OR b = 10000 OR b = 15;
SELECT COUNT(*) FROM foo;

DROP TABLE foo;
