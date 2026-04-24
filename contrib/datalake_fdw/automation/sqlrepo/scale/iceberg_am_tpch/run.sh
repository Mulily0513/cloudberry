#!/usr/bin/env bash
# sqlrepo/scale/iceberg_am_tpch/run.sh
#
# End-to-end TPC-H scale test for iceberg AM. Entry point is either:
#   - `make installcheck` (via Makefile shim), run_scale_tests.sh iterates
#     scale categories and calls this through the Makefile.
#   - ../../../scripts/test/run_tpc.sh tpch <sf> <mode>   (developer UX).
#
# Env:
#   TPC_SCALE     SF to target: 1 | 10 | 100 | 1000     (default: 1)
#   MODE          both | ddl | load | query | clean     (default: both)
#   DB            database name override                (default: iceberg_am_tpch_sf$SF)
#   CONTAINER     CBDB container name                   (default: hashdata-lightning-umbrella-hashdata-1)
#   PGPORT        CBDB coordinator port                 (default: 7000)
#   QUERY_TIMEOUT seconds per query                     (default: 1200 for SF<=10, 7200 otherwise)
#   REPORT_DIR    where summary + per-query logs land   (default: ../reports/scale/<ts>/iceberg_am_tpch/sf<sf>)
#
# Exit status: 0 if all queries PASS, 1 on any FAIL/CRASH, 77 (skip) if the
# SF's raw data cannot fit on disk.

set -u -o pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AUTOMATION_DIR="$(cd "$HERE/../../.." && pwd)"

TPC_SCALE="${TPC_SCALE:-1}"
MODE="${MODE:-both}"
DB="${DB:-iceberg_am_tpch_sf${TPC_SCALE}}"
CONTAINER="${CONTAINER:-hashdata-lightning-umbrella-hashdata-1}"
PGPORT="${PGPORT:-7000}"
if (( TPC_SCALE <= 10 )); then
    QUERY_TIMEOUT="${QUERY_TIMEOUT:-1200}"
else
    QUERY_TIMEOUT="${QUERY_TIMEOUT:-7200}"
fi

TS="$(date -u +%Y%m%d_%H%M%S)"
REPORT_DIR="${REPORT_DIR:-$AUTOMATION_DIR/reports/scale/$TS/iceberg_am_tpch/sf${TPC_SCALE}}"
mkdir -p "$REPORT_DIR"

# Raw data lives outside the build tree so git stays clean. /workspace is
# the umbrella-root bind-mount into the CBDB container, so the container's
# /workspace/.tpc-data/... path maps to host $AUTOMATION_DIR/../../../../.tpc-data.
# We create the directory from *inside* the container so it lands as
# gpadmin:gpadmin and dbgen can write to it; a root-owned host mkdir would
# render the dir read-only for gpadmin and trip dbgen's Open failure at
# bm_utils.c:417.
RAW_IN_CONTAINER="/workspace/.tpc-data/tpch/sf${TPC_SCALE}"
RAW_HOST="$AUTOMATION_DIR/../../../../.tpc-data/tpch/sf${TPC_SCALE}"
docker exec -u gpadmin "$CONTAINER" mkdir -p "$RAW_IN_CONTAINER" 2>/dev/null || true

DBGEN_BIN="$AUTOMATION_DIR/tools/tpc/bin/dbgen"
DBGEN_SRC="$AUTOMATION_DIR/tools/tpc/tpch-dbgen"

log()  { printf '\033[1;34m[run]\033[0m  %s\n' "$*" >&2; }
warn() { printf '\033[1;33m[run]\033[0m  %s\n' "$*" >&2; }
err()  { printf '\033[1;31m[run]\033[0m  %s\n' "$*" >&2; }

# ----- pre-flight -----
if [[ "$MODE" != "clean" ]]; then
    if [[ ! -x "$DBGEN_BIN" ]]; then
        err "dbgen not built; run INSTALL_TPC_TOOLS=1 scripts/setup/install_tools.sh"
        exit 1
    fi
    # Disk guard. dbgen SF=N produces ~N GB raw; iceberg AM roughly doubles.
    # Check the parent (which always exists) rather than $RAW_HOST (which we
    # intentionally create on the container side for gpadmin ownership).
    local_need_gb=$(( TPC_SCALE * 3 ))
    avail_gb=$(df --output=avail -BG "$AUTOMATION_DIR" | tail -1 | tr -dc 0-9)
    if (( avail_gb < local_need_gb )); then
        err "insufficient disk for SF=$TPC_SCALE: need ~${local_need_gb}GB, have ${avail_gb}GB at $AUTOMATION_DIR"
        exit 77
    fi
