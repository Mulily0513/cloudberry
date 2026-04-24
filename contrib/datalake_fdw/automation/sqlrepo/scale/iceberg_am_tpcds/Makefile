# TPC-DS scale test for Iceberg AM — SF=1/10/100/1000
#
# Mirrors scale/iceberg_am_tpch's layout for the 24-table TPC-DS schema:
# real dsdgen-generated .dat files go through a staging heap into
# CREATE ICEBERG TABLE targets. Same MODE (ddl/load/query/both/clean) and
# TPC_SCALE env as the TPC-H category.
#
# Primary goal: exercise the iceberg AM writer path under more complex
# join + window patterns than TPC-H; surface bugs that TPC-H doesn't cover
# (nested subqueries, ROLLUP variants, 5+ dim star joins, ...).
#
# Requires:
#   - scripts/setup/install_tpc_tools.sh has built dsdgen
#   - singlecluster lakehouse container running (MinIO at lakehouse:9100)

PGXS := $(shell pg_config --pgxs)
include $(PGXS)

installcheck:
	@bash $(CURDIR)/run.sh

clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
