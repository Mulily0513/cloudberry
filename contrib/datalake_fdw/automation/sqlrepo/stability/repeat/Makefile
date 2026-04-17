# Repeat stability tests
#
# Run a full CREATE -> INSERT -> UPDATE -> DELETE -> VACUUM -> DROP cycle
# many times to surface flaky ops, leaks, and catalog drift.

PGXS := $(shell pg_config --pgxs)
include $(PGXS)

REGRESS = repeat_iceberg_cycle
REPEAT_ITERS ?= $(or $(STABILITY_REPEAT_ITERATIONS),10)

installcheck:
	export PGOPTIONS="-c rep.iterations=$(REPEAT_ITERS)" && $(pg_regress_installcheck) $(REGRESS)

clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
