#!/bin/bash
# Run the TPC-DS basic functional smoke for Iceberg AM inside the CBDB
# development container.
#
# Usage:
#   ./run.sh                      # default database "iceberg_am_tpcds_smoke"
#   DB=mydb ./run.sh
#   CONTAINER=<name> ./run.sh
set -eu

DB="${DB:-iceberg_am_tpcds_smoke}"
CONTAINER="${CONTAINER:-hashdata-lightning-umbrella-hashdata-1}"
PGPORT="${PGPORT:-7000}"

HERE_IN_CONTAINER="/workspace/database/contrib/datalake_fdw/automation/sqlrepo/smoke/iceberg_am_tpcds"

docker exec -u gpadmin "$CONTAINER" bash -c "
  source /workspace/dist/database/cloudberry-env.sh
  export PGPORT=$PGPORT
  psql -d postgres -v ON_ERROR_STOP=1 <<SQL
    DROP DATABASE IF EXISTS $DB;
    CREATE DATABASE $DB;
SQL
  cd $HERE_IN_CONTAINER
  psql -d $DB -v ON_ERROR_STOP=1 -f sql/iceberg_am_tpcds_basic.sql 2>&1 | tail -500
"
