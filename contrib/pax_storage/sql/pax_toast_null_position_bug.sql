-- ============================================================
-- Test: PAX Toast + NULL Position Bug Fix
-- ============================================================
--
-- BUG-1 (IsToast position mismatch):
--   PaxNonFixedColumn::GetDatum(position) used to receive a non-null
--   data index, but IsToast(position) checks toast_flat_map_ built
--   with absolute row positions.  When NULL rows appear before toast
--   rows, positions diverge and IsToast() returns false, leaving PAX
--   external toast pointers (va_tag=21) un-detoasted → SIGSEGV.
--
-- BUG-2 (AddToastIndex off-by-one):
--   AddToastIndex called Clear(total_rows_) instead of
--   Clear(index_of_toast), mis-marking the bitmap.
--
-- Fix (Approach D):
--   Unified GetDatum(size_t position, int null_counts = -1) so that
--   `position` is always the absolute row index.  Non-VEC columns
--   compute data_idx = position - null_counts internally.  IsToast()
--   now receives the correct absolute position.
--
-- This test covers both non-VEC (storage_format='porc') and VEC
-- (storage_format='porc_vec') storage formats.
--
-- Note: pax.min_size_of_external_toast default and minimum is 10MB
-- (10485760).  We use 11MB strings to exceed this threshold.
-- ============================================================

-- Cleanup
DROP TABLE IF EXISTS toast_bug_minimal;
DROP TABLE IF EXISTS toast_ok_no_nulls;
DROP TABLE IF EXISTS toast_ok_nulls_after;
DROP TABLE IF EXISTS toast_bug_nulls_before;
DROP TABLE IF EXISTS toast_bug_multi;
DROP TABLE IF EXISTS toast_bug_vacuum;
DROP TABLE IF EXISTS toast_vec_minimal;
DROP TABLE IF EXISTS toast_vec_no_nulls;
DROP TABLE IF EXISTS toast_vec_nulls_before;
DROP TABLE IF EXISTS toast_vec_multi;
DROP TABLE IF EXISTS toast_vec_vacuum;
DROP TABLE IF EXISTS toast_mg_porc_1;
DROP TABLE IF EXISTS toast_mg_porc_2;
DROP TABLE IF EXISTS toast_mg_porc_3;
DROP TABLE IF EXISTS toast_mg_vec_1;
DROP TABLE IF EXISTS toast_mg_vec_2;
DROP TABLE IF EXISTS toast_mg_vec_3;

-- Use PAX storage
SET default_table_access_method = pax;

-- Ensure toast is enabled
SET pax.enable_toast = on;

-- ============================================================
-- PART 1: Non-VEC (storage_format='porc') tests
-- ============================================================

-- Case 1: Minimal — 1 NULL before 1 toast row
CREATE TABLE toast_bug_minimal (
    id int,
    data text
) WITH (storage_format='porc') DISTRIBUTED REPLICATED;

INSERT INTO toast_bug_minimal VALUES
    (1, NULL),
    (2, repeat('A', 11000000));

SELECT id, length(data) FROM toast_bug_minimal ORDER BY id;

-- Case 2: No NULLs — positions always match
CREATE TABLE toast_ok_no_nulls (
    id int,
    data text
) WITH (storage_format='porc') DISTRIBUTED REPLICATED;

INSERT INTO toast_ok_no_nulls VALUES
    (1, 'small text'),
    (2, repeat('B', 11000000)),
    (3, 'another small');

SELECT id, length(data) FROM toast_ok_no_nulls ORDER BY id;

-- Case 3: NULLs after toast row — toast position not shifted
CREATE TABLE toast_ok_nulls_after (
    id int,
    data text
) WITH (storage_format='porc') DISTRIBUTED REPLICATED;

INSERT INTO toast_ok_nulls_after VALUES
    (1, repeat('C', 11000000)),
    (2, NULL),
    (3, NULL),
    (4, 'small');

SELECT id, length(data) FROM toast_ok_nulls_after ORDER BY id;

-- Case 4: Multiple NULLs before toast — larger offset
CREATE TABLE toast_bug_nulls_before (
    id int,
    data text
) WITH (storage_format='porc') DISTRIBUTED REPLICATED;

INSERT INTO toast_bug_nulls_before VALUES
    (1, NULL),
    (2, NULL),
    (3, NULL),
    (4, repeat('D', 11000000)),
    (5, 'short');

