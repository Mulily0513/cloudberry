# Cross foreign table JOIN smoke test Makefile

PGXS := $(shell pg_config --pgxs)
include $(PGXS)

REGRESS = join_smoke

installcheck:
	$(pg_regress_installcheck) $(REGRESS)

clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
