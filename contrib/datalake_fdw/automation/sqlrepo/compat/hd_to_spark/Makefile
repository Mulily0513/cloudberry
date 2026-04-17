# HashData→Spark compat: HashData writes, Spark reads and verifies.
PGXS := $(shell pg_config --pgxs)
include $(PGXS)
REGRESS = hd_write_spark_read
installcheck:
	@command -v spark-sql >/dev/null 2>&1 || { echo "SKIP: spark-sql not found"; exit 0; }
	$(pg_regress_installcheck) $(REGRESS)
clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
