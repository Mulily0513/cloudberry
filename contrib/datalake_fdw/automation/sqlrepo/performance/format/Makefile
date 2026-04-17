# Format performance - compare scan throughput across S3 foreign tables in
# ORC vs Parquet format on the same underlying row count.
#
# REQUIRES: /perf-data/orc/10k/ and /perf-data/parquet/10k/ on the MinIO
# bucket (see prepare/prepare_s3/). If that data isn't prepared, the tests
# will fail at the CREATE FOREIGN TABLE step; that's expected.

PGXS := $(shell pg_config --pgxs)
include $(PGXS)

REGRESS = s3_format_compare

installcheck:
	$(pg_regress_installcheck) $(REGRESS)

clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
