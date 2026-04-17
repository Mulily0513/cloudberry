#!/bin/bash
# network_inject.sh — add/remove tc netem rules for latency/loss injection.
#
# Usage:
#   network_inject.sh add  <iface> <delay_ms> [loss_pct]
#   network_inject.sh del  <iface>
#
# Examples:
#   network_inject.sh add eth0 500 5      # 500ms delay + 5% loss
#   network_inject.sh del eth0            # clean up
set -e
CMD="${1:?usage: network_inject.sh add|del <iface> [delay_ms] [loss_pct]}"
IFACE="${2:?usage: network_inject.sh add|del <iface>}"

case "${CMD}" in
    add)
        DELAY="${3:?add requires delay_ms}"
        LOSS="${4:-0}"
        echo "[chaos/network_inject] Adding netem: ${DELAY}ms delay, ${LOSS}% loss on ${IFACE}" >&2
        tc qdisc add dev "${IFACE}" root netem delay "${DELAY}ms" loss "${LOSS}%" 2>/dev/null || \
        tc qdisc change dev "${IFACE}" root netem delay "${DELAY}ms" loss "${LOSS}%"
        ;;
    del)
        echo "[chaos/network_inject] Removing netem on ${IFACE}" >&2
        tc qdisc del dev "${IFACE}" root 2>/dev/null || true
        ;;
    *)
        echo "Unknown command: ${CMD}" >&2; exit 1
        ;;
esac
