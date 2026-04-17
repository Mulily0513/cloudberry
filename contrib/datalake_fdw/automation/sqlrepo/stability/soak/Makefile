# Soak stability tests
#
# Run a mixed INSERT/SELECT/DELETE/VACUUM workload for N iterations.
# Catch regressions via quartile comparison (last quartile vs first).
# True memory/FD monitoring needs lib/scripts/resource_monitor.sh run
# in parallel - see README. The SQL-level test alone catches timing drift.
#
# Iteration count: STABILITY_SOAK_ITERATIONS env var (default 50 CI, 500 nightly).

PGXS := $(shell pg_config --pgxs)
include $(PGXS)

REGRESS = soak_mixed_workload
SOAK_ITERS ?= $(or $(STABILITY_SOAK_ITERATIONS),50)

installcheck:
	export PGOPTIONS="-c soak.iterations=$(SOAK_ITERS)" && $(pg_regress_installcheck) $(REGRESS)

clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
