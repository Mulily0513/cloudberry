# datalake_fdw 与 Iceberg 完整使用指南

## 1. 概述

`datalake_fdw` 是 HashData Lightning 的数据湖访问扩展，提供两大能力：

- **FDW 外部表**：直接读写 S3/HDFS/Hive 上的 Parquet、ORC、Avro、Text、CSV 文件
- **Iceberg 表**：通过 Catalog + Volume + Iceberg Table 三层架构，支持事务性的 ACID 读写、Schema 演化、快照隔离和 VACUUM 压缩

```
                    datalake_fdw
                    /          \
           FDW 外部表         Iceberg 表
         (只读/追加写)       (完整 ACID)
          |                    |
    +-----------+        +-----+------+
    | 协议+格式  |        | Catalog    Volume |
    | S3/HDFS   |        | (元数据)  (存储)   |
    | ORC/Parquet|        +-----+------+
    | Avro/CSV  |              |
    +-----------+         Iceberg Table
```

---

## 2. 安装

```sql
CREATE EXTENSION IF NOT EXISTS datalake_fdw;
CREATE EXTENSION IF NOT EXISTS hive_connector;   -- Hive 集成（可选）
```

安装 `datalake_fdw` 时自动注册以下 FDW：

| FDW 名称 | 用途 |
|----------|------|
| `datalake_fdw` | 通用外部表（S3/HDFS 文件） |
| `iceberg_catalog_fdw` | Iceberg Catalog 管理 |
| `iceberg_volume_fdw` | Iceberg Volume（存储层）管理 |

---

## 3. FDW 外部表（非 Iceberg）

### 3.1 S3 外部表

```sql
-- 创建 S3 Server
CREATE SERVER s3_server
    FOREIGN DATA WRAPPER datalake_fdw
    OPTIONS (
        host 'lakehouse:9100',
        protocol 's3',
        isvirtual 'false',        -- MinIO 需要 false（path-style）
        ishttps 'false'
    );

CREATE USER MAPPING FOR gpadmin
    SERVER s3_server
    OPTIONS (user 'gpadmin', accesskey 'admin', secretkey 'password');

-- 创建外部表（Parquet 格式）
CREATE FOREIGN TABLE s3_sales (id int, name text, amount numeric(10,2))
    SERVER s3_server
    OPTIONS (filePath '/bucket/sales/parquet/', format 'parquet');

-- 查询
SELECT * FROM s3_sales WHERE id > 100;
```

### 3.2 HDFS 外部表

```sql
CREATE SERVER hdfs_server
    FOREIGN DATA WRAPPER datalake_fdw
    OPTIONS (
        protocol 'hdfs',
        hdfs_namenodes 'namenode-host',
        hdfs_port '8020',
        hdfs_auth_method 'simple',
        hadoop_rpc_protection 'authentication'
    );

CREATE USER MAPPING FOR gpadmin
    SERVER hdfs_server
    OPTIONS (user 'gpadmin');

-- Text 格式
CREATE FOREIGN TABLE hdfs_logs (line text)
    SERVER hdfs_server
    OPTIONS (filePath 'hdfs://namenode:8020/logs/2024/', format 'text');

-- ORC 格式
CREATE FOREIGN TABLE hdfs_events (id int, event_type text, ts timestamp)
    SERVER hdfs_server
    OPTIONS (filePath 'hdfs://namenode:8020/events/', format 'orc');
```

### 3.3 支持的文件格式

| 格式 | 读 | 写 | OPTIONS 示例 |
|------|:--:|:--:|-------------|
| Parquet | ✓ | ✓ | `format 'parquet'` |
| ORC | ✓ | ✓ | `format 'orc'` |
| Avro | ✓ | - | `format 'avro'` |
| Text | ✓ | ✓ | `format 'text'` |
| CSV | ✓ | ✓ | `format 'csv', delimiter ',', quote '"'` |

外部表通用 OPTIONS：

| 选项 | 说明 |
|------|------|
| `filePath` | 数据文件路径（支持目录） |
| `format` | 文件格式 |
| `delimiter` | 字段分隔符（text/csv） |
| `quote` | 引号字符（csv） |

### 3.4 Hive 表同步

通过 `hive_connector` 扩展从 Hive Metastore 同步表定义：

