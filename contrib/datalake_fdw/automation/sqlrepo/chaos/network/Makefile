# Network chaos — inject latency/loss via tc netem during query.
# Requires CAP_NET_ADMIN (tc) inside the container.
PGXS := $(shell pg_config --pgxs)
include $(PGXS)
REGRESS = network_latency_scan
installcheck:
	$(pg_regress_installcheck) $(REGRESS)
clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
