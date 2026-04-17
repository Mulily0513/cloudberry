# Lakehouse Single Cluster

This directory contains a self-contained Docker image that bundles the main data lakehouse components for development or demo use:

- Hadoop 3.x NameNode and DataNode
- Hive 3.x Metastore + HiveServer2
- Spark 3.3 master, worker, and history server with Iceberg and Hudi bundles
- MinIO object storage (S3-compatible) that backs the Hive/Spark warehouse

## Prerequisites

- Docker Engine 20.10+ and Docker Compose v2
- The external Docker network `share-enterprise-ci` (create it if it does not exist: `docker network create share-enterprise-ci`)
- At least 8 GB RAM available for the container

## Quick Start

1. From `lakehouse/singlecluster`, build the image and launch the container:
   ```bash
   docker compose build
   docker compose up -d
   ```
2. Follow the bootstrap log to ensure HDFS, Hive, Spark, and MinIO are started:
   ```bash
   docker compose logs -f lakehouse
   ```
3. When the log shows `[ready] services started`, the stack is available. Data and logs are stored under `./data/`.

The container entrypoint automatically runs `/opt/start-all.sh`, which configures Hadoop/Hive/Spark, formats HDFS if needed, brings up PostgreSQL for the metastore, and seeds MinIO with the `warehouse` bucket.

## Services and Ports

| Service            | URL / Port | Notes                                |
|--------------------|------------|--------------------------------------|
| HDFS NameNode UI   | `http://localhost:9870` | View HDFS status                |
| Hive Metastore     | `thrift://localhost:9083` | Used internally by Hive/Spark |
| HiveServer2        | `jdbc:hive2://localhost:10000` | Beeline / JDBC access    |
| Spark Master UI    | `http://localhost:8080` | Job overview                    |
| Spark History UI   | `http://localhost:18080` | Completed jobs                  |
| MinIO API          | `http://localhost:9100` | S3 endpoint (`admin/password`)  |
| MinIO Console      | `http://localhost:9200` | Web console                     |

## Useful Commands

- Open a Hive shell via Beeline:
  ```bash
  docker compose exec lakehouse beeline -u jdbc:hive2://localhost:10000 -n hive
  ```
- Run Spark SQL:
  ```bash
  docker compose exec lakehouse spark-sql
  ```
- Interact with MinIO using the bundled `mc` client (alias already configured as `local`):
  ```bash
  docker compose exec lakehouse mc ls local/warehouse
  ```

## Polaris CLI (quick use)

- Install jq inside the container (one-time):
  ```bash
  docker compose exec lakehouse /workspace/lakehouse/singlecluster/scripts/install_jq.sh
  ```
- List catalogs via Polaris CLI script:
  ```bash
  docker compose exec lakehouse /workspace/lakehouse/singlecluster/scripts/polaris_cli.sh list_catalogs
  ```
  This script calls Polaris REST endpoints using the credentials/endpoints defined in the script.
- Create a catalog (example):
  ```bash
  docker compose exec lakehouse /workspace/lakehouse/singlecluster/scripts/polaris_cli.sh create_catalog polaris s3://warehouse
  ```
  Arguments: `<catalog_name> <default_base_location>`
- Create a namespace (example):
  ```bash
  docker compose exec lakehouse /workspace/lakehouse/singlecluster/scripts/polaris_cli.sh create_namespace polaris default
  ```
  Arguments: `<catalog_name> <namespace>`

## Install dependencies inside container

- `scripts/install_jq.sh` installs `jq` for JSON parsing (required by `polaris_cli.sh`).
- Run it once after the container is up:
  ```bash
  docker compose exec lakehouse /workspace/lakehouse/singlecluster/scripts/install_jq.sh
  ```

## Shutdown

Stop and remove the container, keeping the persistent volumes under `data/`:
```bash
docker compose down
```
If you want a clean slate, remove the data directories before starting again.


