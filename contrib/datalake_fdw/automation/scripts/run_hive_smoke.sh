#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SQL_FILE="${SCRIPT_DIR}/../prepare/prepare_hive/hive_smoke.sql"

# Hive connection settings
HIVE_HOST="${HIVE_HOST:-lakehouse}"
HIVE_PORT="${HIVE_PORT:-10000}"
HIVE_USER="${HIVE_USER:-hive}"
HIVE_DATABASE="${HIVE_DATABASE:-default}"

echo "Executing Hive smoke test SQL..."
echo "SQL file: ${SQL_FILE}"
echo "Hive: ${HIVE_USER}@${HIVE_HOST}:${HIVE_PORT}/${HIVE_DATABASE}"

beeline -u "jdbc:hive2://${HIVE_HOST}:${HIVE_PORT}/${HIVE_DATABASE}" \
        -n "${HIVE_USER}" \
        -f "${SQL_FILE}" \
        --silent=false \
        --showHeader=true

echo "Hive smoke test completed successfully!"
