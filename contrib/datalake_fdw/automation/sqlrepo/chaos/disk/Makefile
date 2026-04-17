# Disk chaos — fill /tmp during query, verify clean error + recovery.
PGXS := $(shell pg_config --pgxs)
include $(PGXS)
REGRESS = disk_fill_during_sort
installcheck:
	$(pg_regress_installcheck) $(REGRESS)
clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