```sql
-- 同步单张 Hive 表到本地 schema
SELECT sync_hive_table(
    'hive_cluster',      -- Hive 集群配置段名
    'default',           -- Hive 数据库
    'orders',            -- Hive 表名
    'paa_cluster',       -- HDFS 配置段名
    'myschema.orders',   -- 本地目标表（schema.table）
    'hive_server'        -- FDW Server 名
);

-- 同步整个 Hive 数据库
SELECT sync_hive_database(
    'hive_cluster', 'default', 'paa_cluster',
    'hive_schema', 'hive_server'
);
```

同步后可直接 `SELECT * FROM myschema.orders` 查询 Hive 数据。

---

## 4. Iceberg 表

### 4.1 架构

Iceberg 表采用三层架构，每一层独立配置、可组合：

```
Catalog (元数据管理)     支持: Polaris / Hive / Builtin
    +
Volume  (存储访问)       支持: S3 / HDFS
    +
Iceberg Table (数据表)   支持: CREATE / INSERT / UPDATE / DELETE / VACUUM
```

### 4.2 Catalog 配置

#### Builtin Catalog（内建，无需外部服务）

```sql
CREATE SERVER my_cat_srv FOREIGN DATA WRAPPER iceberg_catalog_fdw;
CREATE USER MAPPING FOR current_user SERVER my_cat_srv;
CREATE FOREIGN CATALOG my_catalog SERVER my_cat_srv;
```

#### Hive Catalog

```sql
CREATE SERVER hive_cat_srv FOREIGN DATA WRAPPER iceberg_catalog_fdw
OPTIONS (
    type 'hive',
    url 'thrift://hive-metastore:9083'
);
CREATE USER MAPPING FOR current_user SERVER hive_cat_srv;
CREATE FOREIGN CATALOG hive_catalog SERVER hive_cat_srv
OPTIONS (
    catalog_name 'hive_location',
    default_namespace 'default',
    warehouse_location_prefix 's3://warehouse/hive/'
);
```

Kerberos 认证时需额外 User Mapping 选项：

| 选项 | 说明 |
|------|------|
| `krb_service_principal` | Hive 服务端 principal |
| `krb_client_principal` | 客户端 principal |
| `krb_client_keytab` | Keytab 文件路径 |

#### Polaris Catalog（REST API）

```sql
CREATE SERVER polaris_cat_srv FOREIGN DATA WRAPPER iceberg_catalog_fdw
OPTIONS (
    type 'polaris',
    url 'http://polaris:8181/api/catalog'
);
CREATE USER MAPPING FOR current_user SERVER polaris_cat_srv
OPTIONS (
    client_id 'my_client_id',
    client_secret 'my_client_secret',
    scope 'PRINCIPAL_ROLE:ALL'
);
CREATE FOREIGN CATALOG polaris_catalog SERVER polaris_cat_srv
OPTIONS (
    catalog_name 'production',
    default_namespace 'public',
    enable_metadata_cache 'true',
    metadata_cache_ttl '300'
);
```

Catalog 选项汇总：

| 选项 | 适用 | 说明 |
|------|------|------|
| `catalog_name` | Hive/Polaris | 远端 catalog 名称 |
| `default_namespace` | 全部 | 默认命名空间 |
| `enable_metadata_cache` | 全部 | 启用元数据缓存 |
| `metadata_cache_ttl` | 全部 | 缓存 TTL（秒） |
| `auto_refresh_metadata` | 全部 | 自动刷新 |
| `warehouse_location_prefix` | Hive/Polaris | 仓库路径前缀 |
| `allow_exists` | Polaris | 远端已存在时不报错（默认 true） |

### 4.3 Volume 配置

#### S3 / MinIO Volume

```sql
CREATE SERVER s3_vol_srv FOREIGN DATA WRAPPER iceberg_volume_fdw
OPTIONS (
    type 's3',
    endpoint 'http://minio:9000',
    region 'us-east-1',
    bucket_name 'warehouse',
    path_style_access 'true'        -- MinIO 必须为 true
);
CREATE USER MAPPING FOR current_user SERVER s3_vol_srv
OPTIONS (access_key_id 'AKIAEXAMPLE', secret_access_key 'SECRET');
CREATE FOREIGN VOLUME my_volume SERVER s3_vol_srv
OPTIONS (base_path '/warehouse/', allow_writes 'true');
```

#### HDFS Volume

