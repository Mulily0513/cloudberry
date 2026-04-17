#!/bin/bash
# compare_baseline.sh
#
# Compare current performance run results against config/baseline.json.
#
# Workflow:
#   1. Query perf_results_summary for the latest run of each (test_name, operation).
#   2. Read config/baseline.json.
#   3. If baselines block is empty -> first-run mode: write
#      ${REPORT_DIR}/baseline_suggested.json with current p50_ms values.
#      User can manually move suggested values into config/baseline.json.
#   4. Otherwise: for each baseline entry, look up current p50_ms; if it exceeds
#      baseline.p50_ms * (1 + tolerance_pct/100) write the violation to
#      ${REPORT_DIR}/regressions.txt. Always also writes summary.txt with all
#      observed timings.
#
# Exit code:
#   0 - no regressions (or first-run mode)
#   2 - regressions detected (does not fail the test run by itself; runner
#       decides whether to propagate)
#
# Usage:
#   bash scripts/test/compare_baseline.sh <report_dir>
#
# Requires: psql, python2 (or python3) for JSON parsing.

set -e

# Capture caller-provided dir BEFORE sourcing test_config.env, since
# test_config.env unconditionally exports its own REPORT_DIR.
PERF_REPORT_DIR_ARG="${1:?usage: compare_baseline.sh <report_dir>}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AUTOMATION_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${AUTOMATION_DIR}/config/test_config.env"
source "${AUTOMATION_DIR}/scripts/utils/common_functions.sh"

# Restore caller-provided value
REPORT_DIR="${PERF_REPORT_DIR_ARG}"
mkdir -p "${REPORT_DIR}"

BASELINE_FILE="${AUTOMATION_DIR}/config/baseline.json"
SUMMARY_FILE="${REPORT_DIR}/perf_summary.txt"
REGRESSIONS_FILE="${REPORT_DIR}/regressions.txt"
SUGGESTED_FILE="${REPORT_DIR}/baseline_suggested.json"
CURRENT_CSV="${REPORT_DIR}/perf_current.csv"

# Pick a python interpreter
PYTHON_BIN="$(command -v python3 || command -v python2 || command -v python || true)"
if [ -z "${PYTHON_BIN}" ]; then
    log_error "No python interpreter found - cannot parse baseline JSON"
    exit 1
fi
log_debug "Using python: ${PYTHON_BIN}"

# Step 1: locate cumulative CSV produced by run_performance_tests.sh
# (that script appends to perf_dump.csv after each category, since pg_regress
# drops the regression DB at the start of each invocation).
PERF_DUMP_CSV="${REPORT_DIR}/perf_dump.csv"
if [ -f "${PERF_DUMP_CSV}" ] && [ -s "${PERF_DUMP_CSV}" ]; then
    cp "${PERF_DUMP_CSV}" "${CURRENT_CSV}"
    log_info "Using cumulative perf dump: ${PERF_DUMP_CSV}"
else
    # Fallback: query whatever DB is currently available (useful for
    # standalone invocations during development)
    log_info "No cumulative dump found; querying live DB -> ${CURRENT_CSV}"
    psql -At -F',' -c "
        SELECT DISTINCT ON (test_name, operation)
            test_name, operation,
            ROUND(p50_ms, 2),
            ROUND(p95_ms, 2),
            ROUND(min_ms, 2),
            ROUND(max_ms, 2),
            iterations,
            COALESCE(rows_count, 0),
            ROUND(COALESCE(throughput_rows_per_sec, 0), 2)
        FROM public.perf_results_summary
        ORDER BY test_name, operation, created_at DESC;
    " > "${CURRENT_CSV}" 2>/dev/null || {
        log_warn "perf_results_summary unavailable; skipping baseline compare"
        exit 0
    }
fi

if [ ! -s "${CURRENT_CSV}" ]; then
    log_warn "No perf data found (cumulative dump empty); skipping baseline compare"
    exit 0
fi

# Step 2: write human summary
{
    echo "================================================================================"
    echo "Performance Run Summary - ${REPORT_TIMESTAMP}"
    echo "================================================================================"
    printf "%-35s %-30s %12s %12s %12s %8s %12s\n" \
        "test_name" "operation" "p50_ms" "p95_ms" "min_ms" "iters" "throughput"
    echo "--------------------------------------------------------------------------------"
    while IFS=',' read -r test op p50 p95 min_v max_v iters rows tput; do
        printf "%-35s %-30s %12s %12s %12s %8s %12s\n" \
            "${test}" "${op}" "${p50}" "${p95}" "${min_v}" "${iters}" "${tput}"
    done < "${CURRENT_CSV}"
    echo "================================================================================"
} > "${SUMMARY_FILE}"

log_info "Summary written: ${SUMMARY_FILE}"

