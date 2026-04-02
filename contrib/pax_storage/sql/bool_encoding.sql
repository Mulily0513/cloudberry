-- Test Bool column encoding for PAX storage
-- Bool encoding uses Simple8b-RLE to pack boolean 0/1 values efficiently.
-- Note: Tests use single INSERT statements to avoid pre-existing PAX issues.

-- ============================================================
-- Basic bool encoding tests
-- ============================================================

-- Basic bool column with bool encoding
create table t_bool_basic(a int, b bool encoding(compresstype=bool)) using pax;
insert into t_bool_basic select i, (i % 2 = 0) from generate_series(1, 1000) i;
select count(*) from t_bool_basic;
select count(*) filter (where b = true) as true_count,
       count(*) filter (where b = false) as false_count
from t_bool_basic;
-- Verify specific rows
select b from t_bool_basic where a = 1;
select b from t_bool_basic where a = 2;
select b from t_bool_basic where a = 1000;
drop table t_bool_basic;

-- All true values
create table t_bool_true(a int, b bool encoding(compresstype=bool)) using pax;
insert into t_bool_true select i, true from generate_series(1, 1000) i;
select count(*) from t_bool_true;
select count(distinct b), min(b::int), max(b::int) from t_bool_true;
drop table t_bool_true;

-- All false values
create table t_bool_false(a int, b bool encoding(compresstype=bool)) using pax;
insert into t_bool_false select i, false from generate_series(1, 1000) i;
select count(*) from t_bool_false;
select count(distinct b), min(b::int), max(b::int) from t_bool_false;
drop table t_bool_false;

-- ============================================================
-- NULL position tests (P0: null bitmap + bool encoding interaction)
-- 4 patterns: first position, last position, consecutive, scattered
-- ============================================================

-- NULL at first position
create table t_bool_null_first(a int, b bool encoding(compresstype=bool)) using pax;
insert into t_bool_null_first select i, case when i = 1 then null else (i % 2 = 0) end from generate_series(1, 100) i;
select count(*) from t_bool_null_first;
select count(b) from t_bool_null_first;
select b from t_bool_null_first where a = 1;
select b from t_bool_null_first where a = 2;
drop table t_bool_null_first;

-- NULL at last position
create table t_bool_null_last(a int, b bool encoding(compresstype=bool)) using pax;
insert into t_bool_null_last select i, case when i = 100 then null else (i % 2 = 0) end from generate_series(1, 100) i;
select count(*) from t_bool_null_last;
select count(b) from t_bool_null_last;
select b from t_bool_null_last where a = 99;
select b from t_bool_null_last where a = 100;
drop table t_bool_null_last;

-- Consecutive NULLs in the middle
create table t_bool_null_consec(a int, b bool encoding(compresstype=bool)) using pax;
insert into t_bool_null_consec select i, case when i between 40 and 60 then null else (i % 2 = 0) end from generate_series(1, 100) i;
select count(*) from t_bool_null_consec;
select count(b) from t_bool_null_consec;
select count(*) from t_bool_null_consec where b is null;
drop table t_bool_null_consec;

-- High-density scattered NULLs (every other value)
create table t_bool_null_scatter(a int, b bool encoding(compresstype=bool)) using pax;
insert into t_bool_null_scatter select i, case when i % 2 = 0 then null else (i % 3 = 0) end from generate_series(1, 200) i;
select count(*) from t_bool_null_scatter;
select count(b) from t_bool_null_scatter;
select count(*) from t_bool_null_scatter where b is null;
select b from t_bool_null_scatter where a = 1;
select b from t_bool_null_scatter where a = 3;
drop table t_bool_null_scatter;

-- ============================================================
-- Larger dataset test
-- ============================================================

-- 10000 rows with alternating pattern
create table t_bool_large(a int, b bool encoding(compresstype=bool)) using pax;
insert into t_bool_large select i, (i % 2 = 0) from generate_series(1, 10000) i;
select count(*) from t_bool_large;
select count(*) filter (where b = true) as true_count,
       count(*) filter (where b = false) as false_count
