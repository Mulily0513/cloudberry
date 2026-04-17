#!/bin/bash
# kill_backend.sh — send a signal to a specific postgres backend PID.
# Default signal: KILL (9). For graceful: use TERM (15).
#
# Usage: kill_backend.sh <pid> [signal]
set -e
PID="${1:?usage: kill_backend.sh <pid> [signal]}"
SIG="${2:-9}"
echo "[chaos/kill_backend] Sending signal ${SIG} to PID ${PID}" >&2
kill -"${SIG}" "${PID}"