```sql
-- 单节点 + Simple 认证
CREATE SERVER hdfs_vol_srv FOREIGN DATA WRAPPER iceberg_volume_fdw
OPTIONS (
    type 'hdfs',
    hdfs_namenodes '192.168.1.10:9000',
    hdfs_auth_method 'simple',
    hadoop_rpc_protection 'authentication'
);
CREATE USER MAPPING FOR current_user SERVER hdfs_vol_srv
OPTIONS (username 'gpadmin');
CREATE FOREIGN VOLUME hdfs_vol SERVER hdfs_vol_srv
OPTIONS (base_path '/iceberg-warehouse/', allow_writes 'true');

-- HA 模式 + Kerberos
CREATE SERVER hdfs_ha_vol_srv FOREIGN DATA WRAPPER iceberg_volume_fdw
OPTIONS (
    type 'hdfs',
    hdfs_namenodes 'mycluster',
    hdfs_auth_method 'kerberos',
    hadoop_rpc_protection 'privacy',
    is_ha_supported 'true',
    dfs_nameservices 'mycluster',
    dfs_ha_namenodes 'nn1,nn2',
    dfs_namenode_rpc_address '192.168.1.10:9000,192.168.1.11:9000',
    dfs_client_failover_proxy_provider
        'org.apache.hadoop.hdfs.server.namenode.ha.ConfiguredFailoverProxyProvider'
);
CREATE USER MAPPING FOR current_user SERVER hdfs_ha_vol_srv
OPTIONS (
    username 'gpadmin',
    krb_principal 'gpadmin/master@REALM.COM',
    krb_principal_keytab '/home/gpadmin/gpadmin.keytab'
);
```

Volume 选项汇总：

| 选项 | 说明 |
|------|------|
| `base_path` | 存储基础路径 |
| `allow_writes` | 允许写操作（默认 false） |
| `enable_caching` | 启用缓存（默认 false） |

### 4.4 创建 Iceberg 表

```sql
-- 设置默认 Catalog 和 Volume（避免每条 DDL 都指定）
SET iceberg_default_catalog = 'my_catalog';
SET iceberg_default_volume  = 'my_volume';

-- 基本建表
CREATE ICEBERG TABLE orders (
    id         BIGINT NOT NULL,
    product    VARCHAR(100),
    amount     DECIMAL(15,2) NOT NULL,
    order_date DATE NOT NULL,
    CONSTRAINT pk_orders PRIMARY KEY (id, order_date)
);

-- 显式指定 Catalog + Volume + 选项
CREATE ICEBERG TABLE sales (
    id BIGINT, region TEXT, amount NUMERIC(12,2)
)
CATALOG hive_catalog
VOLUME s3_volume
OPTIONS (
    namespace 'analytics',
    table_name 'sales_2024',
    base_location '/data/sales/'
);
```

**CATALOG / VOLUME 指定规则**：
- 可省略 → 使用 `iceberg_default_catalog` / `iceberg_default_volume` GUC
- Polaris Catalog 可不指定 Volume（存储配置由 Polaris 服务端下发）
- Builtin / Hive Catalog 必须指定 Volume

**数据路径组成**（新表）：
```
protocol://bucket/base_path/base_location/namespace/tablename
```

### 4.5 支持的数据类型

| PostgreSQL 类型 | Iceberg 类型 | 说明 |
|----------------|-------------|------|
| `boolean` | boolean | |
| `smallint` | int | |
| `integer` | int | |
| `bigint` | long | |
| `real` | float | |
| `double precision` | double | |
| `decimal(p,s)` / `numeric(p,s)` | decimal(p,s) | **必须指定精度** |
| `text` / `varchar(n)` | string | |
| `date` | date | |
| `timestamp` | timestamp | 无时区 |
| `timestamptz` | timestamptz | 带时区 |
| `bytea` | binary | |

> **重要**：`numeric` 不指定精度时，Iceberg（Parquet 编码）会报错。务必写成 `numeric(p,s)`。

### 4.6 DML 操作

```sql
-- INSERT
INSERT INTO orders VALUES (1, 'Widget', 99.99, '2024-01-15');
INSERT INTO orders SELECT * FROM staging_orders;

-- UPDATE
UPDATE orders SET amount = amount * 1.1 WHERE region = 'US-West';

-- DELETE
DELETE FROM orders WHERE order_date < '2023-01-01';

-- TRUNCATE
TRUNCATE orders;

-- SELECT（支持谓词下推 + 列裁剪）
SELECT region, SUM(amount) FROM orders
WHERE order_date >= '2024-01-01'
GROUP BY region;
```

### 4.7 Schema 演化

