# Hive smoke test Makefile

PGXS := $(shell pg_config --pgxs)
include $(PGXS)

# hive_text_compressed and hive_edge_coverage are temporarily disabled:
# both require `test_text_snappy` which is built by `hive_smoke.sql` via an
# `INSERT ... SET SnappyCodec` that needs Hive-on-MR + YARN, but the
# lakehouse-allinone image doesn't run a ResourceManager/NodeManager.
# Re-enable once the image ships YARN (or the prep is rewritten to use spark-sql).
REGRESS = hive_smoke hive_dlproxy_coverage hive_iceberg_fdw hive_iceberg_fdw_write

AUTOMATION_DIR = $(shell cd ../../.. && pwd)
HIVE_PREPARE_SQL          = $(AUTOMATION_DIR)/prepare/prepare_hive/hive_smoke.sql
HIVE_ICEBERG_PREPARE_SQL  = $(AUTOMATION_DIR)/prepare/prepare_hive/hive_iceberg_fdw.sql

# Spark + Iceberg-on-Hive catalog config for the iceberg_fdw_test fixture.
SPARK_SQL ?= spark-sql
SPARK_CONF = \
	--master local[*] \
	--conf spark.hadoop.hive.metastore.uris=thrift://lakehouse:9083 \
	--conf spark.hadoop.fs.s3a.endpoint=http://lakehouse:9100 \
	--conf spark.hadoop.fs.s3a.access.key=admin \
	--conf spark.hadoop.fs.s3a.secret.key=password \
	--conf spark.hadoop.fs.s3a.path.style.access=true \
	--conf spark.hadoop.fs.s3a.aws.credentials.provider=org.apache.hadoop.fs.s3a.SimpleAWSCredentialsProvider \
	--conf spark.sql.extensions=org.apache.iceberg.spark.extensions.IcebergSparkSessionExtensions \
	--conf spark.sql.catalog.ic=org.apache.iceberg.spark.SparkCatalog \
	--conf spark.sql.catalog.ic.type=hive \
	--conf spark.sql.catalog.ic.uri=thrift://lakehouse:9083

installcheck:
	@echo "Loading Hive test data..."
	@if [ -f "$(HIVE_PREPARE_SQL)" ]; then \
		echo "Found SQL file: $(HIVE_PREPARE_SQL)"; \
		beeline -u "jdbc:hive2://lakehouse:10000/default" -n hive -f "$(HIVE_PREPARE_SQL)" 2>&1 || \
		echo "WARNING: Failed to load Hive test data (beeline not available or Hive not ready)"; \
	else \
		echo "WARNING: Hive prepare SQL not found at $(HIVE_PREPARE_SQL)"; \
	fi
	@echo "Loading Iceberg-on-Hive fixture (default.iceberg_fdw_test)..."
	@if [ -f "$(HIVE_ICEBERG_PREPARE_SQL)" ] && command -v $(SPARK_SQL) >/dev/null 2>&1; then \
		$(SPARK_SQL) $(SPARK_CONF) -f "$(HIVE_ICEBERG_PREPARE_SQL)" 2>&1 | tail -10 || \
		echo "WARNING: Failed to load Iceberg-on-Hive fixture"; \
	else \
		echo "WARNING: skipping Iceberg-on-Hive prep (spark-sql or SQL file missing)"; \
	fi
	$(pg_regress_installcheck) $(REGRESS)

clean:
	rm -rf results/ regression.diffs regression.out tmp_check/ log/
