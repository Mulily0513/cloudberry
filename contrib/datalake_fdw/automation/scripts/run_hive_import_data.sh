#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SQL_FILE="${1:-${SCRIPT_DIR}/../prepare/prepare_hive/hive_smoke.sql}"

HIVE_DATABASE="${HIVE_DATABASE:-default}"

echo "Importing data to Hive..."
echo "SQL file: ${SQL_FILE}"
echo "Database: ${HIVE_DATABASE}"

if [ ! -f "${SQL_FILE}" ]; then
    echo "Error: SQL file not found: ${SQL_FILE}"
    exit 1
fi

hive --database "${HIVE_DATABASE}" -f "${SQL_FILE}"

echo "Data import completed successfully!"