from t_bool_large;
drop table t_bool_large;

-- ============================================================
-- Mixed pattern tests
-- ============================================================

-- Long runs of same value (good for RLE compression)
create table t_bool_runs(a int, b bool encoding(compresstype=bool)) using pax;
insert into t_bool_runs select i, (i <= 500) from generate_series(1, 1000) i;
select count(*) from t_bool_runs;
select count(*) filter (where b = true) as true_count,
       count(*) filter (where b = false) as false_count
from t_bool_runs;
-- Verify boundary
select b from t_bool_runs where a = 500;
select b from t_bool_runs where a = 501;
drop table t_bool_runs;

-- Random-like pattern (every 3rd is true)
create table t_bool_pattern(a int, b bool encoding(compresstype=bool)) using pax;
insert into t_bool_pattern select i, (i % 3 = 0) from generate_series(1, 900) i;
select count(*) from t_bool_pattern;
select count(*) filter (where b = true) as true_count,
       count(*) filter (where b = false) as false_count
from t_bool_pattern;
drop table t_bool_pattern;

-- Note: porc_vec format is not tested for bool encoding because
-- the vec format uses Arrow BooleanArray (bitpacked, 1-bit per value)
-- but the encoding decoder outputs 1-byte per value.
-- This is a vec format architectural limitation, not an encoding bug.

-- ============================================================
-- Compression ratio test
-- ============================================================

-- Uncompressed reference
create table t_bool_unencoded(a int, b bool) using pax;
insert into t_bool_unencoded select i, (i % 2 = 0) from generate_series(1, 100000) i;

-- Encoded table
create table t_bool_encoded(a int, b bool encoding(compresstype=bool)) using pax;
insert into t_bool_encoded select * from t_bool_unencoded;

-- Verify data integrity
select count(*) as heap_count from t_bool_unencoded;
select count(*) as encoded_count from t_bool_encoded;
-- Bit-exact EXCEPT comparison
select count(*) as data_mismatches from (
  select * from t_bool_unencoded except select * from t_bool_encoded) sub;

-- Check compression ratio (use inequality to avoid fragile hardcoded sizes)
select pg_relation_size('t_bool_encoded') < pg_relation_size('t_bool_unencoded') as compressed;

drop table t_bool_unencoded;
drop table t_bool_encoded;

-- ============================================================
-- Cross-validation: heap → PAX EXCEPT (independent oracle)
-- ============================================================

create table t_bool_xval_heap(a int, b bool);
insert into t_bool_xval_heap select i,
  case when i % 3 = 0 then true when i % 3 = 1 then false else null end
  from generate_series(1, 10000) i;
create table t_bool_xval_pax(a int, b bool encoding(compresstype=bool)) using pax;
insert into t_bool_xval_pax select * from t_bool_xval_heap;
select count(*) as missing_rows from (
  select * from t_bool_xval_heap except select * from t_bool_xval_pax) sub;
select count(*) as extra_rows from (
  select * from t_bool_xval_pax except select * from t_bool_xval_heap) sub;
drop table t_bool_xval_heap;
drop table t_bool_xval_pax;

-- ============================================================
-- All-NULL column test (0 values to encoder, edge case)
-- ============================================================

create table t_bool_allnull(a int, b bool encoding(compresstype=bool)) using pax;
insert into t_bool_allnull select i, null from generate_series(1, 100) i;
select count(*) from t_bool_allnull;
select count(b) as non_null from t_bool_allnull;
drop table t_bool_allnull;

-- ============================================================
-- Error case tests
-- ============================================================

-- Bool encoding on non-bool column should fail on INSERT (validation is at write time)
create table t_bool_int_err(a int, b int encoding(compresstype=bool)) using pax;
insert into t_bool_int_err values (1, 42);
drop table t_bool_int_err;

-- Bool encoding with porc_vec format should fail on INSERT
create table t_bool_vec_err(a int, b bool encoding(compresstype=bool)) using pax with(storage_format=porc_vec);
insert into t_bool_vec_err values (1, true);
drop table t_bool_vec_err;
