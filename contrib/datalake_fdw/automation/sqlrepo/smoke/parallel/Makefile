# Parallel / multi-segment smoke test Makefile

PGXS := $(shell pg_config --pgxs)
include $(PGXS)

HDFS_CMD = hdfs dfs

REGRESS = parallel_smoke

installcheck:
	@echo "=== Cleaning old HDFS parallel test data ==="
	@$(HDFS_CMD) -rm -r -f /test/parallel/data 2>/dev/null || true
	@echo "=== Running pg_regress tests ==="
	$(pg_regress_installcheck) $(REGRESS)

clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
