# Schema evolution stability tests
#
# Verify that Iceberg tables tolerate ALTER TABLE operations without data loss
# or cross-file inconsistency. The "mixed schema files" test is the critical
# one - it writes with schema v1, alters, writes with schema v2, and verifies
# a single SELECT correctly reads both file generations.

PGXS := $(shell pg_config --pgxs)
include $(PGXS)

REGRESS = add_column mixed_schema_files

installcheck:
	$(pg_regress_installcheck) $(REGRESS)

clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
