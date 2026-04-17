# Iceberg polaris smoke test Makefile

PGXS := $(shell pg_config --pgxs)
include $(PGXS)

REGRESS = iceberg_polaris iceberg_polaris_types iceberg_polaris_statistics iceberg_polaris_deferred_commit \
          iceberg_polaris_options iceberg_polaris_toolkit

POLARIS_CLI = ../../../../docker/singlecluster/scripts/polaris_cli.sh

installcheck:
	@echo "Cleaning Polaris catalog before test..."
	@rm -f regression.diffs
	@$(POLARIS_CLI) delete-catalog-force || echo "WARNING: Polaris catalog cleanup failed (non-fatal)"
	@$(POLARIS_CLI) create-namespace || echo "WARNING: Polaris namespace creation failed (non-fatal)"
	$(pg_regress_installcheck) $(REGRESS)

clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
