# Vacuum performance - measure Iceberg compaction timing under various
# small-file workloads. Exercises iceberg_vacuum_compact_min_input_files and
# iceberg_vacuum_rewrite_target_file_size_mb GUCs implicitly.

PGXS := $(shell pg_config --pgxs)
include $(PGXS)

REGRESS = iceberg_vacuum_timing

installcheck:
	$(pg_regress_installcheck) $(REGRESS)

clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
