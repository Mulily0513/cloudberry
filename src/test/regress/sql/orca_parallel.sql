create schema orca_parallel;
set search_path=orca_parallel, public;
set statement_mem = '256MB';

create table t1(a int, b int) with(parallel_workers=2) distributed by (a);
create table t2(c int, d int ) with(parallel_workers=3) distributed by (c);
insert into t1 select i, i+1 from generate_series(1, 1000)i;
insert into t2 select i, i+2 from generate_series(1, 20000)i;
analyze t1;
analyze t2;

set parallel_setup_cost=0;
set max_parallel_workers_per_gather=4;
set enable_parallel = on;

explain (verbose, costs off) select * from t1  join t2  on t1.a = t2.c;
explain (verbose, costs off) select * from t1  join t2  on t1.a = t2.d;
explain (verbose, costs off) select * from t1  join t2  on t1.b = t2.c;
explain (verbose, costs off) select * from t1  join t2  on t1.b = t2.d;

-- Redistribute Motion
alter table t2 set (parallel_workers=2);
set max_parallel_workers_per_gather=2;
explain (verbose, costs off) select * from t1  join t2  on t1.a = t2.c;
explain (verbose, costs off) select * from t1  join t2  on t1.a = t2.d;
explain (verbose, costs off) select * from t1  join t2  on t1.b = t2.c;
explain (verbose, costs off) select * from t1  join t2  on t1.b = t2.d;

-- Left Join
explain (verbose, costs off) select * from t1 left join t2  on t1.a = t2.c;
-- Right Join
explain (verbose, costs off) select * from t1 right join t2 on t1.a = t2.c;
-- Full Join
explain (verbose, costs off) select * from t1 full join t2  on t1.a = t2.c;
-- Semi Join
explain (verbose, costs off) select *  from t1 where exists (select 1 from t2 where t2.c = t1.a);

create table t3_null(c2 int) distributed randomly;
insert into t3_null values (1), (2), (3), (null), (5), (null), (10), (20);
analyze t3_null;
-- Anti Semi Join (not-in)
explain (costs off) select * from t1 where a not in (select c2 from t3_null);

-- Broadcast Motion
drop table if exists t1;
drop table if exists t2;
create table t1(a int, b int) with(parallel_workers=2) distributed by (a);
create table t2(c int, d int ) with(parallel_workers=2) distributed by (c);
insert into t1 select i, i+1 from generate_series(1, 100)i;
insert into t2 select i, i+2 from generate_series(1, 30000)i;
analyze t1;
analyze t2;
explain (verbose, costs off) select * from t1  join t2  on t1.a = t2.c;
explain (verbose, costs off) select * from t1  join t2  on t1.a = t2.d;
explain (verbose, costs off) select * from t1  join t2  on t1.b = t2.c;
explain (verbose, costs off) select * from t1  join t2  on t1.b = t2.d;

-- Hash Agg
drop table if exists t0;

create table t0 (
    a int,
    b int
) distributed by (b);

insert into t0
select i % 10000, i 
from generate_series(1, 5000000) i;

analyze t0;

explain (costs off) select a, count(*) from t0 group by a;
explain (costs off) select b, count(*) from t0 group by b;

-- Group Agg
set optimizer_enable_hashagg = off;
explain (costs off) select a, count(*) from t0 group by a;
explain (costs off) select b, count(*) from t0 group by b;
reset optimizer_enable_hashagg;

-- DQA
explain (costs off) select b, count(distinct a) from t1 group by b;

-- No-Group-by Agg
explain (costs off) select count(*), sum(b) from t0;


explain (costs off)
select * from t1 where a in (
    select c from t2 limit 10
);

-- Subquery with LIMIT and ORDER BY should NOT use parallel scan
explain (costs off)
select * from t1 where a in (
    select c from t2 order by c limit 10
);

-- Top-level LIMIT should still allow parallel scan (query_level = 0)
explain (costs off) select * from t2 limit 100;

-- Nested subquery with LIMIT - inner limit should prevent parallel scan
explain (costs off)
select * from t1 where a in (
    select c from t2 where d in (
        select a from t1 limit 5
    )
);

-- parallel index scan test
create table pidx1(a int, b int) with (parallel_workers=2) distributed by (a);
create table pidx2(c int, d int) with (parallel_workers=2) distributed by (c);
create table pidx3(e int, f int) with (parallel_workers=2) distributed by (e);

