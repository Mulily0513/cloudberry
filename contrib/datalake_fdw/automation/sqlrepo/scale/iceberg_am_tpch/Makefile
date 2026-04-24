# TPC-H scale test for Iceberg AM — SF=1/10/100/1000
#
# Drives real dbgen-generated data into CREATE ICEBERG TABLE via a staging
# heap, then runs the 22 TPC-H queries and records PASS / FAIL / CRASH per
# query. Primary goal: surface bugs in the iceberg AM writer / catalog agent
# path at realistic data volumes; secondary goal: track per-query wall time.
#
# Usage:
#   make installcheck                      # default TPC_SCALE=1
#   TPC_SCALE=10 make installcheck         # ~10 GB raw
#   MODE=query   make installcheck         # reuse previously-loaded DB
#
# Depends on:
#   - TPC tools built by scripts/setup/install_tpc_tools.sh
#   - singlecluster lakehouse container running (MinIO at lakehouse:9100)

PGXS := $(shell pg_config --pgxs)
include $(PGXS)

# Dispatch to the shell driver; pg_regress output-diff is the wrong fit for
# scale-sensitive queries (row counts and aggregates change with SF).
installcheck:
	@bash $(CURDIR)/run.sh

clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
