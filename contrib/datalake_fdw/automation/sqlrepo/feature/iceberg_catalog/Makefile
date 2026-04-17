# Iceberg Catalog Feature Tests Makefile

PGXS := $(shell pg_config --pgxs)
include $(PGXS)

REGRESS = 01_catalog_lifecycle 02_catalog_multi

installcheck:
	$(pg_regress_installcheck) $(REGRESS)

clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