```sql
-- 添加列（已有行该列为 NULL）
ALTER TABLE orders ADD COLUMN note TEXT;

-- 新数据可填充新列
INSERT INTO orders VALUES (100, 'New', 50.00, '2024-06-01', 'rush order');

-- 单次 SELECT 同时读取旧文件（note=NULL）和新文件（note 有值）
SELECT * FROM orders;
```

混合 Schema 文件读取依赖 Iceberg 的 **field-id 映射**。新增列后旧数据文件中该列自动填 NULL，无需重写。

支持的 ALTER 操作：
- `ADD COLUMN` — 新列默认 NULL
- `DROP COLUMN` — 新快照不再包含该列
- `RENAME COLUMN`
- 类型提升（如 `int` → `bigint`）

### 4.8 VACUUM（压缩）

Iceberg 表通过 VACUUM 触发数据文件压缩（compaction）：

```sql
-- 基本用法
VACUUM orders;

-- 调整压缩参数
SET datalake.iceberg_vacuum_compact_min_input_files = 5;    -- 最少 5 个文件才触发合并
SET datalake.iceberg_vacuum_rewrite_target_file_size_mb = 256; -- 目标文件大小 256MB
VACUUM orders;
```

VACUUM 效果：
- 合并多个小文件为大文件
- 清理 position delete 文件
- 优化后续 scan 性能（减少文件 IO 次数）

> **注意**：VACUUM 不能在事务块或 PL/pgSQL 函数内执行。

### 4.9 自动 VACUUM

```sql
-- 启用自动 VACUUM（需 superuser，修改后需 reload）
ALTER SYSTEM SET datalake.iceberg_autovacuum = on;
ALTER SYSTEM SET datalake.iceberg_autovacuum_naptime = 600;  -- 每 10 分钟检查
SELECT pg_reload_conf();
```

---

## 5. 谓词下推与列裁剪

`datalake_fdw` 自动将 WHERE 条件下推到存储层，减少网络传输和 IO：

```sql
-- 下推生效的操作符：=, >, <, >=, <=, !=, IS NULL, IS NOT NULL, LIKE, IN
SELECT * FROM orders WHERE id = 12345;       -- 等值下推
SELECT * FROM orders WHERE amount > 100.00;  -- 范围下推
SELECT * FROM orders WHERE region IN ('US', 'EU');  -- IN 下推

-- 列裁剪：只读取查询需要的列
SELECT id, amount FROM orders;  -- 只读 id, amount 两列的数据
```

验证下推效果：
```sql
EXPLAIN SELECT * FROM orders WHERE id = 12345;
-- 计划中应显示 Filter 被推到外部表层

-- 对比关闭下推
SET datalake.disable_filter_pushdown = on;
EXPLAIN SELECT * FROM orders WHERE id = 12345;
-- Filter 在上层执行，scan 返回全部行
RESET datalake.disable_filter_pushdown;
```

---

## 6. GUC 参考

### 会话级（SET 可改）

| GUC | 默认值 | 说明 |
|-----|--------|------|
| `iceberg_default_catalog` | `''` | 默认 Catalog 名称 |
| `iceberg_default_volume` | `''` | 默认 Volume 名称 |
| `datalake.disable_filter_pushdown` | `off` | 关闭谓词下推 |
| `datalake.disable_cache_file` | `off` | 关闭文件缓存 |
| `datalake.enable_iceberg_fragment_cache` | `on` | Iceberg fragment 缓存 |
| `datalake.iceberg_vacuum_compact_min_input_files` | `10` | VACUUM 最少输入文件数 |
| `datalake.iceberg_vacuum_rewrite_target_file_size_mb` | `512` | VACUUM 目标文件大小（MB） |
| `datalake.iceberg_postion_deletes_threshold` | `100000` | Position delete 阈值 |
| `datalake.external_table_limit_segment_num` | `0` | 限制参与扫描的 segment 数（0=不限） |
| `datalake.external_table_debug` | `off` | 调试模式 |
| `datalake.skip_create_polaris_catalog` | `off` | 跳过 Polaris catalog 自动创建 |

### Superuser 级

| GUC | 默认值 | 说明 |
|-----|--------|------|
| `datalake.iceberg_max_snapshot_age` | 5 天 | 快照最大保留时间 |
| `datalake.iceberg_max_file_removals_per_vacuum` | `100000` | 单次 VACUUM 最大删除文件数 |
| `datalake.iceberg_max_compactions_per_vacuum` | `100` | 单次 VACUUM 最大压缩操作数 |

### 服务重启级

