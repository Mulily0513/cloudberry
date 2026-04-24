#!/usr/bin/env bash
# load/load.sh — generate dbgen data at the requested scale factor and load
# it into the iceberg AM tables via a staging heap.
#
# Contract:
#   - Caller (../run.sh) has already created $DB, run ddl/01/02/03,
#     exported PGPORT / PGHOST / PGUSER / PGDATABASE=$DB, and cd'd here.
#   - $TPC_SCALE is set (integer, 1/10/100/1000).
#   - $RAW is the raw-data directory where .tbl files live (sentinel included).
#   - dbgen must be on PATH (symlink at automation/tools/tpc/bin/dbgen).
#
# Strategy:
#   1. dbgen -s $SF -T <letter> per table, writing into $RAW. dbgen reads
#      dists.dss from its cwd so we cd into its source dir before invoking.
#   2. Strip the trailing `|` that dbgen emits on each line (PG's CSV parser
#      otherwise sees an extra empty column).
#   3. \COPY stage.$tbl FROM '$RAW/$tbl.tbl'  (delimiter '|', header off).
#   4. INSERT INTO public.$tbl SELECT * FROM stage.$tbl  — this is the
#      iceberg AM writer path we actually want to stress.
#   5. ANALYZE per table (not the whole DB — pg_statistic noise).
#   6. Touch sentinel $RAW/.loaded-$DB so subsequent runs can skip.
#
# Env:
#   KEEP_STAGE=1      keep stage.* around (debugging)
#   FORCE_REGEN=1     regenerate .tbl even if present

set -euo pipefail

: "${TPC_SCALE:?}"
: "${RAW:?}"
: "${DB:?}"
: "${DBGEN_BIN:?}"
: "${DBGEN_SRC:?}"

PSQL="psql -v ON_ERROR_STOP=1 -q"

# Table list (matches dbgen -T letters). Order: dims first, then facts.
#   -T n : nation (dim)       1 .tbl
#   -T r : region             (piggybacked with n)
#   -T s : supplier
#   -T P : part (without partsupp)
#   -T S : partsupp
#   -T c : customer
#   -T O : orders (without lineitem)
#   -T L : lineitem
TABLES=(nation region part supplier partsupp customer orders lineitem)

# dbgen letter per table (uppercase is "this table alone").
declare -A LETTER=(
    [nation]=n   [region]=r   [part]=P      [supplier]=s
    [partsupp]=S [customer]=c [orders]=O    [lineitem]=L
)

mkdir -p "$RAW"

strip_trailing_pipe() {
    # dbgen writes lines like "...|...|\n" — PG CSV would treat the trailing
    # empty as an extra column. sed is measurably faster than awk here.
    sed -i 's/|$//' "$1"
}

gen_one_table() {
    local tbl="$1"
    local out="$RAW/$tbl.tbl"
    if [[ -s "$out" && "${FORCE_REGEN:-0}" != "1" ]]; then
        echo "[load]   dbgen: reuse existing $out ($(wc -c <"$out") bytes)"
        return
    fi
    rm -f "$out"
    echo "[load]   dbgen -s $TPC_SCALE -T ${LETTER[$tbl]} -> $out"
    # dbgen writes .tbl into its cwd and reads dists.dss from cwd by default.
    # We cd into $RAW (writable) and feed dbgen the dists file via -b to keep
    # the root-owned tpch-dbgen source tree pristine.
    (
        cd "$RAW"
        "$DBGEN_BIN" -q -f -s "$TPC_SCALE" -T "${LETTER[$tbl]}" -b "$DBGEN_SRC/dists.dss"
    )
    [[ -s "$out" ]] || { echo "[load]   FATAL: $out empty/missing"; exit 1; }
    strip_trailing_pipe "$out"
}

copy_stage() {
    local tbl="$1" raw="$RAW/$tbl.tbl"
    local t0=$(date +%s)
    $PSQL -c "\\COPY stage.$tbl FROM '$raw' (FORMAT csv, DELIMITER '|', QUOTE E'\\b')" >/dev/null
    local dt=$(( $(date +%s) - t0 ))
    echo "[load]   COPY stage.$tbl  ${dt}s"
}

insert_iceberg() {
    local tbl="$1"
    local t0=$(date +%s)
    $PSQL -c "INSERT INTO $tbl SELECT * FROM stage.$tbl" >/dev/null
    local dt=$(( $(date +%s) - t0 ))
    echo "[load]   INSERT iceberg.$tbl  ${dt}s"
    $PSQL -c "ANALYZE $tbl" >/dev/null
    if [[ "${KEEP_STAGE:-0}" != "1" ]]; then
        $PSQL -c "TRUNCATE stage.$tbl" >/dev/null
    fi
}

# Phase 1: data generation (idempotent)
echo "[load] generating raw .tbl files at SF=$TPC_SCALE under $RAW"
for tbl in "${TABLES[@]}"; do
    gen_one_table "$tbl"
done

# Phase 2: COPY into stage, INSERT into iceberg AM
echo "[load] COPY stage.* + INSERT iceberg.* (expect the INSERT loop to be"
echo "       the slow step — it exercises pg_iceberg_metadata_tracker)"
for tbl in "${TABLES[@]}"; do
    copy_stage "$tbl"
    insert_iceberg "$tbl"
done

# Phase 3: sentinel
touch "$RAW/.loaded-$DB"
echo "[load] done; sentinel at $RAW/.loaded-$DB"

# Row-count dump for the summary.
$PSQL -P pager=off -c "
SELECT 'nation'   AS tbl, count(*) FROM nation UNION ALL
SELECT 'region',   count(*) FROM region UNION ALL
SELECT 'part',     count(*) FROM part UNION ALL
SELECT 'supplier', count(*) FROM supplier UNION ALL
SELECT 'partsupp', count(*) FROM partsupp UNION ALL
SELECT 'customer', count(*) FROM customer UNION ALL
SELECT 'orders',   count(*) FROM orders UNION ALL
SELECT 'lineitem', count(*) FROM lineitem
ORDER BY 1;"
