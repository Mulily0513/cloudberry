#!/usr/bin/env bash
set -euo pipefail

LOG_DIR=/var/log/lakehouse

export HADOOP_HOME=${HADOOP_HOME:-/opt/hadoop}
export HIVE_HOME=${HIVE_HOME:-/opt/hive}
export SPARK_HOME=${SPARK_HOME:-/opt/spark}
export HIVE_CONF_DIR=${HIVE_HOME}/conf
export PATH=$PATH:${HADOOP_HOME}/bin:${HIVE_HOME}/bin:${SPARK_HOME}/bin

export MINIO_ROOT_USER=${MINIO_ROOT_USER:-minio}
export MINIO_ROOT_PASSWORD=${MINIO_ROOT_PASSWORD:-minio123}
export S3_ENDPOINT=${S3_ENDPOINT:-http://localhost:9100}
export S3_ACCESS_KEY=${S3_ACCESS_KEY:-${MINIO_ROOT_USER}}
export S3_SECRET_KEY=${S3_SECRET_KEY:-${MINIO_ROOT_PASSWORD}}
export WAREHOUSE_S3=${WAREHOUSE_S3:-s3a://warehouse/}
export HDFS_NN_URI=${HDFS_NN_URI:-hdfs://localhost:8020}
export METASTORE_DB_USER=${METASTORE_DB_USER:-hive}
export METASTORE_DB_PASS=${METASTORE_DB_PASS:-hivepw}
export METASTORE_DB_NAME=${METASTORE_DB_NAME:-metastore}

WAREHOUSE_DIR=${WAREHOUSE_S3}
if [ -z "${WAREHOUSE_DIR}" ]; then
  WAREHOUSE_DIR="s3a://warehouse/"
fi
HIVE_WAREHOUSE_DIR="hdfs://lakehouse:8020/warehouse"

log_stage() {
  echo "[stage] $*" | tee -a "${LOG_DIR}/startup.log"
}

setup_dirs() {
  mkdir -p "${LOG_DIR}"
}

copy_jars() {
  log_stage "copy jars into Spark/Hive"
  cp /opt/jars/* "${SPARK_HOME}/jars/"
  cp /opt/jars/* "${HIVE_HOME}/lib/"
}

write_hadoop_configs() {
  log_stage "write Hadoop configs"
  cat > "${HADOOP_HOME}/etc/hadoop/core-site.xml" <<EOF
<?xml version="1.0"?>
<configuration>
  <property>
    <name>fs.defaultFS</name>
    <value>${HDFS_NN_URI}</value>
  </property>
  <property>
    <name>fs.s3a.endpoint</name>
    <value>${S3_ENDPOINT}</value>
  </property>
  <property>
    <name>fs.s3a.access.key</name>
    <value>${S3_ACCESS_KEY}</value>
  </property>
  <property>
    <name>fs.s3a.secret.key</name>
    <value>${S3_SECRET_KEY}</value>
  </property>
  <property>
    <name>fs.s3a.path.style.access</name>
    <value>true</value>
  </property>
  <property>
    <name>fs.s3a.aws.credentials.provider</name>
    <value>org.apache.hadoop.fs.s3a.SimpleAWSCredentialsProvider</value>
  </property>
  <property>
    <name>fs.s3a.impl</name>
    <value>org.apache.hadoop.fs.s3a.S3AFileSystem</value>
  </property>
  <property>
    <name>fs.s3a.connection.ssl.enabled</name>
    <value>false</value>
  </property>
  <!-- Map the standard "s3" URI scheme to the same S3A backend so SQL using
       LOCATION 's3://...' is accepted on the Hive side without rewriting it
       to 's3a://...'.  Without this Hive throws UnsupportedFileSystemException. -->
  <property>
    <name>fs.s3.impl</name>
    <value>org.apache.hadoop.fs.s3a.S3AFileSystem</value>
  </property>
  <property>
    <name>fs.AbstractFileSystem.s3.impl</name>
    <value>org.apache.hadoop.fs.s3a.S3A</value>
  </property>
  <property>
    <name>fs.s3.endpoint</name>
    <value>${S3_ENDPOINT}</value>
  </property>
  <property>
    <name>fs.s3.access.key</name>
    <value>${S3_ACCESS_KEY}</value>
  </property>
  <property>
    <name>fs.s3.secret.key</name>
    <value>${S3_SECRET_KEY}</value>
  </property>
  <property>
    <name>fs.s3.path.style.access</name>
    <value>true</value>
  </property>
  <property>
    <name>fs.s3.connection.ssl.enabled</name>
    <value>false</value>
  </property>
  <property>
    <name>hadoop.proxyuser.root.hosts</name>
    <value>*</value>
  </property>
  <property>
    <name>hadoop.proxyuser.root.groups</name>
    <value>*</value>
  </property>
</configuration>
EOF

  cat > "${HADOOP_HOME}/etc/hadoop/hdfs-site.xml" <<EOF
<?xml version="1.0"?>
<configuration>
  <property>
    <name>dfs.replication</name>
    <value>1</value>
  </property>
  <property>
    <name>dfs.namenode.name.dir</name>
    <value>file:/data/hdfs/nn</value>
  </property>
  <property>
    <name>dfs.datanode.data.dir</name>
    <value>file:/data/hdfs/dn</value>
  </property>
  <property>
    <name>dfs.permissions.enabled</name>
    <value>false</value>
  </property>
</configuration>
EOF
}

write_hive_configs() {
  log_stage "write Hive configs"
  cat > "${HIVE_CONF_DIR}/hive-site.xml" <<EOF
<?xml version="1.0"?>
<configuration>
  <property>
    <name>javax.jdo.option.ConnectionURL</name>
    <value>jdbc:postgresql://localhost:5432/${METASTORE_DB_NAME}</value>
  </property>
  <property>
    <name>javax.jdo.option.ConnectionDriverName</name>
    <value>org.postgresql.Driver</value>
  </property>
  <property>
    <name>javax.jdo.option.ConnectionUserName</name>
    <value>${METASTORE_DB_USER}</value>
  </property>
  <property>
    <name>javax.jdo.option.ConnectionPassword</name>
    <value>${METASTORE_DB_PASS}</value>
  </property>
  <property>
    <name>datanucleus.autoCreateSchema</name>
    <value>false</value>
  </property>
  <property>
    <name>hive.metastore.uris</name>
    <value>thrift://lakehouse:9083</value>
  </property>
  <property>
    <name>hive.metastore.thrift.bind.host</name>
    <value>0.0.0.0</value>
  </property>
  <property>
    <name>hive.metastore.port</name>
    <value>9083</value>
  </property>
  <property>
    <name>hive.metastore.warehouse.dir</name>
    <value>${HIVE_WAREHOUSE_DIR}</value>
  </property>
  <property>
    <name>hive.support.concurrency</name>
    <value>false</value>
  </property>
  <property>
    <name>hive.txn.manager</name>
    <value>org.apache.hadoop.hive.ql.lockmgr.DummyTxnManager</value>
  </property>
  <property>
    <name>hive.compactor.initiator.on</name>
    <value>false</value>
  </property>
  <property>
    <name>hive.compactor.worker.threads</name>
    <value>1</value>
  </property>
  <property>
    <name>hive.server2.thrift.port</name>
    <value>10000</value>
  </property>
  <property>
    <name>hive.server2.thrift.bind.host</name>
    <value>0.0.0.0</value>
  </property>
  <property>
    <name>hive.server2.transport.mode</name>
    <value>binary</value>
  </property>
  <property>
    <name>hive.server2.authentication</name>
    <value>NONE</value>
  </property>
  <property>
    <name>metastore.metastore.event.db.notification.api.auth</name>
    <value>false</value>
  </property>
</configuration>
EOF
}

write_spark_configs() {
  log_stage "write Spark configs"
  mkdir -p "${SPARK_HOME}/conf"
  cat > "${SPARK_HOME}/conf/spark-defaults.conf" <<EOF
spark.master                     spark://lakehouse:7077
spark.eventLog.enabled           true
spark.eventLog.dir               ${WAREHOUSE_DIR}/spark-events
spark.history.fs.logDirectory    ${WAREHOUSE_DIR}/spark-events
spark.sql.catalogImplementation  hive
spark.sql.extensions             org.apache.iceberg.spark.extensions.IcebergSparkSessionExtensions
spark.sql.catalog.spark_catalog  org.apache.iceberg.spark.SparkSessionCatalog
spark.sql.catalog.spark_catalog.type hive
spark.sql.catalog.spark_catalog.warehouse ${WAREHOUSE_DIR}
spark.hadoop.fs.s3a.endpoint     ${S3_ENDPOINT}
spark.hadoop.fs.s3a.path.style.access true
spark.hadoop.fs.s3a.access.key   ${S3_ACCESS_KEY}
spark.hadoop.fs.s3a.secret.key   ${S3_SECRET_KEY}
spark.hadoop.fs.s3a.aws.credentials.provider org.apache.hadoop.fs.s3a.SimpleAWSCredentialsProvider
spark.hadoop.fs.s3a.impl         org.apache.hadoop.fs.s3a.S3AFileSystem
spark.hadoop.fs.s3a.connection.ssl.enabled false
spark.hadoop.fs.defaultFS        ${HDFS_NN_URI}
EOF
}

start_postgres() {
  log_stage "start PostgreSQL"
  PG_VERSION=$(ls /etc/postgresql | head -n1)
  pg_ctlcluster "${PG_VERSION}" main start
  PG_HBA="/etc/postgresql/${PG_VERSION}/main/pg_hba.conf"
  PG_CONF="/etc/postgresql/${PG_VERSION}/main/postgresql.conf"
  # Hive's bundled PostgreSQL JDBC driver doesn't support SCRAM (auth type 10).
  sed -i.bak -E 's/^(local\s+all\s+all\s+)scram-sha-256/\1md5/' "${PG_HBA}"
  sed -i.bak -E 's/^(host\s+all\s+all\s+127\.0\.0\.1\/32\s+)scram-sha-256/\1md5/' "${PG_HBA}"
  sed -i.bak -E 's/^(host\s+all\s+all\s+::1\/128\s+)scram-sha-256/\1md5/' "${PG_HBA}"
  if ! grep -q '^password_encryption\s*=\s*md5' "${PG_CONF}"; then
    sed -i.bak -E 's/^#?\s*password_encryption\s*=.*/password_encryption = md5/' "${PG_CONF}"
  fi
  pg_ctlcluster "${PG_VERSION}" main reload
  sudo -u postgres psql -tc "SELECT 1 FROM pg_roles WHERE rolname='${METASTORE_DB_USER}'" | grep -q 1 || \
    sudo -u postgres psql -c "CREATE USER ${METASTORE_DB_USER} WITH PASSWORD '${METASTORE_DB_PASS}'"
  sudo -u postgres psql -tc "SELECT 1 FROM pg_database WHERE datname='${METASTORE_DB_NAME}'" | grep -q 1 || \
    sudo -u postgres createdb "${METASTORE_DB_NAME}"
  sudo -u postgres psql -c "GRANT ALL PRIVILEGES ON DATABASE ${METASTORE_DB_NAME} TO ${METASTORE_DB_USER}" || true
}

start_hdfs() {
  log_stage "format and start HDFS"
  if [ ! -f /data/hdfs/nn/current/VERSION ]; then
    hdfs namenode -format -force -nonInteractive
  fi
  hdfs --daemon start namenode
  hdfs --daemon start datanode
  sleep 10
  hdfs dfs -mkdir -p "${HIVE_WAREHOUSE_DIR}"
  hdfs dfs -chmod 777 "${HIVE_WAREHOUSE_DIR}"
}

start_minio() {
  log_stage "start MinIO"
  mkdir -p /data/warehouse
  minio server /data/warehouse --address ":9100" --console-address ":9200" >"${LOG_DIR}/minio.log" 2>&1 &
  sleep 3
  mc alias set local http://127.0.0.1:9100 "${MINIO_ROOT_USER}" "${MINIO_ROOT_PASSWORD}" >/dev/null
  mc mb -p local/warehouse >/dev/null || true
  mc mb -p local/warehouse/spark-events >/dev/null || true
}

init_hive_metastore() {
  log_stage "init Hive schema"
  /opt/init-metastore.sh
}

start_hive_services() {
  log_stage "start Hive Metastore and HS2"
  nohup ${HIVE_HOME}/bin/hive --service metastore >"${LOG_DIR}/hivemetastore.log" 2>&1 &
  sleep 5
  nohup ${HIVE_HOME}/bin/hive --service hiveserver2 >"${LOG_DIR}/hiveserver2.log" 2>&1 &
}

start_spark() {
  log_stage "start Spark"
  ${SPARK_HOME}/sbin/start-master.sh
  ${SPARK_HOME}/sbin/start-worker.sh spark://lakehouse:7077
  ${SPARK_HOME}/sbin/start-history-server.sh
}

main() {
  setup_dirs
  copy_jars
  write_hadoop_configs
  write_hive_configs
  write_spark_configs

  start_postgres
  start_hdfs
  start_minio

  log_stage "write hive-env.sh (larger heap)"
  cat > "${HIVE_HOME}/conf/hive-env.sh" <<"HIVE_ENV"
export HADOOP_HEAPSIZE=2048
export HADOOP_CLIENT_OPTS="-Xmx2048m -Xms512m ${HADOOP_CLIENT_OPTS}"
HIVE_ENV
  chmod +x "${HIVE_HOME}/conf/hive-env.sh"
  init_hive_metastore
  start_hive_services
  start_spark

  echo "[ready] services started, tailing logs" | tee -a "${LOG_DIR}/startup.log"
  tail -n 200 -f "${LOG_DIR}/startup.log"
}

main "$@"

