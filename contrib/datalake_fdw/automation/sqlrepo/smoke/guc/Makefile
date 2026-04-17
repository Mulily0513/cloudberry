# GUC parameters smoke test Makefile

PGXS := $(shell pg_config --pgxs)
include $(PGXS)

REGRESS = guc_smoke guc_iceberg_vacuum

installcheck:
	$(pg_regress_installcheck) $(REGRESS)

clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