SELECT id, length(data) FROM toast_bug_nulls_before ORDER BY id;

-- Case 5: Mixed NULLs, normals, and toast
CREATE TABLE toast_bug_multi (
    id int,
    data text
) WITH (storage_format='porc') DISTRIBUTED REPLICATED;

INSERT INTO toast_bug_multi VALUES
    (1, 'hello'),
    (2, NULL),
    (3, 'world'),
    (4, NULL),
    (5, repeat('E', 11000000)),
    (6, 'end');

SELECT id, length(data) FROM toast_bug_multi ORDER BY id;

-- Case 6: VACUUM FULL — triggers scan → reinsert path
CREATE TABLE toast_bug_vacuum (
    id int,
    data text
) WITH (storage_format='porc') DISTRIBUTED REPLICATED;

INSERT INTO toast_bug_vacuum VALUES
    (1, NULL),
    (2, repeat('F', 11000000));

VACUUM FULL toast_bug_vacuum;
SELECT id, length(data) FROM toast_bug_vacuum ORDER BY id;

-- ============================================================
-- PART 2: VEC (storage_format='porc_vec') tests
-- ============================================================
-- VEC columns store null slots inline, so position == data index.
-- These tests verify that VEC toast+null works correctly and
-- the unified GetDatum signature doesn't regress VEC behavior.
-- ============================================================

-- Case 7: VEC — 1 NULL before 1 toast row
CREATE TABLE toast_vec_minimal (
    id int,
    data text
) WITH (storage_format='porc_vec') DISTRIBUTED REPLICATED;

INSERT INTO toast_vec_minimal VALUES
    (1, NULL),
    (2, repeat('G', 11000000));

SELECT id, length(data) FROM toast_vec_minimal ORDER BY id;

-- Case 8: VEC — No NULLs
CREATE TABLE toast_vec_no_nulls (
    id int,
    data text
) WITH (storage_format='porc_vec') DISTRIBUTED REPLICATED;

INSERT INTO toast_vec_no_nulls VALUES
    (1, 'small'),
    (2, repeat('H', 11000000)),
    (3, 'end');

SELECT id, length(data) FROM toast_vec_no_nulls ORDER BY id;

-- Case 9: VEC — Multiple NULLs before toast
CREATE TABLE toast_vec_nulls_before (
    id int,
    data text
) WITH (storage_format='porc_vec') DISTRIBUTED REPLICATED;

INSERT INTO toast_vec_nulls_before VALUES
    (1, NULL),
    (2, NULL),
    (3, NULL),
    (4, repeat('I', 11000000)),
    (5, 'short');

SELECT id, length(data) FROM toast_vec_nulls_before ORDER BY id;

-- Case 10: VEC — Mixed NULLs, normals, and toast
CREATE TABLE toast_vec_multi (
    id int,
    data text
) WITH (storage_format='porc_vec') DISTRIBUTED REPLICATED;

INSERT INTO toast_vec_multi VALUES
    (1, 'hello'),
    (2, NULL),
    (3, 'world'),
    (4, NULL),
    (5, repeat('J', 11000000)),
    (6, 'end');

SELECT id, length(data) FROM toast_vec_multi ORDER BY id;

-- Case 11: VEC — VACUUM FULL
CREATE TABLE toast_vec_vacuum (
    id int,
    data text
) WITH (storage_format='porc_vec') DISTRIBUTED REPLICATED;

INSERT INTO toast_vec_vacuum VALUES
    (1, NULL),
    (2, repeat('K', 11000000));

VACUUM FULL toast_vec_vacuum;
SELECT id, length(data) FROM toast_vec_vacuum ORDER BY id;

-- ============================================================
-- PART 3: Multi-group tests (small max_tuples_per_group)
-- ============================================================
-- With max_tuples_per_group = 5, rows are split across groups:
--   group 0: rows 0–4,  group 1: rows 5–9, ...
-- This verifies toast_flat_map_ is correctly built per-group
-- and IsToast uses group-internal row indices.
-- ============================================================

SET pax.max_tuples_per_group = 5;

-- Case 12: porc — NULLs + toast in group 0, normal data in group 1
CREATE TABLE toast_mg_porc_1 (
    id int,
    data text
) WITH (storage_format='porc') DISTRIBUTED REPLICATED;

