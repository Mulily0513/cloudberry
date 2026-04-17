# Crash chaos test — kill -9 a backend during INSERT, verify recovery.
# This test uses pg_regress for the setup/verify SQL, and a separate shell
# script for the actual kill. The shell step is invoked via \! from SQL.

PGXS := $(shell pg_config --pgxs)
include $(PGXS)
REGRESS = crash_insert_kill
installcheck:
	$(pg_regress_installcheck) $(REGRESS)
clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