fi

# Helper: psql inside the CBDB container. Each call is an independent
# connection; cheap here (~ms) and keeps crash/reconnect simple.
psql_in() {
    local dbname="$1"; shift
    docker exec -u gpadmin -e PGPORT="$PGPORT" -e PGDATABASE="$dbname" \
        "$CONTAINER" bash -lc "
            source /workspace/dist/database/cloudberry-env.sh >/dev/null
            psql $*"
}

# Helper: run a SQL file through psql_in, streaming stdout/stderr to a log.
psql_file_in() {
    local dbname="$1" path="$2" log="$3"
    docker exec -u gpadmin -e PGPORT="$PGPORT" -e PGDATABASE="$dbname" \
        -e PATH_IN="$path" \
        "$CONTAINER" bash -lc "
            source /workspace/dist/database/cloudberry-env.sh >/dev/null
            timeout $QUERY_TIMEOUT psql -v ON_ERROR_STOP=0 -v dbname=$dbname -f \$PATH_IN
        " >"$log" 2>&1
}

# ----- phases -----
phase_clean() {
    log "drop database $DB and raw data under $RAW_HOST"
    psql_in postgres "-c 'DROP DATABASE IF EXISTS $DB'" >/dev/null 2>&1 || true
    rm -rf "$RAW_HOST"
}

phase_ddl() {
    log "create database $DB"
    psql_in postgres "-c 'DROP DATABASE IF EXISTS $DB'" >/dev/null 2>&1
    psql_in postgres "-c 'CREATE DATABASE $DB'" >/dev/null
    log "apply ddl/01_catalog_volume.sql"
    psql_in "$DB" "-v dbname=$DB -f /workspace/database/contrib/datalake_fdw/automation/sqlrepo/scale/iceberg_am_tpch/ddl/01_catalog_volume.sql" \
        > "$REPORT_DIR/01_catalog_volume.log" 2>&1 || { err "01_catalog_volume.sql failed"; return 1; }
    log "apply ddl/02_stage_heap.sql"
    psql_in "$DB" "-f /workspace/database/contrib/datalake_fdw/automation/sqlrepo/scale/iceberg_am_tpch/ddl/02_stage_heap.sql" \
        > "$REPORT_DIR/02_stage_heap.log" 2>&1 || { err "02_stage_heap.sql failed"; return 1; }
    log "apply ddl/03_iceberg_am.sql"
    psql_in "$DB" "-f /workspace/database/contrib/datalake_fdw/automation/sqlrepo/scale/iceberg_am_tpch/ddl/03_iceberg_am.sql" \
        > "$REPORT_DIR/03_iceberg_am.log" 2>&1 || { err "03_iceberg_am.sql failed"; return 1; }
}

phase_load() {
    if [[ -f "$RAW_HOST/.loaded-$DB" && "${FORCE_LOAD:-0}" != "1" ]]; then
        log "load sentinel $RAW_HOST/.loaded-$DB exists, skipping (FORCE_LOAD=1 to override)"
        return 0
    fi
    log "invoke load.sh inside container: TPC_SCALE=$TPC_SCALE DB=$DB RAW=$RAW_IN_CONTAINER"
    docker exec -u gpadmin \
        -e PGPORT="$PGPORT" -e PGDATABASE="$DB" \
        -e TPC_SCALE="$TPC_SCALE" \
        -e RAW="$RAW_IN_CONTAINER" \
        -e DB="$DB" \
        -e DBGEN_BIN="/workspace/database/contrib/datalake_fdw/automation/tools/tpc/bin/dbgen" \
        -e DBGEN_SRC="/workspace/database/contrib/datalake_fdw/automation/tools/tpc/tpch-dbgen" \
        -e KEEP_STAGE="${KEEP_STAGE:-0}" \
        -e FORCE_REGEN="${FORCE_REGEN:-0}" \
        "$CONTAINER" bash -lc "
            source /workspace/dist/database/cloudberry-env.sh >/dev/null
            bash /workspace/database/contrib/datalake_fdw/automation/sqlrepo/scale/iceberg_am_tpch/load/load.sh
        " 2>&1 | tee "$REPORT_DIR/load.log"
    local rc=${PIPESTATUS[0]}
    return $rc
}

