#!/bin/bash
# fill_disk.sh — create a large file to consume disk space, then clean up.
#
# Usage:
#   fill_disk.sh <path> <size_mb> [hold_sec]
#
# Creates a fallocate'd file at <path>. If hold_sec is given, waits that
# long then deletes it. Otherwise exits immediately (caller deletes later).
set -e
FPATH="${1:?usage: fill_disk.sh <path> <size_mb> [hold_sec]}"
SIZE_MB="${2:?usage: fill_disk.sh <path> <size_mb>}"
HOLD_SEC="${3:-0}"

cleanup() {
    echo "[chaos/fill_disk] Cleanup: removing ${FPATH}" >&2
    rm -f "${FPATH}"
}
trap cleanup SIGTERM SIGINT EXIT

echo "[chaos/fill_disk] Allocating ${SIZE_MB}MB at ${FPATH}" >&2
fallocate -l "${SIZE_MB}M" "${FPATH}" 2>/dev/null || \
    dd if=/dev/zero of="${FPATH}" bs=1M count="${SIZE_MB}" 2>/dev/null

if [ "${HOLD_SEC}" -gt 0 ]; then
    echo "[chaos/fill_disk] Holding for ${HOLD_SEC}s" >&2
    sleep "${HOLD_SEC}"
fi

cleanup
trap - EXIT
