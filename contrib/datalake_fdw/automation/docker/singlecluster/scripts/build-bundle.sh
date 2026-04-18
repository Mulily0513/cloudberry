#!/usr/bin/env bash
#
# build-bundle.sh — produce a self-contained offline bundle of the Apache
# packages and the two Docker images (lakehouse-allinone + apache-polaris)
# needed by contrib/datalake_fdw/automation CI.
#
# The bundle is designed to be uploaded to the company-internal MinIO so CI
# runners (both amd64 and arm64) can fetch everything without going to the
# public internet.
#
# Usage:
#   bash scripts/build-bundle.sh [options]
#
# Options:
#   --output-dir DIR    Output root (default: ./prefetch-bundle). Final layout
#                       is written to <DIR>/v1/{common,linux-amd64,linux-arm64}.
#   --arch LIST         Comma-separated archs to build images for.
#                       One of: amd64 | arm64 | amd64,arm64 (default: amd64,arm64).
#   --skip-download     Skip running download_cache/download.sh (assumes the
#                       tarballs/jars are already present).
#   --skip-build        Skip the docker buildx + save steps (only gather raw
#                       packages + SHA256SUMS).
#   --skip-checksum     Don't regenerate SHA256SUMS.
#   --keep-tags         Keep the locally-loaded per-arch docker tags after save
#                       (default: remove them to avoid cluttering local daemon).
#   -h | --help         Show this help.
#
# Prerequisites:
#   - docker + buildx (`docker buildx version` should print something).
#   - For cross-arch builds on a single host, QEMU binfmt must be registered:
#         docker run --privileged --rm tonistiigi/binfmt --install all
#   - Public-internet access for the download step (upstream URLs in
#     download_cache/download.sh).
#
set -euo pipefail

# -------------------------------------------------------------------------
# Locate ourselves
# -------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SC_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"              # .../singlecluster
CACHE_DIR="${SC_DIR}/download_cache"
POLARIS_DIR="${SC_DIR}/thirdparty/apache-polaris"

# -------------------------------------------------------------------------
# Defaults and argument parsing
# -------------------------------------------------------------------------
OUTPUT_DIR="${SC_DIR}/prefetch-bundle"
ARCH_LIST="amd64,arm64"
SKIP_DOWNLOAD=0
SKIP_BUILD=0
SKIP_CHECKSUM=0
KEEP_TAGS=0

while [ $# -gt 0 ]; do
  case "$1" in
    --output-dir)    OUTPUT_DIR="$2"; shift 2 ;;
    --output-dir=*)  OUTPUT_DIR="${1#--output-dir=}"; shift ;;
    --arch)          ARCH_LIST="$2"; shift 2 ;;
    --arch=*)        ARCH_LIST="${1#--arch=}"; shift ;;
    --skip-download) SKIP_DOWNLOAD=1; shift ;;
    --skip-build)    SKIP_BUILD=1; shift ;;
    --skip-checksum) SKIP_CHECKSUM=1; shift ;;
    --keep-tags)     KEEP_TAGS=1; shift ;;
    -h|--help)       sed -n '2,40p' "$0"; exit 0 ;;
    *) echo "[error] unknown arg: $1" >&2; exit 2 ;;
  esac
done

IFS=',' read -r -a ARCHES <<< "$ARCH_LIST"
for a in "${ARCHES[@]}"; do
  case "$a" in
    amd64|arm64) ;;
    *) echo "[error] unsupported arch: $a (use amd64 or arm64)" >&2; exit 2 ;;
  esac
done

# -------------------------------------------------------------------------
# Resolve versions from the lakehouse Dockerfile so the bundle tag stays in
# sync with whatever the image is actually built with.
# -------------------------------------------------------------------------
# The Dockerfile uses a multi-line `ENV KEY=V \` block, so we can't rely on
# per-line parsing. Just grep every KEY=VALUE token and take the first match.
dockerfile_env() {
  local key="$1"
  grep -oE "\\b${key}=[^[:space:]\\\\]+" "${SC_DIR}/Dockerfile" \
    | head -1 | cut -d= -f2-
}

HADOOP_VERSION="$(dockerfile_env HADOOP_VERSION)"
HIVE_VERSION="$(dockerfile_env HIVE_VERSION)"
SPARK_VERSION="$(dockerfile_env SPARK_VERSION)"
HUDI_VERSION="$(dockerfile_env HUDI_VERSION)"
ICEBERG_VERSION="$(dockerfile_env ICEBERG_VERSION)"

# Polaris version comes from the Polaris Dockerfile's ARG default.
POLARIS_VERSION="$(awk -F= '/^ARG[ \t]+POLARIS_VERSION=/ { print $2; exit }' \
  "${POLARIS_DIR}/Dockerfile")"
