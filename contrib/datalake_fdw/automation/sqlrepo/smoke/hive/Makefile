# Hive smoke test Makefile

PGXS := $(shell pg_config --pgxs)
include $(PGXS)

REGRESS = hive_smoke hive_dlproxy_coverage hive_iceberg_fdw hive_text_compressed hive_edge_coverage hive_iceberg_fdw_write

AUTOMATION_DIR = $(shell cd ../../.. && pwd)
HIVE_PREPARE_SQL = $(AUTOMATION_DIR)/prepare/prepare_hive/hive_smoke.sql

installcheck:
	@echo "Loading Hive test data..."
	@if [ -f "$(HIVE_PREPARE_SQL)" ]; then \
		echo "Found SQL file: $(HIVE_PREPARE_SQL)"; \
		beeline -u "jdbc:hive2://lakehouse:10000/default" -n hive -f "$(HIVE_PREPARE_SQL)" 2>&1 || \
		echo "WARNING: Failed to load Hive test data (beeline not available or Hive not ready)"; \
	else \
		echo "WARNING: Hive prepare SQL not found at $(HIVE_PREPARE_SQL)"; \
	fi
	$(pg_regress_installcheck) $(REGRESS)

clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
