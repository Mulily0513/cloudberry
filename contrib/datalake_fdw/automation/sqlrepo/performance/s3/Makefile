# S3 Performance Tests Makefile

PGXS := $(shell pg_config --pgxs)
include $(PGXS)

REGRESS = s3_read_performance

installcheck:
	$(pg_regress_installcheck) $(REGRESS)

clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
