# Concurrency stability tests (pg_isolation_regress)
#
# Unlike pg_regress, pg_isolation_regress runs .spec files that define
# multiple concurrent sessions and permutations. Each spec verifies a
# specific multi-session invariant (snapshot isolation, last-write-wins,
# reader-during-vacuum, etc.).
#
# Specs live under specs/, expected output under expected/. pg_isolation_regress
# compares actual output (output_iso/results) to expected and reports diffs.
#
# First-run workflow:
#   1. expected/*.out is empty or missing
#   2. Running `make installcheck` fails-with-diff but writes output_iso/results/
#   3. Human reviews the captured output; if behaviour is correct, copies
#      output_iso/results/<name>.out -> expected/<name>.out and commits

PGXS := $(shell pg_config --pgxs)
include $(PGXS)

# Specs to run. Add new specs here when ready to include in CI.
# Iceberg-dependent specs are commented out by default - enable after the
# Iceberg catalog server is reachable and the spec's expected/ is populated.
ISOLATION = concurrent_read_regular
# ISOLATION += snapshot_isolation_iceberg
# ISOLATION += concurrent_insert_iceberg

installcheck:
	$(pg_isolation_regress_installcheck) $(ISOLATION)

clean:
	rm -rf output_iso/ regression.diffs regression.out
