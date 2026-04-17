# FDW Pushdown Feature Tests Makefile

PGXS := $(shell pg_config --pgxs)
include $(PGXS)

REGRESS = 01_filter_pushdown 02_advanced_pushdown

S3_PATHS = /warehouse/fdw-test/feature/pushdown/

installcheck: clean-s3
	$(pg_regress_installcheck) $(REGRESS)

clean-s3:
	@for p in $(S3_PATHS); do \
		MC_HOST_minio=http://$${MINIO_ACCESS_KEY:-admin}:$${MINIO_SECRET_KEY:-password}@$${MINIO_HOST:-lakehouse}:$${MINIO_PORT:-9100} \
		mc rm --recursive --force minio$$p 2>/dev/null; \
	done; true

clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
