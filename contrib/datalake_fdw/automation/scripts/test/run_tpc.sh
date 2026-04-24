#!/usr/bin/env bash
# run_tpc.sh — one-stop TPC-H / TPC-DS scale-test launcher for Iceberg AM.
#
# Wrapper around the per-category run.sh files under sqlrepo/scale/. Keeps
# developers from having to remember the full category path or the per-tier
# conventions.
#
# Usage:
#   run_tpc.sh <tpch|tpcds> [scale] [mode]
#
# scale  :  1 | 10 | 100 | 1000   (default: 1)
# mode   :  both | ddl | load | query | clean   (default: both)
#
# Examples:
#   run_tpc.sh tpch                 # TPC-H SF=1, full ddl+load+query
#   run_tpc.sh tpcds 10 query       # re-run TPC-DS queries at SF=10 against existing DB
#   run_tpc.sh tpch 100 clean       # drop SF=100 DB + raw data
#
# Env passthrough: CONTAINER, PGPORT, QUERY_TIMEOUT, KEEP_STAGE, FORCE_REGEN.

set -eu

BENCH="${1:-}"
SCALE="${2:-1}"
MODE="${3:-both}"

if [[ -z "$BENCH" || ! "$BENCH" =~ ^(tpch|tpcds)$ ]]; then
    cat >&2 <<USAGE
usage: run_tpc.sh <tpch|tpcds> [scale] [mode]
  scale: 1 | 10 | 100 | 1000     (default: 1)
  mode:  both | ddl | load | query | clean   (default: both)
USAGE
    exit 2
fi

AUTOMATION_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
RUN_SH="$AUTOMATION_DIR/sqlrepo/scale/iceberg_am_${BENCH}/run.sh"

if [[ ! -x "$RUN_SH" ]]; then
    echo "not found / not executable: $RUN_SH" >&2
    exit 1
fi

echo "[run_tpc] bench=$BENCH  SF=$SCALE  mode=$MODE" >&2
exec env TPC_SCALE="$SCALE" MODE="$MODE" bash "$RUN_SH"
