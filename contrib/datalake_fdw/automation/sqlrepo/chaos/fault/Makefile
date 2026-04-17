# Fault chaos test — docker pause/unpause a service during query.
# Uses \! to invoke docker_pause.sh from within the SQL test.

PGXS := $(shell pg_config --pgxs)
include $(PGXS)
REGRESS = fault_minio_pause
installcheck:
	$(pg_regress_installcheck) $(REGRESS)
clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
