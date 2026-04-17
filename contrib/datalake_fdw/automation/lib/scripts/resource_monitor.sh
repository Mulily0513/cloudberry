#!/bin/bash
# resource_monitor.sh
#
# Sample RSS / VSZ / FD count for all postgres processes belonging to gpadmin
# and append to a CSV. Intended to run as a background process alongside a
# soak test so we can detect memory/FD leaks that don't show up in SQL-level
# timing drift.
#
# Usage:
#   resource_monitor.sh <output_csv> [sample_interval_sec] [duration_sec]
#
# Defaults:
#   sample_interval_sec: 5
#   duration_sec:        0 (runs until signalled)
#
# Signal handling: SIGTERM / SIGINT cleanly flush and exit.
#
# Output CSV columns:
#   timestamp_iso, pid, cmd_short, rss_mb, vsz_mb, open_fds

set -e

OUTPUT_CSV="${1:?usage: resource_monitor.sh <output_csv> [interval] [duration]}"
INTERVAL_SEC="${2:-5}"
DURATION_SEC="${3:-0}"

TARGET_USER="${TARGET_USER:-gpadmin}"
TARGET_PROCESS_PATTERN="${TARGET_PROCESS_PATTERN:-postgres}"

# Prepare output file with header
if [ ! -s "${OUTPUT_CSV}" ]; then
    echo "timestamp_iso,pid,cmd_short,rss_mb,vsz_mb,open_fds" > "${OUTPUT_CSV}"
fi

# Cleanup on exit
cleanup() {
    echo "[resource_monitor] terminating; final flush to ${OUTPUT_CSV}" >&2
    sync 2>/dev/null || true
    exit 0
}
trap cleanup SIGTERM SIGINT

START_EPOCH=$(date +%s)
SAMPLES=0

while true; do
    NOW_EPOCH=$(date +%s)
    if [ "${DURATION_SEC}" -gt 0 ] && [ $((NOW_EPOCH - START_EPOCH)) -ge "${DURATION_SEC}" ]; then
        break
    fi

    # Sample all postgres processes of the target user. ps output columns:
    # pid, rss (KB), vsz (KB), command
    TIMESTAMP=$(date -u +%Y-%m-%dT%H:%M:%SZ)
    while IFS= read -r line; do
        [ -z "${line}" ] && continue
        pid=$(echo "${line}"    | awk '{print $1}')
        rss_kb=$(echo "${line}" | awk '{print $2}')
        vsz_kb=$(echo "${line}" | awk '{print $3}')
        cmd=$(echo "${line}"    | awk '{for(i=4;i<=NF;i++) printf "%s ", $i; print ""}' | \
                                  sed 's/,/_/g; s/"/_/g' | cut -c1-40)
        fd_cnt=$(ls -1 "/proc/${pid}/fd" 2>/dev/null | wc -l || echo 0)
        rss_mb=$(awk -v k="${rss_kb}" 'BEGIN { printf "%.2f", k/1024 }')
        vsz_mb=$(awk -v k="${vsz_kb}" 'BEGIN { printf "%.2f", k/1024 }')
        echo "${TIMESTAMP},${pid},\"${cmd}\",${rss_mb},${vsz_mb},${fd_cnt}" >> "${OUTPUT_CSV}"
    done < <(ps -u "${TARGET_USER}" -o pid,rss,vsz,cmd --no-headers 2>/dev/null \
               | grep -F "${TARGET_PROCESS_PATTERN}" || true)

    SAMPLES=$((SAMPLES + 1))
    sleep "${INTERVAL_SEC}"
done

echo "[resource_monitor] collected ${SAMPLES} sample rounds" >&2
