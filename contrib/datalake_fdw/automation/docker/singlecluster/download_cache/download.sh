#!/usr/bin/env bash
#
# Pre-download all packages into this directory.
# Run once, then subsequent `docker compose build` will use local cache.
#
# Usage:  bash download.sh
#
set -euo pipefail
cd "$(dirname "$0")"

HADOOP_VERSION="${HADOOP_VERSION:-3.0.0}"
HIVE_VERSION="${HIVE_VERSION:-3.0.0}"
SPARK_VERSION="${SPARK_VERSION:-3.3.3}"
HUDI_VERSION="${HUDI_VERSION:-0.11.1}"
ICEBERG_VERSION="${ICEBERG_VERSION:-1.1.0}"

download() {
  local file="$1" url="$2"
  if [ -f "$file" ]; then
    echo "[skip] $file already exists"
  else
    echo "[downloading] $file ..."
    curl -fSL "$url" -o "$file"
    echo "[done] $file"
  fi
}

# Apache components
download "hadoop-${HADOOP_VERSION}.tar.gz" \
  "https://archive.apache.org/dist/hadoop/common/hadoop-${HADOOP_VERSION}/hadoop-${HADOOP_VERSION}.tar.gz"

download "apache-hive-${HIVE_VERSION}-bin.tar.gz" \
  "https://archive.apache.org/dist/hive/hive-${HIVE_VERSION}/apache-hive-${HIVE_VERSION}-bin.tar.gz"

download "spark-${SPARK_VERSION}-bin-hadoop3.tgz" \
  "https://archive.apache.org/dist/spark/spark-${SPARK_VERSION}/spark-${SPARK_VERSION}-bin-hadoop3.tgz"

# MinIO
download "minio" \
  "https://dl.min.io/server/minio/release/linux-amd64/minio"

download "mc" \
  "https://dl.min.io/client/mc/release/linux-amd64/mc"

# Maven JARs (aliyun mirror)
MAVEN="https://maven.aliyun.com/repository/central"

download "hudi-spark3-bundle_2.12-${HUDI_VERSION}.jar" \
  "${MAVEN}/org/apache/hudi/hudi-spark3-bundle_2.12/${HUDI_VERSION}/hudi-spark3-bundle_2.12-${HUDI_VERSION}.jar"

download "iceberg-spark-runtime-3.3_2.12-${ICEBERG_VERSION}.jar" \
  "${MAVEN}/org/apache/iceberg/iceberg-spark-runtime-3.3_2.12/${ICEBERG_VERSION}/iceberg-spark-runtime-3.3_2.12-${ICEBERG_VERSION}.jar"

download "hadoop-aws-${HADOOP_VERSION}.jar" \
  "${MAVEN}/org/apache/hadoop/hadoop-aws/${HADOOP_VERSION}/hadoop-aws-${HADOOP_VERSION}.jar"

download "aws-java-sdk-bundle-1.11.1026.jar" \
  "${MAVEN}/com/amazonaws/aws-java-sdk-bundle/1.11.1026/aws-java-sdk-bundle-1.11.1026.jar"

echo ""
echo "All downloads complete. You can now run: docker compose build"
