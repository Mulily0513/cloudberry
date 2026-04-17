# Lightweight recovery tests
#
# Verify that Iceberg tables remain consistent after in-SQL abort scenarios:
#   - BEGIN; INSERT ...; ROLLBACK;
#   - Abort during VACUUM compaction
#   - Concurrent ABORT storm
#
# These do NOT use kill -9 / docker pause / network injection - they only use
# SQL-level aborts and pg_terminate_backend, so the category is safe to run in
# default CI. True crash tests (kill -9, OOM, restart) live under sqlrepo/chaos/.

PGXS := $(shell pg_config --pgxs)
include $(PGXS)

# All tests here share the common Iceberg setup; the SQL files include
# common_setup.sql and do their own catalog/volume DDL.
REGRESS = abort_during_insert abort_during_vacuum

installcheck:
	$(pg_regress_installcheck) $(REGRESS)

clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
