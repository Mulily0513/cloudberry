-- Test Gorilla column encoding for PAX storage
-- Gorilla: XOR-based float compression (Pelkonen et al., VLDB 2015).
-- Optimal for slowly-changing float metric sequences.
-- Note: Tests use single INSERT statements and moderate row counts
-- to avoid pre-existing PAX issues with multi-INSERT and large tables.

-- ============================================================
-- Basic type tests
-- ============================================================

-- float8 Gorilla encoding (deterministic data for reproducible tests)
create table t_gor_f8(a int, val float8 encoding(compresstype=gorilla)) using pax;
insert into t_gor_f8 select i, sin(i::float8 / 10.0) * 50.0 + 50.0 from generate_series(1, 1000) i;
select count(*) from t_gor_f8;
-- Verify specific values roundtrip correctly
select round(val::numeric, 4) from t_gor_f8 where a = 1;
select round(val::numeric, 4) from t_gor_f8 where a = 500;
drop table t_gor_f8;

-- float4 Gorilla encoding
create table t_gor_f4(a int, val float4 encoding(compresstype=gorilla)) using pax;
insert into t_gor_f4 select i, (i * 0.1)::float4 from generate_series(1, 500) i;
select count(*) from t_gor_f4;
select val from t_gor_f4 where a = 1;
select val from t_gor_f4 where a = 500;
drop table t_gor_f4;

-- Gorilla on integer column (positive test: XOR on bit patterns works for any fixed-width type)
create table t_gor_int(a int, b int encoding(compresstype=gorilla)) using pax;
insert into t_gor_int select i, i * 100 from generate_series(1, 100) i;
select count(*) from t_gor_int;
select min(b), max(b) from t_gor_int;
drop table t_gor_int;

-- ============================================================
-- Value pattern tests
-- ============================================================

-- Similar adjacent values (CPU metric simulation, good for Gorilla)
-- Deterministic: slow sine wave ± small perturbation
create table t_gor_cpu(a int, usage float8 encoding(compresstype=gorilla)) using pax;
insert into t_gor_cpu select i, 55.0 + sin(i::float8 / 50.0) * 2.0 from generate_series(1, 10000) i;
select count(*) from t_gor_cpu;
-- Verify exact values at known points
select round(usage::numeric, 4) from t_gor_cpu where a = 1;
select round(usage::numeric, 4) from t_gor_cpu where a = 5000;
drop table t_gor_cpu;

-- All same values (XOR = 0 for all, best-case compression)
create table t_gor_same(a int, val float8 encoding(compresstype=gorilla)) using pax;
insert into t_gor_same select i, 42.0 from generate_series(1, 1000) i;
select count(*) from t_gor_same;
select min(val), max(val), count(distinct val) from t_gor_same;
drop table t_gor_same;

-- ============================================================
-- NULL position tests (P0: null bitmap + XOR encoding interaction)
-- 4 patterns: first position, last position, consecutive, scattered
-- ============================================================

-- NULL at first position (first_value is NULL, affects XOR base)
create table t_gor_null_first(a int, val float8 encoding(compresstype=gorilla)) using pax;
insert into t_gor_null_first select i, case when i = 1 then null else i * 1.1 end from generate_series(1, 100) i;
select count(*) from t_gor_null_first;
select count(val) from t_gor_null_first;
select val from t_gor_null_first where a = 1;
select val from t_gor_null_first where a = 2;
select val from t_gor_null_first where a = 100;
drop table t_gor_null_first;

-- NULL at last position
create table t_gor_null_last(a int, val float8 encoding(compresstype=gorilla)) using pax;
insert into t_gor_null_last select i, case when i = 100 then null else i * 1.1 end from generate_series(1, 100) i;
select count(*) from t_gor_null_last;
select count(val) from t_gor_null_last;
select val from t_gor_null_last where a = 99;
select val from t_gor_null_last where a = 100;
drop table t_gor_null_last;

-- Consecutive NULLs in the middle
create table t_gor_null_consec(a int, val float8 encoding(compresstype=gorilla)) using pax;
insert into t_gor_null_consec select i, case when i between 40 and 60 then null else sin(i::float8) * 100 end from generate_series(1, 100) i;
select count(*) from t_gor_null_consec;
select count(val) from t_gor_null_consec;
select count(*) from t_gor_null_consec where val is null;
drop table t_gor_null_consec;