create index pidx1_a_idx on pidx1(a);
create index pidx1_b_idx on pidx1(b);
create index pidx2_c_idx on pidx2(c);
create index pidx2_d_idx on pidx2(d);
create index pidx3_e_idx on pidx3(e);
create index pidx3_f_idx on pidx3(f);

insert into pidx1 select i, i from generate_series(1, 10000) i;
insert into pidx2 select i, i from generate_series(1, 200000) i;
insert into pidx3 select i, i from generate_series(1, 300) i;

analyze pidx1;
analyze pidx2;
analyze pidx3;

-- parallel index scan(simple)
explain (verbose, costs off) select * from pidx1 where a < 1000;
select count(*) from pidx1 where a < 1000;

explain (verbose, costs off) select * from pidx1 where b < 1000;
select count(*) from pidx1 where b < 1000;

-- parallel index scan(no redistribute motion)
explain (verbose, costs off) select * from pidx1 left join pidx2 on pidx1.a = pidx2.c where pidx1.a < 1000;
select count(*) from pidx1 left join pidx2 on pidx1.a = pidx2.c where pidx1.a < 1000;

-- parallel index scan(redistribute motion)
explain (verbose, costs off) select * from pidx1 left join pidx2 on pidx1.b = pidx2.c where pidx1.b < 1000;
select count(*) from pidx1 left join pidx2 on pidx1.b = pidx2.c where pidx1.b < 1000;

-- parallel index scan(broadcast motion)
explain (verbose, costs off) select * from pidx1 join pidx3 on pidx1.a = pidx3.f where pidx1.a < 5000;
select count(*) from pidx1 join pidx3 on pidx1.a = pidx3.f where pidx1.a < 5000;

-- parallel index scan(with limit)
explain (verbose, costs off) select * from pidx1 where a < 1000 limit 10;
select count(*) from pidx1 where a < 1000 limit 10;

-- parallel index scan(with order and limit)
explain (verbose, costs off) select * from pidx1 where a < 100000 order by a limit 10;
select * from pidx1 where a < 100000 order by a limit 10;

-- parallel index scan inside IN subquery
set enable_hashagg = off;
explain (verbose, costs off) select * from pidx1 where a in (select c from pidx2 where c < 500);
select count(*) from pidx1 where a in (select c from pidx2 where c < 500);
reset enable_hashagg;

-- Subquery with LIMIT and ORDER BY should NOT use parallel scan
explain (verbose, costs off) select * from pidx1 where a in (select c from pidx2 order by c limit 10);
select * from pidx1 where a in (select c from pidx2 order by c limit 10) order by a;

-- left join
explain (verbose, costs off) select * from pidx1 left join pidx2 on pidx1.a = pidx2.c where pidx1.a < 1000;
select count(*) from pidx1 left join pidx2 on pidx1.a = pidx2.c where pidx1.a < 1000;

-- right join
explain (verbose, costs off) select * from pidx1 right join pidx2 on pidx1.a = pidx2.c where pidx2.c < 1000;
select count(*) from pidx1 right join pidx2 on pidx1.a = pidx2.c where pidx2.c < 1000;

-- semi join with parallel index scan
explain (verbose, costs off) select * from pidx1 where exists (select 1 from pidx2 where pidx2.c = pidx1.a);
select count(*) from pidx1 where exists (select 1 from pidx2 where pidx2.c = pidx1.a);

-- anti semi join with parallel index scan
explain (verbose, costs off) select * from pidx1 where a not in (select c from pidx2 where c < 1000);
select count(*) from pidx1 where a not in (select c from pidx2 where c < 1000);

-- parallel index scan with hash aggregation
explain (verbose, costs off) select a, count(*) from pidx1 where a < 1000 group by a;
select count(*) from (select a, count(*) from pidx1 where a < 1000 group by a) s;

-- Nested subquery with LIMIT - inner limit should prevent parallel index scan
explain (costs off)
select * from pidx1 where a in (
    select c from pidx2 where c in (
        select a from pidx1 limit 5
    )
);

-- GUC to control plan
set optimizer_enable_indexscan to off;
explain (verbose, costs off) select * from pidx1 where a < 1000;

reset optimizer_enable_indexscan;
reset enable_parallel;
reset max_parallel_workers_per_gather;
reset parallel_setup_cost;
reset statement_mem;

-- start_ignore
drop schema orca_parallel cascade;
-- end_ignore