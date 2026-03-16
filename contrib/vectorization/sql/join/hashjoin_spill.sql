-- ============================================================================
-- Hash Join Spill-to-Disk Regression Test
-- Verifies that hash join produces correct results when build side exceeds
-- memory budget and data is spilled to disk.
-- Tests: various data types, join types, NULL handling, composite keys,
--        filters, aggregation correctness, and data skew.
-- ============================================================================

set vector.enable_vectorization = on;
set optimizer = on;

-- ============================================================================
-- Setup: create tables with various data types
-- ============================================================================
drop table if exists spill_build, spill_probe;

create table spill_build (
    id          int,
    col_int2    smallint,
    col_int8    bigint,
    col_float4  real,
    col_float8  double precision,
    col_numeric numeric(12,2),
    col_text    text,
    col_varchar varchar(100),
    col_date    date,
    col_bool    boolean,
    col_pad     text
) with (appendonly=true, orientation=column) distributed by (id);

create table spill_probe (
    id          int,
    col_int2    smallint,
    col_int8    bigint,
    col_float4  real,
    col_float8  double precision,
    col_numeric numeric(12,2),
    col_text    text,
    col_varchar varchar(100),
    col_date    date,
    col_bool    boolean,
    col_pad     text
) with (appendonly=true, orientation=column) distributed by (id);

-- Build: 100K rows, wide rows to ensure spill at 1MB budget
insert into spill_build
select
    i,
    (i % 32767)::smallint,
    i::bigint * 100000,
    (i * 1.1)::real,
    (i * 2.2)::double precision,
    (i * 3.33)::numeric(12,2),
    'text_' || i,
    'varchar_' || (i % 1000),
    '2020-01-01'::date + (i % 365),
    (i % 2 = 0),
    repeat('B', 150)
from generate_series(1, 100000) i;

-- Probe: 50K rows, ids 1..50000 (partial overlap with build)
insert into spill_probe
select
    i,
    (i % 32767)::smallint,
    i::bigint * 100000,
    (i * 1.1)::real,
    (i * 2.2)::double precision,
    (i * 3.33)::numeric(12,2),
    'text_' || i,
    'varchar_' || (i % 1000),
    '2020-01-01'::date + (i % 365),
    (i % 2 = 0),
    repeat('P', 100)
from generate_series(1, 50000) i;

analyze spill_build;
analyze spill_probe;

-- ============================================================================
-- A: INNER JOIN on various key types
-- ============================================================================

-- A1: INT key
set vector.hashjoin_spill_memory_mb = 0;
select count(*) as cnt, sum(length(b.col_pad)) as chk from spill_build b join spill_probe p on b.id = p.id;
set vector.hashjoin_spill_memory_mb = 1;
select count(*) as cnt, sum(length(b.col_pad)) as chk from spill_build b join spill_probe p on b.id = p.id;

-- A2: BIGINT key
set vector.hashjoin_spill_memory_mb = 0;
select count(*) as cnt, sum(length(b.col_pad)) as chk from spill_build b join spill_probe p on b.col_int8 = p.col_int8;
set vector.hashjoin_spill_memory_mb = 1;
select count(*) as cnt, sum(length(b.col_pad)) as chk from spill_build b join spill_probe p on b.col_int8 = p.col_int8;

-- A3: TEXT key
set vector.hashjoin_spill_memory_mb = 0;
select count(*) as cnt, sum(length(b.col_pad)) as chk from spill_build b join spill_probe p on b.col_text = p.col_text;
set vector.hashjoin_spill_memory_mb = 1;
select count(*) as cnt, sum(length(b.col_pad)) as chk from spill_build b join spill_probe p on b.col_text = p.col_text;

-- A4: FLOAT8 key
set vector.hashjoin_spill_memory_mb = 0;
select count(*) as cnt, sum(length(b.col_pad)) as chk from spill_build b join spill_probe p on b.col_float8 = p.col_float8;
set vector.hashjoin_spill_memory_mb = 1;
select count(*) as cnt, sum(length(b.col_pad)) as chk from spill_build b join spill_probe p on b.col_float8 = p.col_float8;

-- A5: DATE key
set vector.hashjoin_spill_memory_mb = 0;
select count(*) as cnt, sum(length(b.col_pad)) as chk from spill_build b join spill_probe p on b.col_date = p.col_date;
set vector.hashjoin_spill_memory_mb = 1;
select count(*) as cnt, sum(length(b.col_pad)) as chk from spill_build b join spill_probe p on b.col_date = p.col_date;

-- A6: NUMERIC key
set vector.hashjoin_spill_memory_mb = 0;
select count(*) as cnt, sum(length(b.col_pad)) as chk from spill_build b join spill_probe p on b.col_numeric = p.col_numeric;
set vector.hashjoin_spill_memory_mb = 1;
select count(*) as cnt, sum(length(b.col_pad)) as chk from spill_build b join spill_probe p on b.col_numeric = p.col_numeric;

-- ============================================================================
-- B: Various join types on INT key
-- ============================================================================

