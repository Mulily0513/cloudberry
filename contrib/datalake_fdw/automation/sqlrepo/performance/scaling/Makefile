# Scaling performance - verify time complexity stays linear as data grows.
# Measures scan/aggregation time for 1e3, 1e4, and 1e5 rows and writes all
# three as separate operations into perf_results_summary.

PGXS := $(shell pg_config --pgxs)
include $(PGXS)

REGRESS = iceberg_scaling

installcheck:
	$(pg_regress_installcheck) $(REGRESS)

clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
