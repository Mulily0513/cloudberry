# Catalog consistency tests
#
# Verify builtin / polaris / hive catalogs produce identical query results
# for the same logical operations. Runs the same DDL+DML+SELECT sequence on
# each catalog and md5-hashes the results for comparison.
#
# REQUIRES: builtin (always), polaris (singlecluster-polaris-1:8181), and
# hive (lakehouse:9083). Missing catalogs cause the test to skip gracefully.

PGXS := $(shell pg_config --pgxs)
include $(PGXS)

REGRESS = data_consistency

installcheck:
	$(pg_regress_installcheck) $(REGRESS)

clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