-- B1: LEFT JOIN
set vector.hashjoin_spill_memory_mb = 0;
select count(*) as cnt, sum(length(b.col_pad)) as chk from spill_probe p left join spill_build b on p.id = b.id;
set vector.hashjoin_spill_memory_mb = 1;
select count(*) as cnt, sum(length(b.col_pad)) as chk from spill_probe p left join spill_build b on p.id = b.id;

-- B2: RIGHT JOIN
set vector.hashjoin_spill_memory_mb = 0;
select count(*) as cnt, sum(length(b.col_pad)) as chk from spill_probe p right join spill_build b on p.id = b.id;
set vector.hashjoin_spill_memory_mb = 1;
select count(*) as cnt, sum(length(b.col_pad)) as chk from spill_probe p right join spill_build b on p.id = b.id;

-- B3: FULL OUTER JOIN
set vector.hashjoin_spill_memory_mb = 0;
select count(*) as cnt, sum(length(b.col_pad)) as chk from spill_build b full outer join spill_probe p on b.id = p.id;
set vector.hashjoin_spill_memory_mb = 1;
select count(*) as cnt, sum(length(b.col_pad)) as chk from spill_build b full outer join spill_probe p on b.id = p.id;

-- B4: SEMI JOIN (EXISTS)
set vector.hashjoin_spill_memory_mb = 0;
select count(*) as cnt, sum(length(p.col_pad)) as chk from spill_probe p where exists (select 1 from spill_build b where b.id = p.id);
set vector.hashjoin_spill_memory_mb = 1;
select count(*) as cnt, sum(length(p.col_pad)) as chk from spill_probe p where exists (select 1 from spill_build b where b.id = p.id);

-- B5: ANTI JOIN (NOT EXISTS)
set vector.hashjoin_spill_memory_mb = 0;
select count(*) as cnt, sum(length(b.col_pad)) as chk from spill_build b where not exists (select 1 from spill_probe p where p.id = b.id);
set vector.hashjoin_spill_memory_mb = 1;
select count(*) as cnt, sum(length(b.col_pad)) as chk from spill_build b where not exists (select 1 from spill_probe p where p.id = b.id);

-- ============================================================================
-- C: Composite keys
-- ============================================================================

-- C1: (INT, TEXT)
set vector.hashjoin_spill_memory_mb = 0;
select count(*) as cnt, sum(length(b.col_pad)) as chk from spill_build b join spill_probe p on b.id = p.id and b.col_text = p.col_text;
set vector.hashjoin_spill_memory_mb = 1;
select count(*) as cnt, sum(length(b.col_pad)) as chk from spill_build b join spill_probe p on b.id = p.id and b.col_text = p.col_text;

-- C2: (BIGINT, DATE)
set vector.hashjoin_spill_memory_mb = 0;
select count(*) as cnt, sum(length(b.col_pad)) as chk from spill_build b join spill_probe p on b.col_int8 = p.col_int8 and b.col_date = p.col_date;
set vector.hashjoin_spill_memory_mb = 1;
select count(*) as cnt, sum(length(b.col_pad)) as chk from spill_build b join spill_probe p on b.col_int8 = p.col_int8 and b.col_date = p.col_date;

-- C3: LEFT JOIN on (INT, VARCHAR, BOOL)
set vector.hashjoin_spill_memory_mb = 0;
select count(*) as cnt, sum(length(b.col_pad)) as chk from spill_probe p left join spill_build b on b.id = p.id and b.col_varchar = p.col_varchar and b.col_bool = p.col_bool;
set vector.hashjoin_spill_memory_mb = 1;
select count(*) as cnt, sum(length(b.col_pad)) as chk from spill_probe p left join spill_build b on b.id = p.id and b.col_varchar = p.col_varchar and b.col_bool = p.col_bool;

-- ============================================================================
-- D: Join with WHERE filter
-- ============================================================================

-- D1: INNER JOIN + filter
set vector.hashjoin_spill_memory_mb = 0;
select count(*) as cnt, sum(length(b.col_pad)) as chk from spill_build b join spill_probe p on b.id = p.id where b.col_int2 > 10000;
set vector.hashjoin_spill_memory_mb = 1;
select count(*) as cnt, sum(length(b.col_pad)) as chk from spill_build b join spill_probe p on b.id = p.id where b.col_int2 > 10000;

-- D2: LEFT JOIN + filter on nullable side
set vector.hashjoin_spill_memory_mb = 0;
select count(*) as cnt, sum(length(b.col_pad)) as chk from spill_probe p left join spill_build b on b.id = p.id where b.col_float8 > 50000.0 or b.col_float8 is null;
set vector.hashjoin_spill_memory_mb = 1;
select count(*) as cnt, sum(length(b.col_pad)) as chk from spill_probe p left join spill_build b on b.id = p.id where b.col_float8 > 50000.0 or b.col_float8 is null;

-- ============================================================================
-- E: Aggregation correctness
-- ============================================================================