INSERT INTO toast_mg_porc_1 VALUES
    (1, NULL),
    (2, NULL),
    (3, repeat('L', 11000000)),
    (4, 'a'),
    (5, 'b'),
    (6, 'c'),
    (7, NULL),
    (8, 'd');

SELECT id, length(data) FROM toast_mg_porc_1 ORDER BY id;

-- Case 13: porc — toast in group 1, NULLs spread across groups
CREATE TABLE toast_mg_porc_2 (
    id int,
    data text
) WITH (storage_format='porc') DISTRIBUTED REPLICATED;

INSERT INTO toast_mg_porc_2 VALUES
    (1, NULL),
    (2, 'a'),
    (3, NULL),
    (4, 'b'),
    (5, 'c'),
    (6, NULL),
    (7, repeat('M', 11000000)),
    (8, 'd');

SELECT id, length(data) FROM toast_mg_porc_2 ORDER BY id;

-- Case 14: porc — toast in both groups
CREATE TABLE toast_mg_porc_3 (
    id int,
    data text
) WITH (storage_format='porc') DISTRIBUTED REPLICATED;

INSERT INTO toast_mg_porc_3 VALUES
    (1, NULL),
    (2, repeat('N', 11000000)),
    (3, 'a'),
    (4, NULL),
    (5, 'b'),
    (6, NULL),
    (7, repeat('O', 12000000)),
    (8, 'c');

SELECT id, length(data) FROM toast_mg_porc_3 ORDER BY id;

-- Case 15: porc_vec — NULLs + toast in group 0, normal data in group 1
CREATE TABLE toast_mg_vec_1 (
    id int,
    data text
) WITH (storage_format='porc_vec') DISTRIBUTED REPLICATED;

INSERT INTO toast_mg_vec_1 VALUES
    (1, NULL),
    (2, NULL),
    (3, repeat('P', 11000000)),
    (4, 'a'),
    (5, 'b'),
    (6, 'c'),
    (7, NULL),
    (8, 'd');

SELECT id, length(data) FROM toast_mg_vec_1 ORDER BY id;

-- Case 16: porc_vec — toast in group 1, NULLs spread across groups
CREATE TABLE toast_mg_vec_2 (
    id int,
    data text
) WITH (storage_format='porc_vec') DISTRIBUTED REPLICATED;

INSERT INTO toast_mg_vec_2 VALUES
    (1, NULL),
    (2, 'a'),
    (3, NULL),
    (4, 'b'),
    (5, 'c'),
    (6, NULL),
    (7, repeat('Q', 11000000)),
    (8, 'd');

SELECT id, length(data) FROM toast_mg_vec_2 ORDER BY id;

-- Case 17: porc_vec — toast in both groups
CREATE TABLE toast_mg_vec_3 (
    id int,
    data text
) WITH (storage_format='porc_vec') DISTRIBUTED REPLICATED;

INSERT INTO toast_mg_vec_3 VALUES
    (1, NULL),
    (2, repeat('R', 11000000)),
    (3, 'a'),
    (4, NULL),
    (5, 'b'),
    (6, NULL),
    (7, repeat('S', 12000000)),
    (8, 'c');

SELECT id, length(data) FROM toast_mg_vec_3 ORDER BY id;

RESET pax.max_tuples_per_group;

-- ============================================================
-- Cleanup
-- ============================================================
DROP TABLE IF EXISTS toast_bug_minimal;
DROP TABLE IF EXISTS toast_ok_no_nulls;
DROP TABLE IF EXISTS toast_ok_nulls_after;
DROP TABLE IF EXISTS toast_bug_nulls_before;
DROP TABLE IF EXISTS toast_bug_multi;
DROP TABLE IF EXISTS toast_bug_vacuum;
DROP TABLE IF EXISTS toast_vec_minimal;
DROP TABLE IF EXISTS toast_vec_no_nulls;
DROP TABLE IF EXISTS toast_vec_nulls_before;
DROP TABLE IF EXISTS toast_vec_multi;
DROP TABLE IF EXISTS toast_vec_vacuum;
DROP TABLE IF EXISTS toast_mg_porc_1;
DROP TABLE IF EXISTS toast_mg_porc_2;
DROP TABLE IF EXISTS toast_mg_porc_3;
DROP TABLE IF EXISTS toast_mg_vec_1;
DROP TABLE IF EXISTS toast_mg_vec_2;
DROP TABLE IF EXISTS toast_mg_vec_3;

RESET pax.enable_toast;
RESET default_table_access_method;
