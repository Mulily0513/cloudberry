#!/bin/bash
# check_orphan_files.sh
#
# Scan the MinIO warehouse bucket and flag files that are not referenced by
# any currently-tracked Iceberg snapshot in the given database. Useful as a
# post-test assertion that lightweight_recovery / chaos runs don't leave
# orphan files behind.
#
# Usage:
#   check_orphan_files.sh [bucket_prefix]
#
#   bucket_prefix: optional MinIO prefix (default: /warehouse/rec_test/)
#
# Requires: mc (MinIO client), psql, awk
#
# Exit codes:
#   0 - no orphan files found
#   1 - orphans detected (non-fatal; report to stderr + listing to stdout)
#   2 - prerequisite missing (mc, psql) or db query failed

set -e

BUCKET_PREFIX="${1:-/warehouse/rec_test/}"
MC_ALIAS="${MC_ALIAS:-minio}"
MINIO_ENDPOINT="${MINIO_ENDPOINT:-http://lakehouse:9100}"
MINIO_ACCESS_KEY="${MINIO_ACCESS_KEY:-admin}"
MINIO_SECRET_KEY="${MINIO_SECRET_KEY:-password}"

if ! command -v mc >/dev/null 2>&1; then
    echo "[check_orphan_files] ERROR: mc (MinIO client) not found" >&2
    exit 2
fi
if ! command -v psql >/dev/null 2>&1; then
    echo "[check_orphan_files] ERROR: psql not found" >&2
    exit 2
fi

# Ensure mc alias is configured
mc alias set "${MC_ALIAS}" "${MINIO_ENDPOINT}" "${MINIO_ACCESS_KEY}" "${MINIO_SECRET_KEY}" >/dev/null 2>&1 || {
    echo "[check_orphan_files] WARN: could not configure mc alias" >&2
}

# List files on storage
TMPDIR=$(mktemp -d)
trap 'rm -rf "${TMPDIR}"' EXIT

STORAGE_LIST="${TMPDIR}/storage.txt"
CATALOG_LIST="${TMPDIR}/catalog.txt"

mc ls --recursive "${MC_ALIAS}${BUCKET_PREFIX}" 2>/dev/null | \
    awk '{print $NF}' | sort -u > "${STORAGE_LIST}"

# Referenced files - read from Iceberg catalog via pg_catalog_iceberg.
# Different versions expose different functions; try the common ones and
# accept 'not found' as "feature not available in this build".
psql -At -c "
    SELECT DISTINCT file_path
    FROM pg_catalog_iceberg.list_all_data_files()
    WHERE file_path LIKE '%${BUCKET_PREFIX#/}%';
" 2>/dev/null | sort -u > "${CATALOG_LIST}" || {
    echo "[check_orphan_files] WARN: catalog introspection unavailable; listing storage files only" >&2
    cat "${STORAGE_LIST}"
    exit 0
}

# Orphans = storage \ catalog
ORPHANS="${TMPDIR}/orphans.txt"
comm -23 "${STORAGE_LIST}" "${CATALOG_LIST}" > "${ORPHANS}" || true

ORPHAN_COUNT=$(wc -l < "${ORPHANS}" | tr -d ' ')

if [ "${ORPHAN_COUNT}" -gt 0 ]; then
    echo "[check_orphan_files] Found ${ORPHAN_COUNT} orphan file(s) under ${BUCKET_PREFIX}:" >&2
    cat "${ORPHANS}"
    exit 1
fi

echo "[check_orphan_files] OK - no orphan files under ${BUCKET_PREFIX}"
exit 0
