# Plan stability - capture EXPLAIN for representative queries and diff against
# expected. Uses EXPLAIN (COSTS off) so output is stable across runs; catches
# optimizer regressions without caring about row estimate drift.

PGXS := $(shell pg_config --pgxs)
include $(PGXS)

REGRESS = iceberg_plan_stability

installcheck:
	$(pg_regress_installcheck) $(REGRESS)

clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
