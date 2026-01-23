set optimizer=on;
create table s1(a int, b int, c int, d int);
create table s2(a int, b int, c int, d int);

explain select *
from
(select * from s1 where a < 0) q1,
(select * from s1 where a > 0) q2;

explain select *
from
(select sum(b) from s1 where a < 0) q1,
(select sum(b) from s1 where a > 0) q2;

explain select *
from
(select sum(b) from s1 where int4lt(a, 0)) q1,
(select sum(b) from s1 where int4eq(a, 0)) q2;

explain select *
from
(select sum(s1.b)
 from s1,s2
 where s1.d = s2.d and s1.a < 0) q1,
(select sum(s1.b)
 from s1,s2
 where s1.d = s2.d and s1.a > 0) q2;

explain select *
from
(select sum(s1.b)
 from s1,s2
 where s1.d = s2.d and s1.a < 0) q1(x),
(select sum(s1.b)
 from s1,s2
 where s1.d = s2.d and s1.a > 0) q2(x)
where q2.x > 1;

explain select *
from
(select sum(s1.b)
 from s1,s2
 where s1.d = s2.d and s1.a < 0) q1(x),
(select avg(s1.b)
 from s1,s2
 where s1.d = s2.d and s1.a > 0) q2(x)
where q2.x > 1;

-- falied, orca can not give a plan
explain select *
from
(select count(*)
 from s1,s2
 where s1.d = s2.d) q1(x),
(select count(*)
 from s1,s2
 where s1.d = s2.d) q2(x);

-- failed, ref CTranslatorScalarToDXL::TranslateAggrefToDXL aggfilter does not support
explain select *
from
(select sum(b) filter (where c < 0) from s1 where int4lt(a, 0)) q1,
(select sum(b) filter (where c < 0) from s1 where int4eq(a, 0)) q2;

-- failed, join order does not same
explain select *
from
(select sum(s1.b)
 from s1,s2
 where s1.d = s2.d and s1.a < 0) q1,
(select sum(s1.b)
 from s2,s1
 where s1.d = s2.d and s1.a > 0) q2;

--bug fix
select count(*) from (select 1) a, (select 1) b;

explain select *
from
(select sum(s1.b)
 from s1,s2
 where s1.d = s2.d and s1.a < 0) q1(x),
(select 1) a,
(select avg(s1.b)
 from s1,s2
 where s1.d = s2.d and s1.a > 0) q2(x),
(select 1) b
where q2.x > 1;

drop table s1, s2;
reset optimizer;
