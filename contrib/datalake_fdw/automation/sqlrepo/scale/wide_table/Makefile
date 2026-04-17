PGXS := $(shell pg_config --pgxs)
include $(PGXS)

REGRESS = scale_wide_table

installcheck:
	$(pg_regress_installcheck) $(REGRESS)

clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
