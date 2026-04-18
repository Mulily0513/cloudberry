# Prefetch bundle builder

本目录下的 `build-bundle.sh` 用于一次性准备 `datalake_fdw` CI 所需的全部 Apache 上游产物和多架构 Docker 镜像，打包成可上传到公司内部 MinIO 的 bundle。

**适用阶段**：Phase 1 —— "下载 + 打包镜像"，不动 CI / docker-compose / install_tools.sh。CI 端接入是 Phase 2。

## 一、前置条件

1. **网络**：只需要能访问 **一个** 地址 ——

   ```
   https://hashdata-download.obs.cn-north-4.myhuaweicloud.com/lightning-ci/packages/
   ```

   公司内部的华为云 OBS 镜像，里面放好了所有 Apache 原始包、MinIO 二进制、Ubuntu 基础镜像 tar。**不再需要** 访问 `archive.apache.org` / `dl.min.io` / Docker Hub。

   如果以后镜像迁移，`export DOWNLOAD_BASE_URL=https://new-host/path/` 覆盖即可（`download_cache/download.sh` 里读这个环境变量）。

2. **Docker + Buildx**：`docker buildx version` 有输出即可。

   - 推荐：**在原生架构机器上分别构建**（amd64 机器跑 `--arch amd64`，arm64 机器跑 `--arch arm64`），这样用 Docker daemon 内置 buildkit（`default` builder），不需要额外拉 `moby/buildkit` 或 `tonistiigi/binfmt`。
   - 不推荐：单机跨架构 QEMU 模拟。真要跑的话会自动落到 `docker-container` 驱动，需要能拉 `moby/buildkit:buildx-stable-1` 镜像。

3. **磁盘空间**：至少 **15 GB** 空闲（原始包 ~2 GB + ubuntu image tar ~60 MB × 2 + 构建层和 save tar ~8 GB）。

## 二、一键跑

```bash
cd contrib/datalake_fdw/automation/docker/singlecluster

# amd64 机器上
bash scripts/build-bundle.sh --arch amd64 --output-dir /tmp/bundle

# arm64 机器上（另外一台）
bash scripts/build-bundle.sh --arch arm64 --output-dir /tmp/bundle-arm64
```

首次全量（单架构原生）约 **10-20 分钟**，主要耗时是从 OBS 下原始包 + 镜像 build。

### 常用变体

```bash
# 已经下载过原始包，只跑 build + save
bash scripts/build-bundle.sh --arch amd64 --skip-download

# 只出原始包 + SHA256SUMS，不碰 docker
bash scripts/build-bundle.sh --skip-build

# 双架构（需要本机支持 QEMU，多数情况下不推荐）
bash scripts/build-bundle.sh --arch amd64,arm64

# 查看所有选项
bash scripts/build-bundle.sh --help
```

## 三、产物布局

脚本跑完后 `--output-dir/v1/` 下长这样（默认 `./prefetch-bundle/v1/`）：

```
v1/
├── README.md                 # 自动生成，带版本号、生成时间、使用说明
├── SHA256SUMS                # 所有文件的 sha256 清单（供下游校验）
├── common/                   # 架构无关：8 个文件，~1.2 GB
│   ├── hadoop-3.0.0.tar.gz
│   ├── apache-hive-3.0.0-bin.tar.gz
│   ├── spark-3.3.3-bin-hadoop3.tgz
│   ├── polaris-bin-1.3.0-incubating.tgz
│   ├── hudi-spark3-bundle_2.12-0.11.1.jar
│   ├── iceberg-spark-runtime-3.3_2.12-1.1.0.jar
│   ├── hadoop-aws-3.0.0.jar
│   └── aws-java-sdk-bundle-1.11.1026.jar
├── linux-amd64/              # amd64 专属：2 个二进制 + 2 个镜像 tar
│   ├── minio
│   ├── mc
│   ├── lakehouse-allinone-hd3.0.0-hive3.0.0-spark3.3.3-hudi0.11.1-iceberg1.1.0.tar
│   └── apache-polaris-1.3.0-incubating.tar
└── linux-arm64/              # 同上，arm64 版
    ├── minio
    ├── mc
    ├── lakehouse-allinone-...tar
    └── apache-polaris-...tar
```

预估总大小（双架构合并后）：**~10-12 GB**。

## 四、两台机合并 bundle

在 amd64 机器上完成 `--arch amd64` 之后，从 arm64 机器把 `linux-arm64/` 四个文件拿回来，再重算 SHA256SUMS：

```bash
# amd64 机器
scp -r arm64-host:/tmp/bundle-arm64/v1/linux-arm64 /tmp/bundle/v1/
cd /tmp/bundle/v1
find . -type f ! -name SHA256SUMS ! -name README.md -print0 \
  | LC_ALL=C sort -z | xargs -0 sha256sum > SHA256SUMS
```

之后 `/tmp/bundle/v1/` 就是完整的双架构 bundle，上传 MinIO 即可。

## 五、上传 MinIO 的建议目录

保持 `v1/` 顶层结构不变，整棵树传到 bucket：

```
s3://<bucket>/datalake-fdw/prefetch/v1/
    ├── SHA256SUMS
    ├── README.md
    ├── common/...
    ├── linux-amd64/...
    └── linux-arm64/...
```

建议用 `mc mirror` 或 `aws s3 sync` 一次性上传：

```bash
mc alias set internal http://<minio-host>:9000 <AK> <SK>
mc mirror --overwrite /tmp/bundle/v1/ internal/<bucket>/datalake-fdw/prefetch/v1/
```

## 六、验证产出

```bash
cd /tmp/bundle/v1

# 1) 完整性
sha256sum -c SHA256SUMS

# 2) 镜像可加载可启动（在本机架构对应的目录下做）
ARCH=$(dpkg --print-architecture)
docker load -i linux-${ARCH}/lakehouse-allinone-*.tar
docker load -i linux-${ARCH}/apache-polaris-*.tar
docker run --rm lakehouse-allinone:hd3.0.0-hive3.0.0-spark3.3.3-hudi0.11.1-iceberg1.1.0-linux-${ARCH} \
    /opt/hadoop/bin/hadoop version        # 应输出 Hadoop 3.0.0
```

## 七、脚本内部细节

- **版本源**：脚本从 `singlecluster/Dockerfile` 的 `ENV` 和 `thirdparty/apache-polaris/Dockerfile` 的 `ARG` 解析版本号。升级组件版本时只改 Dockerfile，脚本自动同步。
- **Polaris context 绕行**：Polaris 的 compose build context 是 `thirdparty/apache-polaris/`，访问不到上层 `download_cache/`。脚本构建前把 `polaris-bin-*.tgz` 复制到 `thirdparty/apache-polaris/polaris.tgz`，配合 Polaris Dockerfile 里的 "有缓存用缓存，没缓存 curl" 分支；构建后清理（`trap`）。
- **Ubuntu 基础镜像**：build 前自动 `docker load -i ubuntu-22.04-<arch>.tar`，避免 `FROM ubuntu:22.04` 去 Docker Hub。tar 不在就 warn，回落到 daemon 的 registry pull。
- **默认 builder**：本机单架构构建直接用 `default` builder，不需要 `moby/buildkit`、不需要 `--platform`。跨架构场景才会自动创建 `docker-container` 驱动的 builder（名字 `datalake-fdw-bundle`），不想留可以 `docker buildx rm datalake-fdw-bundle`。
