# Spark→HashData compat: Spark writes Iceberg tables, HashData reads them.
# REQUIRES: spark-sql (not installed in current env - test will skip).
PGXS := $(shell pg_config --pgxs)
include $(PGXS)
REGRESS = spark_write_hd_read
installcheck:
	@command -v spark-sql >/dev/null 2>&1 || { echo "SKIP: spark-sql not found"; exit 0; }
	$(pg_regress_installcheck) $(REGRESS)
clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
