-- This test captures the non-vectorized plan shape for PAX dictionary encoding,
-- so force vectorization off regardless of the PGOPTIONS passed by the harness
-- (pax-test runs the schedule twice: once with vector.enable_vectorization off,
-- once with it on; both passes share this expected output).
set vector.enable_vectorization=off;
create table t_dict1(a int, b text) using pax with(storage_format=porc,compresstype=dict);
insert into t_dict1 select 1, repeat('1b', 12345678) from generate_series(1,20)i;

explain select count(*) from t_dict1;
select count(*) from t_dict1;

explain select count(b) from t_dict1;
select count(b) from t_dict1;

drop table t_dict1;

create table t_dict1(a int, b text) using pax with(storage_format=porc_vec,compresstype=dict);
insert into t_dict1 select 1, repeat('1b', 12345678) from generate_series(1,20)i;

explain select count(*) from t_dict1;
select count(*) from t_dict1;

explain select count(b) from t_dict1;
select count(b) from t_dict1;

drop table t_dict1;
