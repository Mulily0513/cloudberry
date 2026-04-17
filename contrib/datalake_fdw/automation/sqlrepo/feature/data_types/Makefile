# Data Types Feature Tests Makefile

PGXS := $(shell pg_config --pgxs)
include $(PGXS)

REGRESS = 01_numeric_types 02_string_types

installcheck:
	$(pg_regress_installcheck) $(REGRESS)

clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