-- High-density scattered NULLs (every other value)
create table t_gor_null_scatter(a int, val float8 encoding(compresstype=gorilla)) using pax;
insert into t_gor_null_scatter select i, case when i % 2 = 0 then null else i * 1.1 end from generate_series(1, 200) i;
select count(*) from t_gor_null_scatter;
select count(val) from t_gor_null_scatter;
select count(*) from t_gor_null_scatter where val is null;
-- Verify non-null values are correct
select val from t_gor_null_scatter where a = 1;
select val from t_gor_null_scatter where a = 3;
select val from t_gor_null_scatter where a = 199;
drop table t_gor_null_scatter;

-- ============================================================
-- Bit-exact roundtrip tests (no randomness)
-- ============================================================

-- Exact float8 values: verify bit-exact roundtrip
create table t_gor_exact(a int, val float8 encoding(compresstype=gorilla)) using pax;
insert into t_gor_exact
  select * from (values
    (1, 3.14159265358979),
    (2, 2.71828182845905),
    (3, 1.41421356237310),
    (4, 0.0),
    (5, -0.0::float8),
    (6, 1e308),
    (7, -1e308),
    (8, 5e-324)
  ) as v(a, val);
select a, val from t_gor_exact order by a;
drop table t_gor_exact;

-- Subnormal float8 values (near smallest representable, tests XOR edge cases)
create table t_gor_subnormal(a int, val float8 encoding(compresstype=gorilla)) using pax;
insert into t_gor_subnormal
  select * from (values
    (1, 5e-324),               -- smallest positive subnormal
    (2, 2.2250738585072014e-308), -- smallest positive normal
    (3, 1e-300),
    (4, -5e-324),
    (5, -2.2250738585072014e-308),
    (6, 1.7976931348623157e+308)  -- largest finite float8
  ) as v(a, val);
select a, val from t_gor_subnormal order by a;
drop table t_gor_subnormal;

-- ============================================================
-- IEEE 754 special values (NaN, Infinity)
-- ============================================================

-- float8 special values: NaN, Infinity, -Infinity
-- (from TSDB compression_algos.sql FLOAT8 special values test)
create table t_gor_special(a int, val float8 encoding(compresstype=gorilla)) using pax;
insert into t_gor_special
  select * from (values
    (1, 'NaN'::float8),
    (2, 'Infinity'::float8),
    (3, '-Infinity'::float8),
    (4, 0::float8),
    (5, 1.0::float8),
    (6, -1.0::float8)
  ) as v(a, val);
select count(*) from t_gor_special;
-- NaN != NaN in SQL, so use val::text to verify
select a, val::text from t_gor_special order by a;
drop table t_gor_special;

-- float4 special values
-- (from TSDB compression_algos.sql FLOAT4 special values test)
create table t_gor_f4_special(a int, val float4 encoding(compresstype=gorilla)) using pax;
insert into t_gor_f4_special
  select * from (values
    (1, 'NaN'::float4),
    (2, 'Infinity'::float4),
    (3, '-Infinity'::float4),
    (4, 0::float4),
    (5, 1.0::float4)
  ) as v(a, val);
select count(*) from t_gor_f4_special;
select a, val::text from t_gor_f4_special order by a;
drop table t_gor_f4_special;

