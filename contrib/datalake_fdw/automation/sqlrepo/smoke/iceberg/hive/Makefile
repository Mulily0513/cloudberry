# Iceberg hive catalog smoke test Makefile

PGXS := $(shell pg_config --pgxs)
include $(PGXS)

REGRESS = iceberg_hive iceberg_hive_deferred_commit iceberg_hive_options

# Hive Metastore requires the "public" database to exist (PG schema maps to Hive DB).
HIVE_BEELINE = beeline -u "jdbc:hive2://lakehouse:10000" --silent=true

prepare:
	@$(HIVE_BEELINE) -e "CREATE DATABASE IF NOT EXISTS public;" 2>/dev/null || \
		echo "WARNING: Could not create Hive 'public' database (beeline unavailable)"

installcheck: prepare
	$(pg_regress_installcheck) $(REGRESS)

clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