POLARIS_VERSION="${POLARIS_VERSION:-1.3.0-incubating}"

AWS_SDK_VERSION="1.11.1026"   # kept in sync with download.sh default
UBUNTU_VERSION="${UBUNTU_VERSION:-22.04}"

LAKEHOUSE_TAG="hd${HADOOP_VERSION}-hive${HIVE_VERSION}-spark${SPARK_VERSION}-hudi${HUDI_VERSION}-iceberg${ICEBERG_VERSION}"
POLARIS_TAG="${POLARIS_VERSION}"

BUNDLE_DIR="${OUTPUT_DIR}/v1"

cat <<EOF
================================================================
datalake_fdw prefetch-bundle builder
----------------------------------------------------------------
  singlecluster dir : ${SC_DIR}
  download cache    : ${CACHE_DIR}
  output dir        : ${BUNDLE_DIR}
  archs             : ${ARCHES[*]}
  hadoop            : ${HADOOP_VERSION}
  hive              : ${HIVE_VERSION}
  spark             : ${SPARK_VERSION}
  hudi              : ${HUDI_VERSION}
  iceberg           : ${ICEBERG_VERSION}
  polaris           : ${POLARIS_VERSION}
  lakehouse tag     : lakehouse-allinone:${LAKEHOUSE_TAG}
  polaris tag       : apache-polaris:${POLARIS_TAG}
  skip-download     : ${SKIP_DOWNLOAD}
  skip-build        : ${SKIP_BUILD}
================================================================
EOF

# -------------------------------------------------------------------------
# Step 1: download raw artifacts
# -------------------------------------------------------------------------
if [ "$SKIP_DOWNLOAD" = "0" ]; then
  echo "[step 1/4] downloading raw artifacts via download_cache/download.sh"
  ( cd "${CACHE_DIR}" && bash ./download.sh --arch "${ARCH_LIST}" )
else
  echo "[step 1/4] (skipped) assuming download_cache/ is already populated"
fi

# Sanity: required artifacts must exist before we can build images.
require_file() {
  local f="$1"
  if [ ! -s "$f" ]; then
    echo "[error] required file missing or empty: $f" >&2
    exit 1
  fi
}
require_file "${CACHE_DIR}/hadoop-${HADOOP_VERSION}.tar.gz"
require_file "${CACHE_DIR}/apache-hive-${HIVE_VERSION}-bin.tar.gz"
require_file "${CACHE_DIR}/spark-${SPARK_VERSION}-bin-hadoop3.tgz"
require_file "${CACHE_DIR}/polaris-bin-${POLARIS_VERSION}.tgz"
require_file "${CACHE_DIR}/hudi-spark3-bundle_2.12-${HUDI_VERSION}.jar"
require_file "${CACHE_DIR}/iceberg-spark-runtime-3.3_2.12-${ICEBERG_VERSION}.jar"
require_file "${CACHE_DIR}/hadoop-aws-${HADOOP_VERSION}.jar"
require_file "${CACHE_DIR}/aws-java-sdk-bundle-${AWS_SDK_VERSION}.jar"
for a in "${ARCHES[@]}"; do
  require_file "${CACHE_DIR}/linux-${a}/minio"
  require_file "${CACHE_DIR}/linux-${a}/mc"
done

# -------------------------------------------------------------------------
# Step 2: build multi-arch docker images and save to tar
# -------------------------------------------------------------------------
mkdir -p "${BUNDLE_DIR}/common"
for a in "${ARCHES[@]}"; do
  mkdir -p "${BUNDLE_DIR}/linux-${a}"
done