# ---- query phase ----
classify() {
    local rc="$1" log="$2"
    if [[ $rc -eq 124 ]]; then echo "TIMEOUT"; return; fi
    if grep -q 'server closed the connection' "$log"; then echo "CRASH"; return; fi
    if grep -qE '^ERROR:|^FATAL:|^PANIC:' "$log"; then echo "FAIL"; return; fi
    if [[ $rc -ne 0 ]]; then echo "FAIL"; return; fi
    echo "PASS"
}

phase_query() {
    local qdir="$HERE/query/sf${TPC_SCALE}"
    if [[ ! -d "$qdir" ]]; then
        # Fall back to the nearest lower tier that does have rendered queries.
        for tier in 1000 100 10 1; do
            [[ -d "$HERE/query/sf${tier}" ]] && { qdir="$HERE/query/sf${tier}"; break; }
        done
        warn "no query/sf${TPC_SCALE}/ directory; using $qdir"
    fi

    local summary="$REPORT_DIR/summary.tsv"
    : > "$summary"
    printf 'query\tstatus\tlatency_ms\tnote\n' > "$summary"

    # qdir resolves per-tier (sf1/sf10/sf100/sf1000_subset). Compute the
    # in-container path from the tier we actually picked.
    local qtier; qtier="$(basename "$qdir")"
    log "running queries from $qdir against $DB"
    local pass=0 fail=0 crash=0 timeout=0
    local consec_crash=0
    for qf in "$qdir"/q*.sql; do
        [[ -f "$qf" ]] || continue
        local qname; qname="$(basename "$qf" .sql)"
        local qpath_in="/workspace/database/contrib/datalake_fdw/automation/sqlrepo/scale/iceberg_am_tpch/query/${qtier}/${qname}.sql"
        local log="$REPORT_DIR/${qname}.log"
        local t0_ns; t0_ns="$(date +%s%N)"
        psql_file_in "$DB" "$qpath_in" "$log"
        local rc=$?
        local t1_ns; t1_ns="$(date +%s%N)"
        local ms=$(( (t1_ns - t0_ns) / 1000000 ))
        local status; status="$(classify "$rc" "$log")"
        local note; note="$(grep -m1 -E '^(ERROR|FATAL|PANIC):' "$log" 2>/dev/null | head -c 180)"
        printf '%s\t%s\t%d\t%s\n' "$qname" "$status" "$ms" "$note" >> "$summary"
        case "$status" in
            PASS)    printf '  [PASS]  %-6s %6dms\n' "$qname" "$ms"; pass=$((pass+1)); consec_crash=0 ;;
            FAIL)    printf '  [FAIL]  %-6s %6dms  %s\n' "$qname" "$ms" "${note:-(see log)}"; fail=$((fail+1)); consec_crash=0 ;;
            TIMEOUT) printf '  [TOUT]  %-6s %6dms\n' "$qname" "$ms"; timeout=$((timeout+1)); consec_crash=0 ;;
            CRASH)   printf '  [CRSH]  %-6s %6dms  segment crashed\n' "$qname" "$ms"; crash=$((crash+1)); consec_crash=$((consec_crash+1));;
        esac
        # If coordinator is dying repeatedly, give up — further queries will
        # all fail with "server closed the connection".
        if (( consec_crash >= 3 )); then
            err "3 consecutive CRASH results, aborting to avoid spam"
            break
        fi
    done
    log "summary: PASS=$pass FAIL=$fail CRASH=$crash TIMEOUT=$timeout  ($summary)"
    if (( fail + crash + timeout > 0 )); then
        return 1
    fi
    return 0
}

# ----- dispatch -----
case "$MODE" in
    clean)              phase_clean ;;
    ddl)                phase_ddl ;;
    load)               phase_ddl && phase_load ;;
    query)              phase_query ;;
    both|*)             phase_ddl && phase_load && phase_query ;;
esac