# Step 3: invoke python for baseline compare / suggestion
"${PYTHON_BIN}" - "${BASELINE_FILE}" "${CURRENT_CSV}" "${SUGGESTED_FILE}" "${REGRESSIONS_FILE}" <<'PY_EOF'
# -*- coding: utf-8 -*-
"""compare current perf results against baseline; write regressions + suggestion"""
from __future__ import print_function
import json, csv, sys, os

baseline_path, current_csv, suggested_path, regressions_path = sys.argv[1:5]

with open(baseline_path) as f:
    baseline_doc = json.load(f)

baselines = baseline_doc.get("baselines", {})
# strip _comment placeholder so emptiness check works
real_baselines = {k: v for k, v in baselines.items() if not k.startswith("_")}
default_tol = baseline_doc.get("tolerance_default_pct", 20)

current = {}
with open(current_csv) as f:
    reader = csv.reader(f)
    for row in reader:
        if len(row) < 9:
            continue
        test, op, p50, p95, mn, mx, iters, rows_c, tput = row
        try:
            p50f = float(p50)
        except ValueError:
            continue
        current["%s.%s" % (test, op)] = {
            "p50_ms": p50f,
            "p95_ms": float(p95) if p95 else 0.0,
            "min_ms": float(mn) if mn else 0.0,
            "max_ms": float(mx) if mx else 0.0,
            "iterations": int(iters) if iters else 0,
            "rows_count": int(rows_c) if rows_c else 0,
            "throughput_rows_per_sec": float(tput) if tput else 0.0,
        }

# First-run mode: empty baselines -> write suggested file
if not real_baselines:
    suggested = {
        "version": baseline_doc.get("version", "2.0"),
        "description": "Auto-generated suggestion from first run. Review and copy values you accept into config/baseline.json under 'baselines'.",
        "tolerance_default_pct": default_tol,
        "baselines": {
            key: {"p50_ms": round(metrics["p50_ms"], 2),
                  "tolerance_pct": default_tol}
            for key, metrics in current.items()
        }
    }
    with open(suggested_path, "w") as f:
        json.dump(suggested, f, indent=2, sort_keys=True)
    print("[BASELINE] First-run mode: suggested baselines written to %s (%d entries)" %
          (suggested_path, len(current)))
    sys.exit(0)

# Compare mode: walk known baselines, flag overruns
regressions = []
missing_in_current = []
unknown_in_baseline = []

for key, base in real_baselines.items():
    if key not in current:
        missing_in_current.append(key)
        continue
    base_p50 = base.get("p50_ms")
    tol = base.get("tolerance_pct", default_tol)
    if base_p50 is None or base_p50 <= 0:
        continue
    cur_p50 = current[key]["p50_ms"]
    threshold = base_p50 * (1.0 + tol / 100.0)
    if cur_p50 > threshold:
        pct_change = (cur_p50 - base_p50) / base_p50 * 100.0
        regressions.append({
            "key": key,
            "baseline_p50_ms": base_p50,
            "current_p50_ms": cur_p50,
            "tolerance_pct": tol,
            "pct_change": pct_change,
        })

for key in current:
    if key not in real_baselines:
        unknown_in_baseline.append(key)

with open(regressions_path, "w") as f:
    f.write("Performance regression report - %s\n" % os.path.basename(regressions_path))
    f.write("=" * 80 + "\n")
    if regressions:
        f.write("\nREGRESSIONS (%d):\n" % len(regressions))
        for r in regressions:
            f.write("  %s: baseline %.2fms, current %.2fms (+%.1f%%, tol %d%%)\n" % (
                r["key"], r["baseline_p50_ms"], r["current_p50_ms"],
                r["pct_change"], r["tolerance_pct"]))
    else:
        f.write("\nNo regressions detected.\n")
    if missing_in_current:
        f.write("\nBaseline entries missing in current run (%d):\n" % len(missing_in_current))
        for k in missing_in_current:
            f.write("  %s\n" % k)
    if unknown_in_baseline:
        f.write("\nNew tests not in baseline (%d) - consider adding to config/baseline.json:\n" %
                len(unknown_in_baseline))
        for k in unknown_in_baseline:
            f.write("  %s (current p50: %.2fms)\n" % (k, current[k]["p50_ms"]))

print("[BASELINE] Compared %d entries. Regressions: %d. Report: %s" % (
    len(real_baselines), len(regressions), regressions_path))
sys.exit(2 if regressions else 0)
PY_EOF

PY_EXIT=$?

if [ ${PY_EXIT} -eq 2 ]; then
    log_warn "Performance regressions detected - see ${REGRESSIONS_FILE}"
    exit 2
elif [ ${PY_EXIT} -ne 0 ]; then
    log_error "Baseline comparison script failed (exit ${PY_EXIT})"
    exit ${PY_EXIT}
fi

log_success "Baseline comparison complete - no regressions"
exit 0
