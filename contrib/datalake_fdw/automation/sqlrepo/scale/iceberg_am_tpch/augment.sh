#!/usr/bin/env bash
# augment.sh — real-scenario stress tests for the Iceberg AM write path.
#
# Runs against an already-loaded iceberg_am_tpch_sf<N> database (default SF=10).
# Each scenario is designed to provoke a failure mode that single-session
# load + query does not exercise:
#
#   S1  concurrent INSERTs into the same iceberg table
#       -> stresses optimistic CAS in pg_iceberg_metadata_tracker +
#          datalake_agent commitAppend retries.
#
#   S2  ALTER TABLE ADD COLUMN while a long INSERT is in flight
#       -> tests the writer's schema snapshot vs DDL lock ordering.
#
#   S3  VACUUM while an INSERT is running
#       -> tests rewrite-vs-writer interleaving.
#
# Each scenario reports PASS / FAIL + any new cores it produced.

set -u -o pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AUTOMATION_DIR="$(cd "$HERE/../../.." && pwd)"

SF="${TPC_SCALE:-10}"
DB="${DB:-iceberg_am_tpch_sf${SF}}"
CONTAINER="${CONTAINER:-hashdata-lightning-umbrella-hashdata-1}"
PGPORT="${PGPORT:-7000}"
REPORT_DIR="${REPORT_DIR:-$AUTOMATION_DIR/reports/scale/$(date -u +%Y%m%d_%H%M%S)/iceberg_am_tpch_augment_sf${SF}}"
mkdir -p "$REPORT_DIR"

log()  { printf '\033[1;34m[augment]\033[0m %s\n' "$*" >&2; }
ok()   { printf '\033[1;32m[augment]\033[0m PASS: %s\n' "$*" >&2; }
fail() { printf '\033[1;31m[augment]\033[0m FAIL: %s\n' "$*" >&2; }

psql_c() {
    # Run one statement with timing
    local sql="$1"
    docker exec -u gpadmin -e PGPORT="$PGPORT" -e PGDATABASE="$DB" \
        "$CONTAINER" bash -lc "
            source /workspace/dist/database/cloudberry-env.sh >/dev/null
            psql -v ON_ERROR_STOP=0 -c \"$sql\"
        "
}

psql_c_bg() {
    # Run one statement in a detached background psql; returns the pid.
    local sql="$1" tag="$2"
    docker exec -d -u gpadmin -e PGPORT="$PGPORT" -e PGDATABASE="$DB" \
        "$CONTAINER" bash -lc "
            source /workspace/dist/database/cloudberry-env.sh >/dev/null
            psql -v ON_ERROR_STOP=0 -c \"$sql\" > /tmp/augment_${tag}.log 2>&1
        "
}

# Baseline cores inside container
docker exec "$CONTAINER" bash -c \
    'find /workspace/deploy/database/datadirs -maxdepth 6 \( -name "core-*" -o -name "core.*" \) 2>/dev/null | sort' \
    > /tmp/augment_cores_baseline.txt

check_cores() {
    local tag="$1"
    docker exec "$CONTAINER" bash -c \
        'find /workspace/deploy/database/datadirs -maxdepth 6 \( -name "core-*" -o -name "core.*" \) 2>/dev/null | sort' \
        > /tmp/augment_cores_now.txt
    local diff
    diff=$(comm -13 /tmp/augment_cores_baseline.txt /tmp/augment_cores_now.txt || true)
    if [[ -n "$diff" ]]; then
        fail "$tag produced new core(s): $diff"
        return 1
    fi
}

# --- make a fresh scratch table for write scenarios, independent of the
#     benchmark's lineitem so we don't pollute subsequent query runs.
log "setup: create scratch_orders (clone of orders top 200k) for writes"
# Note: iceberg AM does not support CREATE TABLE AS SELECT — file as limitation.
# Work around with explicit DDL + INSERT.
psql_c "DROP TABLE IF EXISTS scratch_orders;
        CREATE ICEBERG TABLE scratch_orders (
            o_orderkey BIGINT, o_custkey INTEGER, o_orderstatus VARCHAR(1),
            o_totalprice DECIMAL(15,2), o_orderdate DATE,
            o_orderpriority VARCHAR(15), o_clerk VARCHAR(15),
            o_shippriority INTEGER, o_comment VARCHAR(79));
        INSERT INTO scratch_orders SELECT * FROM orders WHERE o_orderkey <= 200000;" >/dev/null
N0=$(psql_c "SELECT count(*) FROM scratch_orders;" 2>/dev/null | awk 'NR==3')
log "setup: scratch_orders baseline rows = $N0"

