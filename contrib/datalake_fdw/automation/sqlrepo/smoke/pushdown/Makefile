# Predicate pushdown smoke test Makefile

PGXS := $(shell pg_config --pgxs)
include $(PGXS)

REGRESS = pushdown_smoke pushdown_column_projection

installcheck:
	$(pg_regress_installcheck) $(REGRESS)

clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
