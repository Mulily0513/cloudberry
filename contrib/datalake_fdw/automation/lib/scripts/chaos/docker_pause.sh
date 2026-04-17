#!/bin/bash
# docker_pause.sh — pause/unpause a docker container for fault injection.
#
# Usage:
#   docker_pause.sh <container> <pause_sec>   # pause for N seconds then unpause
#   docker_pause.sh <container> -u            # just unpause (manual recovery)
set -e
CONTAINER="${1:?usage: docker_pause.sh <container> <pause_sec|-u>}"
ACTION="${2:?usage: docker_pause.sh <container> <pause_sec|-u>}"

cleanup() {
    echo "[chaos/docker_pause] Cleanup: unpausing ${CONTAINER}" >&2
    docker unpause "${CONTAINER}" 2>/dev/null || true
}

if [ "${ACTION}" = "-u" ]; then
    docker unpause "${CONTAINER}"
    echo "[chaos/docker_pause] Unpaused ${CONTAINER}" >&2
    exit 0
fi

trap cleanup SIGTERM SIGINT EXIT
echo "[chaos/docker_pause] Pausing ${CONTAINER} for ${ACTION}s" >&2
docker pause "${CONTAINER}"
sleep "${ACTION}"
docker unpause "${CONTAINER}"
echo "[chaos/docker_pause] Unpaused ${CONTAINER} after ${ACTION}s" >&2
trap - EXIT
