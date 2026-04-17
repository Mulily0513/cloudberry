# syntax=docker/dockerfile:1
FROM ubuntu:22.04

ARG DEBIAN_FRONTEND=noninteractive
# TARGETARCH is auto-populated by Docker Buildx (amd64 / arm64); default to host arch
# when building with legacy docker build.
ARG TARGETARCH

ENV HADOOP_VERSION=3.0.0 \
    HIVE_VERSION=3.0.0 \
    SPARK_VERSION=3.3.3 \
    HUDI_VERSION=0.11.1 \
    ICEBERG_VERSION=1.1.0 \
    HADOOP_HOME=/opt/hadoop \
    HIVE_HOME=/opt/hive \
    SPARK_HOME=/opt/spark

# Base tools (must run before JAVA_HOME resolution so openjdk is installed)
RUN apt-get update && apt-get install -y --no-install-recommends \
    bash ca-certificates curl wget procps netcat-openbsd lsof python3 \
    postgresql postgresql-contrib sudo gnupg openjdk-8-jdk-headless \
 && rm -rf /var/lib/apt/lists/*

# Resolve JAVA_HOME from the Debian architecture (amd64 / arm64) so the image
# works on both x86_64 and aarch64 hosts.
ENV JAVA_HOME=/usr/lib/jvm/java-8-openjdk-${TARGETARCH}
ENV PATH=$PATH:$JAVA_HOME/bin:/opt/hadoop/bin:/opt/hive/bin:/opt/spark/bin

# MinIO — use TARGETARCH so arm64 runners download the arm64 binary.
RUN --mount=type=bind,source=download_cache,target=/tmp/cache \
    ARCH="${TARGETARCH:-$(dpkg --print-architecture)}"; \
    if [ -f /tmp/cache/minio ]; then echo "[cache] minio"; cp /tmp/cache/minio /usr/local/bin/minio; \
    else curl -fSL "https://dl.min.io/server/minio/release/linux-${ARCH}/minio" -o /usr/local/bin/minio; fi \
 && chmod +x /usr/local/bin/minio \
 && if [ -f /tmp/cache/mc ]; then echo "[cache] mc"; cp /tmp/cache/mc /usr/local/bin/mc; \
    else curl -fSL "https://dl.min.io/client/mc/release/linux-${ARCH}/mc" -o /usr/local/bin/mc; fi \
 && chmod +x /usr/local/bin/mc

# Hadoop
RUN --mount=type=bind,source=download_cache,target=/tmp/cache \
    if [ -f /tmp/cache/hadoop-${HADOOP_VERSION}.tar.gz ]; then \
      echo "[cache] hadoop-${HADOOP_VERSION}.tar.gz"; \
      tar -xzf /tmp/cache/hadoop-${HADOOP_VERSION}.tar.gz -C /opt; \
    else \
      curl -fSL https://archive.apache.org/dist/hadoop/common/hadoop-${HADOOP_VERSION}/hadoop-${HADOOP_VERSION}.tar.gz \
        | tar -xz -C /opt; \
    fi \
 && ln -s /opt/hadoop-${HADOOP_VERSION} ${HADOOP_HOME}

# Hive
RUN --mount=type=bind,source=download_cache,target=/tmp/cache \
    if [ -f /tmp/cache/apache-hive-${HIVE_VERSION}-bin.tar.gz ]; then \
      echo "[cache] apache-hive-${HIVE_VERSION}-bin.tar.gz"; \
      tar -xzf /tmp/cache/apache-hive-${HIVE_VERSION}-bin.tar.gz -C /opt; \
    else \
      curl -fSL https://archive.apache.org/dist/hive/hive-${HIVE_VERSION}/apache-hive-${HIVE_VERSION}-bin.tar.gz \
        | tar -xz -C /opt; \
    fi \
 && ln -s /opt/apache-hive-${HIVE_VERSION}-bin ${HIVE_HOME}

# Spark
RUN --mount=type=bind,source=download_cache,target=/tmp/cache \
    if [ -f /tmp/cache/spark-${SPARK_VERSION}-bin-hadoop3.tgz ]; then \
      echo "[cache] spark-${SPARK_VERSION}-bin-hadoop3.tgz"; \
      tar -xzf /tmp/cache/spark-${SPARK_VERSION}-bin-hadoop3.tgz -C /opt; \
    else \
      curl -fSL https://archive.apache.org/dist/spark/spark-${SPARK_VERSION}/spark-${SPARK_VERSION}-bin-hadoop3.tgz \
        | tar -xz -C /opt; \
    fi \
 && ln -s /opt/spark-${SPARK_VERSION}-bin-hadoop3 ${SPARK_HOME}

# Dependency JARs: Hudi / Iceberg / S3A
RUN --mount=type=bind,source=download_cache,target=/tmp/cache \
    mkdir -p /opt/jars && cd /opt/jars \
 && { [ -f /tmp/cache/hudi-spark3-bundle_2.12-${HUDI_VERSION}.jar ] \
      && echo "[cache] hudi jar" && cp /tmp/cache/hudi-spark3-bundle_2.12-${HUDI_VERSION}.jar . \
      || curl -fSLO https://maven.aliyun.com/repository/central/org/apache/hudi/hudi-spark3-bundle_2.12/${HUDI_VERSION}/hudi-spark3-bundle_2.12-${HUDI_VERSION}.jar; } \
 && { [ -f /tmp/cache/iceberg-spark-runtime-3.3_2.12-${ICEBERG_VERSION}.jar ] \
      && echo "[cache] iceberg jar" && cp /tmp/cache/iceberg-spark-runtime-3.3_2.12-${ICEBERG_VERSION}.jar . \
      || curl -fSLO https://maven.aliyun.com/repository/central/org/apache/iceberg/iceberg-spark-runtime-3.3_2.12/${ICEBERG_VERSION}/iceberg-spark-runtime-3.3_2.12-${ICEBERG_VERSION}.jar; } \
 && { [ -f /tmp/cache/hadoop-aws-${HADOOP_VERSION}.jar ] \
      && echo "[cache] hadoop-aws jar" && cp /tmp/cache/hadoop-aws-${HADOOP_VERSION}.jar . \
      || curl -fSLO https://maven.aliyun.com/repository/central/org/apache/hadoop/hadoop-aws/${HADOOP_VERSION}/hadoop-aws-${HADOOP_VERSION}.jar; } \
 && { [ -f /tmp/cache/aws-java-sdk-bundle-1.11.1026.jar ] \
      && echo "[cache] aws-sdk jar" && cp /tmp/cache/aws-java-sdk-bundle-1.11.1026.jar . \
      || curl -fSLO https://maven.aliyun.com/repository/central/com/amazonaws/aws-java-sdk-bundle/1.11.1026/aws-java-sdk-bundle-1.11.1026.jar; }

# Startup scripts
COPY scripts/start-all.sh /opt/start-all.sh
COPY scripts/init-metastore.sh /opt/init-metastore.sh
RUN chmod +x /opt/start-all.sh /opt/init-metastore.sh

# Data directories
RUN mkdir -p /data/hdfs/nn /data/hdfs/dn /data/metastore-db /data/warehouse /var/log/lakehouse

EXPOSE 9870 9864 9083 10000 9100 9200 7077 8080 18080 8181 5432
ENTRYPOINT ["/opt/start-all.sh"]
