#!/usr/bin/env bash
set -euo pipefail

export HIVE_HOME=${HIVE_HOME:-/opt/hive}
export HIVE_CONF_DIR=${HIVE_CONF_DIR:-${HIVE_HOME}/conf}

echo "[init-metastore] initializing schema if needed"
schematool -dbType postgres -initSchema --verbose || true

