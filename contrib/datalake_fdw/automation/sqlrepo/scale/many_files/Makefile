PGXS := $(shell pg_config --pgxs)
include $(PGXS)

REGRESS = scale_many_files

installcheck:
	$(pg_regress_installcheck) $(REGRESS)

clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