| GUC | 默认值 | 说明 |
|-----|--------|------|
| `datalake.iceberg_autovacuum` | `off` | 启用自动 VACUUM |
| `datalake.iceberg_autovacuum_naptime` | `600` | 自动 VACUUM 间隔（秒） |
| `datalake.iceberg_log_autovacuum_min_duration` | `-1` | 记录超过此时长的自动 VACUUM（ms，-1=不记录） |

---

## 7. 端到端示例

### 7.1 S3 + Builtin Catalog（最简配置）

```sql
-- 1. 扩展
CREATE EXTENSION IF NOT EXISTS datalake_fdw;

-- 2. Catalog（内建，无外部依赖）
CREATE SERVER cat FOREIGN DATA WRAPPER iceberg_catalog_fdw;
CREATE USER MAPPING FOR current_user SERVER cat;
CREATE FOREIGN CATALOG my_cat SERVER cat;
SET iceberg_default_catalog = 'my_cat';

-- 3. Volume（S3/MinIO）
CREATE SERVER vol FOREIGN DATA WRAPPER iceberg_volume_fdw
OPTIONS (type 's3', endpoint 'http://minio:9000', region 'us-east-1',
         bucket_name 'warehouse', path_style_access 'true');
CREATE USER MAPPING FOR current_user SERVER vol
OPTIONS (access_key_id 'admin', secret_access_key 'password');
CREATE FOREIGN VOLUME my_vol SERVER vol OPTIONS (base_path '/data/');
SET iceberg_default_volume = 'my_vol';

-- 4. 建表 + 使用
CREATE ICEBERG TABLE users (id int, name text, score numeric(10,2));
INSERT INTO users VALUES (1, 'Alice', 95.5), (2, 'Bob', 87.0);
SELECT * FROM users WHERE score > 90;
UPDATE users SET score = 96.0 WHERE id = 1;
VACUUM users;
DROP TABLE users;

-- 5. 清理
DROP VOLUME my_vol;
DROP USER MAPPING FOR current_user SERVER vol;
DROP SERVER vol;
DROP CATALOG my_cat;
DROP USER MAPPING FOR current_user SERVER cat;
DROP SERVER cat;
```

### 7.2 Hive 外部表查询

```sql
CREATE EXTENSION IF NOT EXISTS datalake_fdw;
CREATE EXTENSION IF NOT EXISTS hive_connector;

-- Hive Server（用于 sync_hive_table）
CREATE SERVER hive_server
    FOREIGN DATA WRAPPER datalake_fdw
    OPTIONS (protocol 'hdfs', hdfs_namenodes 'namenode',
             hdfs_port '8020', hdfs_auth_method 'simple',
             hadoop_rpc_protection 'authentication');
CREATE USER MAPPING FOR gpadmin SERVER hive_server OPTIONS (user 'gpadmin');

-- 从 Hive Metastore 同步表
SELECT sync_hive_table('hive_cluster', 'default', 'orders',
                       'paa_cluster', 'hive.orders', 'hive_server');

-- 直接查询
SELECT * FROM hive.orders WHERE order_date > '2024-01-01';
```

---

## 8. 对象清理顺序

创建和删除需遵循依赖顺序：

```
创建：Extension → Server → User Mapping → Catalog/Volume → Iceberg Table
删除：Iceberg Table → Volume → Catalog → User Mapping → Server
```

```sql
-- 删除示例（从里到外）
DROP TABLE my_iceberg_table;
DROP VOLUME my_volume;
DROP USER MAPPING FOR current_user SERVER vol_server;
DROP SERVER vol_server;
DROP CATALOG my_catalog;
DROP USER MAPPING FOR current_user SERVER cat_server;
DROP SERVER cat_server;
```

或使用 `CASCADE`：
```sql
DROP SERVER cat_server CASCADE;  -- 级联删除所有依赖对象
```

---

## 9. 常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| `numeric` 列建表报错 | Parquet 编码要求精度 | 改为 `numeric(p,s)` |
| VACUUM 在函数内报错 | PG 限制 | 在函数外执行 VACUUM |
| Hive Catalog 读回为空 | Hive Metastore 配置 | 检查 `url` 格式：`thrift://host:port` |
| 下推不生效 | GUC 被关闭 | `RESET datalake.disable_filter_pushdown` |
| Polaris 401 错误 | OAuth 认证失败 | 检查 `client_id` / `client_secret` / `scope` |
| 外部表文件找不到 | `filePath` 路径错误 | 确认包含 bucket 前缀或绝对路径 |
