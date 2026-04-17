#!/bin/bash
# analyze_soak.sh
#
# Post-hoc analysis of a resource_monitor.sh CSV. Flags:
#   - RSS growth across the run exceeds threshold (default 20%)
#   - FD count grows monotonically (possible leak)
#
# Usage:
#   analyze_soak.sh <input.csv> [rss_growth_pct_threshold]
#
# Default threshold: 20 (percent).

set -e

INPUT_CSV="${1:?usage: analyze_soak.sh <input.csv> [threshold_pct]}"
RSS_GROW_PCT="${2:-20}"

if [ ! -s "${INPUT_CSV}" ]; then
    echo "analyze_soak: ${INPUT_CSV} is empty or missing" >&2
    exit 2
fi

PYTHON_BIN="$(command -v python3 || command -v python2 || command -v python || true)"
[ -z "${PYTHON_BIN}" ] && { echo "analyze_soak: no python found" >&2; exit 2; }

"${PYTHON_BIN}" - "${INPUT_CSV}" "${RSS_GROW_PCT}" <<'PY_EOF'
# -*- coding: utf-8 -*-
from __future__ import print_function
import csv, sys, collections

csv_path, threshold_str = sys.argv[1:3]
threshold = float(threshold_str)

samples_by_pid = collections.defaultdict(list)
with open(csv_path) as f:
    reader = csv.DictReader(f)
    for row in reader:
        try:
            samples_by_pid[row["pid"]].append({
                "ts": row["timestamp_iso"],
                "rss": float(row["rss_mb"]),
                "fd":  int(row["open_fds"]),
            })
        except (KeyError, ValueError):
            continue

warnings = []
ok_count = 0
for pid, s in samples_by_pid.items():
    if len(s) < 4:
        continue
    rss_first_q = sum(x["rss"] for x in s[:len(s)//4]) / max(len(s)//4, 1)
    rss_last_q  = sum(x["rss"] for x in s[-len(s)//4:]) / max(len(s)//4, 1)
    if rss_first_q > 0:
        rss_pct = (rss_last_q - rss_first_q) / rss_first_q * 100
        if rss_pct > threshold:
            warnings.append("pid=%s RSS grew %.1f%% (%.1fMB -> %.1fMB) over %d samples" %
                            (pid, rss_pct, rss_first_q, rss_last_q, len(s)))
        else:
            ok_count += 1

    fds = [x["fd"] for x in s]
    if len(fds) >= 10:
        first_q_fd_avg = sum(fds[:len(fds)//4]) / max(len(fds)//4, 1)
        last_q_fd_avg  = sum(fds[-len(fds)//4:]) / max(len(fds)//4, 1)
        fd_delta = last_q_fd_avg - first_q_fd_avg
        if fd_delta > 10:
            warnings.append("pid=%s FD count grew by %d (%.0f -> %.0f)" %
                            (pid, fd_delta, first_q_fd_avg, last_q_fd_avg))

if warnings:
    print("analyze_soak: WARNINGS")
    for w in warnings:
        print("  " + w)
    print("pids_with_no_drift:", ok_count)
    sys.exit(1)
else:
    print("analyze_soak: OK - no leak signals over %d tracked pids" % len(samples_by_pid))
    sys.exit(0)
PY_EOF