-- Mixed NaN and normal values in larger dataset
-- (ensures NaN doesn't corrupt subsequent XOR decoding)
create table t_gor_nan_mix(a int, val float8 encoding(compresstype=gorilla)) using pax;
insert into t_gor_nan_mix
select i, case
  when i % 100 = 0 then 'NaN'::float8
  when i % 200 = 50 then 'Infinity'::float8
  else sin(i::float8 / 10.0) * 100.0
end
from generate_series(1, 1000) i;
select count(*) from t_gor_nan_mix;
select count(*) filter (where val::text = 'NaN') as nan_count,
       count(*) filter (where val = 'Infinity'::float8) as inf_count,
       count(*) filter (where val::text != 'NaN' and val != 'Infinity'::float8) as normal_count
from t_gor_nan_mix;
drop table t_gor_nan_mix;

-- ============================================================
-- Order-preserving verification (P0: critical for time-series)
-- ============================================================

-- Verify float8 ordering is preserved after Gorilla roundtrip
-- Uses deterministic sin() pattern, not random
create table t_gor_order(a int, val float8 encoding(compresstype=gorilla)) using pax;
insert into t_gor_order select i, i * 1.1 from generate_series(1, 10000) i;
-- count rows where monotonic order is violated (should be 0)
select count(*) as order_violations from (
  select val, lead(val) over (order by a) as next_val from t_gor_order
) sub where next_val <= val;
drop table t_gor_order;

-- ============================================================
-- Boundary tests
-- ============================================================

-- Single row (only first_value stored, no XOR)
create table t_gor_single(a int, val float8 encoding(compresstype=gorilla)) using pax;
insert into t_gor_single values (1, 3.14);
select * from t_gor_single;
drop table t_gor_single;

-- Two rows (only one XOR, no window reuse)
create table t_gor_two(a int, val float8 encoding(compresstype=gorilla)) using pax;
insert into t_gor_two select * from (values (1, 3.14), (2, 2.72)) as v(a, val);
select a, val from t_gor_two order by a;
drop table t_gor_two;

-- ============================================================
-- porc_vec format test
-- ============================================================

create table t_gor_vec(a int, val float8 encoding(compresstype=gorilla)) using pax with(storage_format=porc_vec);
insert into t_gor_vec select i, i * 1.1 from generate_series(1, 1000) i;
select count(*) from t_gor_vec;
select val from t_gor_vec where a = 1;
select val from t_gor_vec where a = 1000;
drop table t_gor_vec;

-- ============================================================
-- Combined DeltaDelta + Gorilla (time-series table pattern)
-- Using 2-column tables to avoid pre-existing PAX 3-column issue
-- ============================================================

create table t_ts_dg(
  ts    timestamp encoding(compresstype=deltadelta),
  val   float8    encoding(compresstype=gorilla)
) using pax;

insert into t_ts_dg
select '2024-01-01'::timestamp + (i || ' seconds')::interval,
       50.0 + sin(i::float8 / 100.0) * 5.0
from generate_series(1, 10000) i;

select count(*) from t_ts_dg;
select min(ts), max(ts) from t_ts_dg;
select round(avg(val)::numeric, 1) between 49 and 51 as avg_in_range from t_ts_dg;

drop table t_ts_dg;

-- ============================================================
-- Compression ratio test (sorted data, optimal for DeltaDelta+Gorilla)
-- ============================================================

-- Create uncompressed reference table
create table t_ratio_unencoded(
  ts    timestamp,
  val   float8
) using pax;

insert into t_ratio_unencoded
select '2024-01-01'::timestamp + (i || ' seconds')::interval,
       50.0 + sin(i::float8 / 100.0) * 5.0
from generate_series(1, 100000) i;

-- Create encoded table with same data (deterministic, no random)
create table t_ratio_encoded(
  ts    timestamp encoding(compresstype=deltadelta),
  val   float8    encoding(compresstype=gorilla)
) using pax;

insert into t_ratio_encoded
select * from t_ratio_unencoded;

-- Verify data integrity
select count(*) as heap_count from t_ratio_unencoded;
select count(*) as encoded_count from t_ratio_encoded;
-- Bit-exact EXCEPT comparison (no tolerance needed for deterministic data)
select count(*) as data_mismatches from (
  select * from t_ratio_unencoded except select * from t_ratio_encoded) sub;

-- Check compression ratio (use inequality to avoid fragile hardcoded sizes)
select pg_relation_size('t_ratio_encoded') < pg_relation_size('t_ratio_unencoded') as compressed;

drop table t_ratio_unencoded;
drop table t_ratio_encoded;

-- ============================================================
-- Cross-validation: heap → PAX EXCEPT (independent oracle)
-- ============================================================

create table t_gor_xval_heap(a int, val float8);
insert into t_gor_xval_heap select i, sin(i::float8 / 100.0) * 1000.0
  from generate_series(1, 10000) i;
create table t_gor_xval_pax(a int, val float8 encoding(compresstype=gorilla)) using pax;
insert into t_gor_xval_pax select * from t_gor_xval_heap;
select count(*) as missing_rows from (
  select * from t_gor_xval_heap except select * from t_gor_xval_pax) sub;
select count(*) as extra_rows from (
  select * from t_gor_xval_pax except select * from t_gor_xval_heap) sub;
drop table t_gor_xval_heap;
drop table t_gor_xval_pax;

-- ============================================================
-- All-NULL column test (0 values to encoder, edge case)
-- ============================================================

create table t_gor_allnull(a int, val float8 encoding(compresstype=gorilla)) using pax;
insert into t_gor_allnull select i, null from generate_series(1, 100) i;
select count(*) from t_gor_allnull;
select count(val) as non_null from t_gor_allnull;
drop table t_gor_allnull;

-- ============================================================
-- Error case tests
-- ============================================================

-- Gorilla on text column should fail on INSERT
create table t_gor_text_err(a int, b text encoding(compresstype=gorilla)) using pax;
insert into t_gor_text_err values (1, 'hello');
drop table t_gor_text_err;
