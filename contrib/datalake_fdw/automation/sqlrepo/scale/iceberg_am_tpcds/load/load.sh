#!/usr/bin/env bash
# load/load.sh — generate dsdgen data at SF=$TPC_SCALE and load into the
# iceberg AM tables via the staging heap.
#
# Contract (see caller ../run.sh):
#   PGPORT / PGDATABASE=$DB set, cwd here, $RAW writable, DSDGEN_BIN set,
#   DSDGEN_SRC points at tpcds-kit/tools (home of tpcds.idx).
#
# dsdgen writes one .dat per table when given -TABLE all. Lines are
# `|`-delimited and terminate with a trailing `|` that we strip before COPY.
#
# Env:
#   KEEP_STAGE=1    keep stage.* after INSERT (debugging)
#   FORCE_REGEN=1   regenerate .dat even if present

set -euo pipefail

: "${TPC_SCALE:?}"
: "${RAW:?}"
: "${DB:?}"
: "${DSDGEN_BIN:?}"
: "${DSDGEN_SRC:?}"

PSQL="psql -v ON_ERROR_STOP=1 -q"

TABLES=(
    call_center catalog_page catalog_returns catalog_sales customer
    customer_address customer_demographics date_dim household_demographics
    income_band inventory item promotion reason ship_mode store
    store_returns store_sales time_dim warehouse web_page web_returns
    web_sales web_site
)

mkdir -p "$RAW"

# Phase 1: data generation (idempotent). dsdgen -TABLE all writes 24 .dat.
if [[ -s "$RAW/date_dim.dat" && "${FORCE_REGEN:-0}" != "1" ]]; then
    echo "[load] dsdgen output already present under $RAW — reusing"
else
    echo "[load] dsdgen -SCALE $TPC_SCALE -DIR $RAW  (writes all 24 tables)"
    (
        cd "$RAW"
        "$DSDGEN_BIN" \
            -SCALE "$TPC_SCALE" \
            -DIR "$RAW" \
            -FORCE Y \
            -TERMINATE Y \
            -DISTRIBUTIONS "$DSDGEN_SRC/tpcds.idx"
    )
    echo "[load] dsdgen done. Stripping trailing '|' for CSV compatibility."
    for tbl in "${TABLES[@]}"; do
        [[ -s "$RAW/$tbl.dat" ]] && sed -i 's/|$//' "$RAW/$tbl.dat"
    done
fi

# Phase 2: COPY -> stage, INSERT -> iceberg.
echo "[load] COPY stage.* + INSERT iceberg.*"
for tbl in "${TABLES[@]}"; do
    raw="$RAW/$tbl.dat"
    if [[ ! -s "$raw" ]]; then
        echo "[load]   skip $tbl (no data file)"
        continue
    fi
    t0=$(date +%s)
    $PSQL -c "\\COPY stage.$tbl FROM '$raw' (FORMAT csv, DELIMITER '|', QUOTE E'\\b')" >/dev/null
    dt=$(( $(date +%s) - t0 ))
    echo "[load]   COPY stage.$tbl  ${dt}s"

    t0=$(date +%s)
    $PSQL -c "INSERT INTO $tbl SELECT * FROM stage.$tbl" >/dev/null
    dt=$(( $(date +%s) - t0 ))
    echo "[load]   INSERT iceberg.$tbl  ${dt}s"

    $PSQL -c "ANALYZE $tbl" >/dev/null 2>&1 || true
    if [[ "${KEEP_STAGE:-0}" != "1" ]]; then
        $PSQL -c "TRUNCATE stage.$tbl" >/dev/null
    fi
done

touch "$RAW/.loaded-$DB"
echo "[load] done; sentinel at $RAW/.loaded-$DB"

# Row-count dump for summary.
$PSQL -P pager=off -c "
SELECT tbl, cnt FROM (VALUES
    ('call_center',           (SELECT count(*) FROM call_center)),
    ('catalog_page',          (SELECT count(*) FROM catalog_page)),
    ('catalog_returns',       (SELECT count(*) FROM catalog_returns)),
    ('catalog_sales',         (SELECT count(*) FROM catalog_sales)),
    ('customer',              (SELECT count(*) FROM customer)),
    ('customer_address',      (SELECT count(*) FROM customer_address)),
    ('customer_demographics', (SELECT count(*) FROM customer_demographics)),
    ('date_dim',              (SELECT count(*) FROM date_dim)),
    ('household_demographics',(SELECT count(*) FROM household_demographics)),
    ('income_band',           (SELECT count(*) FROM income_band)),
    ('inventory',             (SELECT count(*) FROM inventory)),
    ('item',                  (SELECT count(*) FROM item)),
    ('promotion',             (SELECT count(*) FROM promotion)),
    ('reason',                (SELECT count(*) FROM reason)),
    ('ship_mode',             (SELECT count(*) FROM ship_mode)),
    ('store',                 (SELECT count(*) FROM store)),
    ('store_returns',         (SELECT count(*) FROM store_returns)),
    ('store_sales',           (SELECT count(*) FROM store_sales)),
    ('time_dim',              (SELECT count(*) FROM time_dim)),
    ('warehouse',             (SELECT count(*) FROM warehouse)),
    ('web_page',              (SELECT count(*) FROM web_page)),
    ('web_returns',           (SELECT count(*) FROM web_returns)),
    ('web_sales',             (SELECT count(*) FROM web_sales)),
    ('web_site',              (SELECT count(*) FROM web_site))
) t(tbl,cnt) ORDER BY tbl;"
