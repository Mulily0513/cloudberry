# Pushdown performance - compare query timing with predicate pushdown on vs off.
# Uses enable_seqscan toggle as a proxy (datalake_fdw-specific pushdown GUCs
# vary by version; adapt as needed).

PGXS := $(shell pg_config --pgxs)
include $(PGXS)

REGRESS = iceberg_pushdown_compare

installcheck:
	$(pg_regress_installcheck) $(REGRESS)

clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