-- E1: SUM after INNER JOIN
set vector.hashjoin_spill_memory_mb = 0;
select sum(b.col_float8) as s, sum(length(b.col_pad)) as chk from spill_build b join spill_probe p on b.id = p.id;
set vector.hashjoin_spill_memory_mb = 1;
select sum(b.col_float8) as s, sum(length(b.col_pad)) as chk from spill_build b join spill_probe p on b.id = p.id;

-- E2: SUM + AVG after LEFT JOIN
set vector.hashjoin_spill_memory_mb = 0;
select sum(b.col_numeric) as s, avg(b.col_float4) as a, sum(length(b.col_pad)) as chk from spill_probe p left join spill_build b on b.id = p.id;
set vector.hashjoin_spill_memory_mb = 1;
select sum(b.col_numeric) as s, avg(b.col_float4) as a, sum(length(b.col_pad)) as chk from spill_probe p left join spill_build b on b.id = p.id;

-- ============================================================================
-- F: NULL key handling
-- ============================================================================
drop table if exists spill_build_null, spill_probe_null;

create table spill_build_null (
    id   int,
    val  text,
    pad  text
) with (appendonly=true, orientation=column) distributed by (id);

create table spill_probe_null (
    id   int,
    val  text,
    pad  text
) with (appendonly=true, orientation=column) distributed by (id);

-- Build: 100K rows, 10% NULL keys
insert into spill_build_null
select
    case when i % 10 = 0 then null else i end,
    'bval_' || i,
    repeat('X', 150)
from generate_series(1, 100000) i;

-- Probe: 50K rows, 5% NULL keys
insert into spill_probe_null
select
    case when i % 20 = 0 then null else i end,
    'pval_' || i,
    repeat('Y', 100)
from generate_series(1, 50000) i;

analyze spill_build_null;
analyze spill_probe_null;

-- F1: INNER JOIN with NULLs (NULLs should not match)
set vector.hashjoin_spill_memory_mb = 0;
select count(*) as cnt, sum(length(b.pad)) as chk from spill_build_null b join spill_probe_null p on b.id = p.id;
set vector.hashjoin_spill_memory_mb = 1;
select count(*) as cnt, sum(length(b.pad)) as chk from spill_build_null b join spill_probe_null p on b.id = p.id;

-- F2: LEFT JOIN with NULLs
set vector.hashjoin_spill_memory_mb = 0;
select count(*) as cnt, count(b.id) as matched, sum(length(b.pad)) as chk from spill_probe_null p left join spill_build_null b on b.id = p.id;
set vector.hashjoin_spill_memory_mb = 1;
select count(*) as cnt, count(b.id) as matched, sum(length(b.pad)) as chk from spill_probe_null p left join spill_build_null b on b.id = p.id;

-- F3: FULL OUTER JOIN with NULLs
set vector.hashjoin_spill_memory_mb = 0;
select count(*) as cnt, sum(length(b.pad)) as chk from spill_build_null b full outer join spill_probe_null p on b.id = p.id;
set vector.hashjoin_spill_memory_mb = 1;
select count(*) as cnt, sum(length(b.pad)) as chk from spill_build_null b full outer join spill_probe_null p on b.id = p.id;

-- ============================================================================
-- G: Skewed data (triggers repartition)
-- ============================================================================
drop table if exists spill_build_skew, spill_probe_skew;

create table spill_build_skew (
    id   int,
    val  text,
    pad  text
) with (appendonly=true, orientation=column) distributed by (id);

create table spill_probe_skew (
    id   int,
    val  text,
    pad  text
) with (appendonly=true, orientation=column) distributed by (id);

-- Build: 80% rows have id=1 (extreme skew to trigger repartition)
insert into spill_build_skew
select
    case when i <= 80000 then 1 else i - 80000 end,
    'bval_' || i,
    repeat('S', 150)
from generate_series(1, 100000) i;

insert into spill_probe_skew
select i, 'pval_' || i, repeat('T', 100)
from generate_series(1, 50000) i;

analyze spill_build_skew;
analyze spill_probe_skew;

-- G1: INNER JOIN with skewed data
set vector.hashjoin_spill_memory_mb = 0;
select count(*) as cnt, sum(length(b.pad)) as chk from spill_build_skew b join spill_probe_skew p on b.id = p.id;
set vector.hashjoin_spill_memory_mb = 1;
select count(*) as cnt, sum(length(b.pad)) as chk from spill_build_skew b join spill_probe_skew p on b.id = p.id;

-- G2: LEFT JOIN with skewed data
set vector.hashjoin_spill_memory_mb = 0;
select count(*) as cnt, sum(length(b.pad)) as chk from spill_probe_skew p left join spill_build_skew b on b.id = p.id;
set vector.hashjoin_spill_memory_mb = 1;
select count(*) as cnt, sum(length(b.pad)) as chk from spill_probe_skew p left join spill_build_skew b on b.id = p.id;

-- ============================================================================
-- Cleanup
-- ============================================================================
drop table if exists spill_build, spill_probe;
drop table if exists spill_build_null, spill_probe_null;
drop table if exists spill_build_skew, spill_probe_skew;
reset vector.enable_vectorization;
reset vector.hashjoin_spill_memory_mb;