if [ "$SKIP_BUILD" = "0" ]; then
  echo "[step 2/4] preparing buildx"
  if ! docker buildx version >/dev/null 2>&1; then
    echo "[error] 'docker buildx' is not available. Install Docker Buildx first." >&2
    exit 1
  fi

  # Detect host arch in docker-compatible form.
  HOST_ARCH="$(dpkg --print-architecture 2>/dev/null || true)"
  if [ -z "$HOST_ARCH" ]; then
    case "$(uname -m)" in
      x86_64)  HOST_ARCH=amd64 ;;
      aarch64) HOST_ARCH=arm64 ;;
      *)       HOST_ARCH=$(uname -m) ;;
    esac
  fi

  # Decide builder strategy.
  #   Native single-arch   -> use default builder (daemon's built-in buildkit).
  #                           No moby/buildkit image pull, no --platform needed.
  #   Cross-arch requested -> fall back to docker-container driver + QEMU, which
  #                           requires moby/buildkit and tonistiigi/binfmt to be
  #                           reachable. Prefer running on a native-arch host.
  NEEDS_CROSS=0
  for a in "${ARCHES[@]}"; do
    if [ "$a" != "$HOST_ARCH" ]; then NEEDS_CROSS=1; fi
  done

  if [ "$NEEDS_CROSS" = "0" ]; then
    echo "[buildx] using default builder (native ${HOST_ARCH}, no QEMU)"
    docker buildx use default
    PLATFORM_FLAG=()
  else
    BUILDER_NAME="datalake-fdw-bundle"
    echo "[buildx] cross-arch requested; using docker-container driver '${BUILDER_NAME}'"
    echo "         NOTE: this pulls moby/buildkit and requires QEMU binfmt."
    echo "         Prefer running this script natively on each target arch."
    if ! docker buildx inspect "${BUILDER_NAME}" >/dev/null 2>&1; then
      docker buildx create --name "${BUILDER_NAME}" --driver docker-container --use >/dev/null
      docker buildx inspect --bootstrap "${BUILDER_NAME}" >/dev/null
    fi
    docker buildx use "${BUILDER_NAME}"
    PLATFORM_FLAG=()  # filled per-iteration below
  fi

  # Pre-load the ubuntu base image for each requested arch so `FROM ubuntu:22.04`
  # hits the local daemon cache instead of trying Docker Hub. Harmless if the
  # tar is missing — we just warn and let buildx/daemon try the registry.
  for a in "${ARCHES[@]}"; do
    UBU_TAR="${CACHE_DIR}/ubuntu-${UBUNTU_VERSION:-22.04}-${a}.tar"
    if [ -s "$UBU_TAR" ]; then
      echo "[docker-load] ${UBU_TAR}"
      docker load -i "$UBU_TAR" >/dev/null
    else
      echo "[warn] ${UBU_TAR} not present; docker build will try to pull ubuntu:${UBUNTU_VERSION:-22.04} from the registry"
    fi
  done

  # Drop Polaris tarball into the Polaris build context so the conditional
  # COPY in that Dockerfile picks it up. Always copy from the cache (idempotent).
  cp -f "${CACHE_DIR}/polaris-bin-${POLARIS_VERSION}.tgz" "${POLARIS_DIR}/polaris.tgz"
  trap 'rm -f "${POLARIS_DIR}/polaris.tgz"' EXIT

  TMP_TAGS=()

  for a in "${ARCHES[@]}"; do
    echo "----------------------------------------------------------------"
    echo "[step 2/4] building images for linux/${a}"
    echo "----------------------------------------------------------------"

    LH_LOCAL_TAG="lakehouse-allinone:${LAKEHOUSE_TAG}-linux-${a}"
    PL_LOCAL_TAG="apache-polaris:${POLARIS_TAG}-linux-${a}"

    # Native path skips --platform entirely so the default docker driver works.
    # Cross-arch path passes --platform to the docker-container builder.
    if [ "$NEEDS_CROSS" = "0" ]; then
      PLATFORM_FLAG=()
    else
      PLATFORM_FLAG=(--platform "linux/${a}")
    fi

    echo "[build] ${LH_LOCAL_TAG}"
    docker buildx build \
      "${PLATFORM_FLAG[@]}" \
      --load \
      --file "${SC_DIR}/Dockerfile" \
      --tag "${LH_LOCAL_TAG}" \
      "${SC_DIR}"

    echo "[build] ${PL_LOCAL_TAG}"
    docker buildx build \
      "${PLATFORM_FLAG[@]}" \
      --load \
      --file "${POLARIS_DIR}/Dockerfile" \
      --tag "${PL_LOCAL_TAG}" \
      "${POLARIS_DIR}"

    LH_TAR="${BUNDLE_DIR}/linux-${a}/lakehouse-allinone-${LAKEHOUSE_TAG}.tar"
    PL_TAR="${BUNDLE_DIR}/linux-${a}/apache-polaris-${POLARIS_TAG}.tar"

    echo "[save] ${LH_TAR}"
    docker save -o "${LH_TAR}" "${LH_LOCAL_TAG}"
    echo "[save] ${PL_TAR}"
    docker save -o "${PL_TAR}" "${PL_LOCAL_TAG}"

    TMP_TAGS+=("${LH_LOCAL_TAG}" "${PL_LOCAL_TAG}")
  done

  if [ "$KEEP_TAGS" = "0" ]; then
    echo "[cleanup] removing per-arch local tags"
    for t in "${TMP_TAGS[@]}"; do
      docker rmi "$t" >/dev/null 2>&1 || true
    done
  fi
else
  echo "[step 2/4] (skipped) docker build + save"
fi

# -------------------------------------------------------------------------
# Step 3: organize the bundle directory
# -------------------------------------------------------------------------
echo "[step 3/4] assembling bundle at ${BUNDLE_DIR}"