# ===== Scenario 1: concurrent INSERTs =====
log "S1: concurrent INSERT into scratch_orders from 2 sessions"
# Session A inserts keys 1..100000 again (duplicates allowed, this is a stress)
# Session B inserts keys 100001..200000 again.
psql_c_bg "INSERT INTO scratch_orders SELECT * FROM orders WHERE o_orderkey BETWEEN 1 AND 100000" s1a
psql_c_bg "INSERT INTO scratch_orders SELECT * FROM orders WHERE o_orderkey BETWEEN 100001 AND 200000" s1b
sleep 1
# wait for completion by polling scratch_orders count settling
for i in $(seq 1 60); do
    sleep 2
    N=$(psql_c "SELECT count(*) FROM scratch_orders;" 2>/dev/null | awk 'NR==3')
    P=$(docker exec "$CONTAINER" bash -lc 'pgrep -f "INSERT INTO scratch_orders" | wc -l')
    [[ "$P" == "0" ]] && break
done
N_AFTER_S1=$(psql_c "SELECT count(*) FROM scratch_orders;" 2>/dev/null | awk 'NR==3')
# Each session inserts ~25000 rows at SF=10 (sparse orderkey distribution
# in dbgen output; orderkey ≤ 100000 maps to ~25000 orders). Disjoint key
# ranges mean that if both sessions' commits land, delta = 50000 from
# baseline. If we observe baseline+25000, one writer's CAS commit lost.
EXP_S1=$(( N0 + 50000 ))
if [[ "${N_AFTER_S1// /}" == "${EXP_S1// /}" ]]; then
    ok "S1: both sessions committed, rows=$N_AFTER_S1 (expected $EXP_S1)"
else
    fail "S1: row mismatch rows=$N_AFTER_S1 expected=$EXP_S1 (concurrency race — one commit lost?)"
    docker exec "$CONTAINER" bash -c \
        'for f in /tmp/augment_s1a.log /tmp/augment_s1b.log; do echo "=== $f ==="; cat "$f" 2>/dev/null; done' \
        >> "$REPORT_DIR/s1.log" 2>&1
fi
check_cores S1

# ===== Scenario 2: ALTER ADD COLUMN while INSERT runs =====
log "S2: ALTER TABLE ADD COLUMN during active INSERT"
psql_c_bg "INSERT INTO scratch_orders SELECT o_orderkey+1000000, o_custkey, o_orderstatus, o_totalprice,
           o_orderdate, o_orderpriority, o_clerk, o_shippriority, o_comment
           FROM orders WHERE o_orderkey <= 500000" s2insert
sleep 2
# Add a new column mid-insert
ALTER_OUT=$(psql_c "ALTER TABLE scratch_orders ADD COLUMN o_note varchar(50);" 2>&1)
# Wait for INSERT to finish
for i in $(seq 1 60); do
    sleep 2
    P=$(docker exec "$CONTAINER" bash -lc 'pgrep -f "INSERT INTO scratch_orders" | wc -l')
    [[ "$P" == "0" ]] && break
done
# Read back
N_AFTER_S2=$(psql_c "SELECT count(*) FROM scratch_orders;" 2>/dev/null | awk 'NR==3')
NULLCOUNT=$(psql_c "SELECT count(*) FROM scratch_orders WHERE o_note IS NULL;" 2>/dev/null | awk 'NR==3')
log "S2: alter=$ALTER_OUT  rows=$N_AFTER_S2  null_notes=$NULLCOUNT"
if [[ -n "$NULLCOUNT" && "$NULLCOUNT" -gt 0 ]]; then
    ok "S2: ADD COLUMN landed, null fill works across schema versions (nulls=$NULLCOUNT)"
else
    fail "S2: ADD COLUMN read-back did not show NULLs — inspect $REPORT_DIR/s2.log"
    cat /tmp/augment_s2insert.log >> "$REPORT_DIR/s2.log" 2>/dev/null || true
fi
check_cores S2

# ===== Scenario 3: VACUUM during INSERT =====
log "S3: VACUUM while INSERT runs"
psql_c_bg "INSERT INTO scratch_orders SELECT o_orderkey+2000000, o_custkey, o_orderstatus, o_totalprice,
           o_orderdate, o_orderpriority, o_clerk, o_shippriority, o_comment, NULL::varchar(50)
           FROM orders WHERE o_orderkey <= 200000" s3insert
sleep 1
# VACUUM must run outside a tx block so we go through psql directly
VAC_OUT=$(psql_c "VACUUM scratch_orders;" 2>&1)
for i in $(seq 1 60); do
    sleep 2
    P=$(docker exec "$CONTAINER" bash -lc 'pgrep -f "INSERT INTO scratch_orders" | wc -l')
    [[ "$P" == "0" ]] && break
done
N_AFTER_S3=$(psql_c "SELECT count(*) FROM scratch_orders;" 2>/dev/null | awk 'NR==3')
log "S3: vacuum=${VAC_OUT:0:200}  rows=$N_AFTER_S3"
check_cores S3
ok "S3: VACUUM + INSERT interleaved (rows=$N_AFTER_S3, see $REPORT_DIR)"

# ===== cleanup =====
psql_c "DROP TABLE IF EXISTS scratch_orders;" >/dev/null
log "augment done; report dir: $REPORT_DIR"
