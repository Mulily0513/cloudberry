# Negative / error handling smoke test Makefile

PGXS := $(shell pg_config --pgxs)
include $(PGXS)

REGRESS = negative_smoke negative_iceberg_extended negative_error_paths

installcheck:
	$(pg_regress_installcheck) $(REGRESS)

clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
