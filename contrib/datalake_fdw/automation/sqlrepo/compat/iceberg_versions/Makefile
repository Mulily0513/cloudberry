# Iceberg version compat: read tables written by different Iceberg spec / writer versions.
PGXS := $(shell pg_config --pgxs)
include $(PGXS)
REGRESS = version_compat
installcheck:
	@command -v spark-sql >/dev/null 2>&1 || { echo "SKIP: spark-sql not found"; exit 0; }
	$(pg_regress_installcheck) $(REGRESS)
clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
