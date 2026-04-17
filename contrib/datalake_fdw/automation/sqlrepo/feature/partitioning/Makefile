# Partitioning Feature Tests Makefile

PGXS := $(shell pg_config --pgxs)
include $(PGXS)

REGRESS = 01_single_partition 02_multi_partition

installcheck:
	$(pg_regress_installcheck) $(REGRESS)

clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