# Copy (hardlink if same FS) common artifacts.
link_or_copy() {
  local src="$1" dst="$2"
  if [ -e "$dst" ] && [ "$(stat -c '%i' "$src")" = "$(stat -c '%i' "$dst" 2>/dev/null || echo -)" ]; then
    return 0
  fi
  rm -f "$dst"
  if ln "$src" "$dst" 2>/dev/null; then
    return 0
  fi
  cp -f "$src" "$dst"
}

COMMON_FILES=(
  "hadoop-${HADOOP_VERSION}.tar.gz"
  "apache-hive-${HIVE_VERSION}-bin.tar.gz"
  "spark-${SPARK_VERSION}-bin-hadoop3.tgz"
  "polaris-bin-${POLARIS_VERSION}.tgz"
  "hudi-spark3-bundle_2.12-${HUDI_VERSION}.jar"
  "iceberg-spark-runtime-3.3_2.12-${ICEBERG_VERSION}.jar"
  "hadoop-aws-${HADOOP_VERSION}.jar"
  "aws-java-sdk-bundle-${AWS_SDK_VERSION}.jar"
)
for f in "${COMMON_FILES[@]}"; do
  link_or_copy "${CACHE_DIR}/${f}" "${BUNDLE_DIR}/common/${f}"
done

for a in "${ARCHES[@]}"; do
  link_or_copy "${CACHE_DIR}/linux-${a}/minio" "${BUNDLE_DIR}/linux-${a}/minio"
  link_or_copy "${CACHE_DIR}/linux-${a}/mc"    "${BUNDLE_DIR}/linux-${a}/mc"
done

# -------------------------------------------------------------------------
# Step 4: SHA256SUMS and bundle README
# -------------------------------------------------------------------------
if [ "$SKIP_CHECKSUM" = "0" ]; then
  echo "[step 4/4] generating ${BUNDLE_DIR}/SHA256SUMS"
  if command -v sha256sum >/dev/null 2>&1; then SHA_CMD="sha256sum"; else SHA_CMD="shasum -a 256"; fi
  ( cd "${BUNDLE_DIR}" && \
    find . -type f \! -name SHA256SUMS \! -name README.md -print0 \
      | LC_ALL=C sort -z \
      | xargs -0 $SHA_CMD > SHA256SUMS )
  echo "[done] SHA256SUMS ($(wc -l < "${BUNDLE_DIR}/SHA256SUMS") entries)"
else
  echo "[step 4/4] (skipped) SHA256SUMS"
fi

cat > "${BUNDLE_DIR}/README.md" <<EOF
# datalake_fdw prefetch-bundle v1

Generated by \`scripts/build-bundle.sh\` on $(date -u +"%Y-%m-%dT%H:%M:%SZ").

## Component versions

| Component | Version |
|---|---|
| Hadoop   | ${HADOOP_VERSION}   |
| Hive     | ${HIVE_VERSION}     |
| Spark    | ${SPARK_VERSION}    |
| Hudi     | ${HUDI_VERSION}     |
| Iceberg  | ${ICEBERG_VERSION}  |
| Polaris  | ${POLARIS_VERSION}  |
| AWS SDK  | ${AWS_SDK_VERSION}  |

## Docker image tags

- \`lakehouse-allinone:${LAKEHOUSE_TAG}\`
- \`apache-polaris:${POLARIS_TAG}\`

## Layout

\`\`\`
v1/
├── SHA256SUMS
├── README.md
├── common/            # arch-neutral tarballs + jars
├── linux-amd64/       # minio, mc, and docker-save tars for amd64 (if built)
└── linux-arm64/       # minio, mc, and docker-save tars for arm64 (if built)
\`\`\`

## Consuming the bundle

1. Upload the whole \`v1/\` tree to internal MinIO, e.g.
   \`s3://<bucket>/datalake-fdw/prefetch/v1/\`.
2. On a CI runner, \`docker load\` the per-arch image tars:
   \`\`\`
   docker load -i linux-\${ARCH}/lakehouse-allinone-${LAKEHOUSE_TAG}.tar
   docker load -i linux-\${ARCH}/apache-polaris-${POLARIS_TAG}.tar
   \`\`\`
3. For raw Apache artifacts, point \`DOWNLOAD_BASE_URL\` at the MinIO
   prefix so \`download_cache/download.sh\` fetches from the internal
   mirror instead of the default OBS one.

## Rebuilding

\`\`\`
cd contrib/datalake_fdw/automation/docker/singlecluster
bash scripts/build-bundle.sh --output-dir /tmp/prefetch-bundle
\`\`\`
EOF

echo ""
echo "================================================================"
echo "Bundle ready: ${BUNDLE_DIR}"
du -sh "${BUNDLE_DIR}"/* 2>/dev/null || true
echo "================================================================"
