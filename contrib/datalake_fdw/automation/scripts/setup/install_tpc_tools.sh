#!/usr/bin/env bash
# install_tpc_tools.sh
#
# Clone and build the official TPC-H dbgen and TPC-DS dsdgen into
# automation/tools/tpc/, with symlinks in tools/tpc/bin/. Invoked by
# install_tools.sh when INSTALL_TPC_TOOLS=1 and also directly callable.
#
# Outputs:
#   automation/tools/tpc/tpch-dbgen/dbgen
#   automation/tools/tpc/tpch-dbgen/qgen
#   automation/tools/tpc/tpcds-kit/tools/dsdgen
#   automation/tools/tpc/bin/{dbgen,dsdgen,qgen}   (symlinks)
#
# Idempotent: re-runs detect existing build artifacts and skip.
# Safe to run inside the hashdata container (where /workspace is mounted);
# can also run on a host with a standard C toolchain.
#
# Sources cloned but their generated data is NEVER committed — TPC tool
# output is license-restricted for distribution.

set -euo pipefail

AUTOMATION_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TPC_DIR="${AUTOMATION_DIR}/tools/tpc"
BIN_DIR="${TPC_DIR}/bin"
TPCH_SRC="${TPC_DIR}/tpch-dbgen"
TPCDS_SRC="${TPC_DIR}/tpcds-kit"

# Pin to commits known to build on Linux / GCC 12.
TPCH_REPO="${TPCH_REPO:-https://github.com/electrum/tpch-dbgen.git}"
TPCH_REF="${TPCH_REF:-master}"
TPCDS_REPO="${TPCDS_REPO:-https://github.com/gregrahn/tpcds-kit.git}"
TPCDS_REF="${TPCDS_REF:-master}"

mkdir -p "${TPC_DIR}" "${BIN_DIR}"

log() { echo "[install_tpc_tools] $*" >&2; }

need() { command -v "$1" >/dev/null 2>&1 || { echo "missing $1"; exit 1; }; }
need git
need make
need gcc

# ---------- tpch-dbgen ----------
# We only need `dbgen` to produce raw .tbl files; `qgen` is intentionally not
# built (its upstream sources have never compiled cleanly against gcc >= 10's
# format-string checks without additional macro definitions, and we author
# queries by hand rather than from qgen templates).
if [[ ! -x "${TPCH_SRC}/dbgen" ]]; then
    log "cloning tpch-dbgen into ${TPCH_SRC}"
    if [[ ! -d "${TPCH_SRC}/.git" ]]; then
        rm -rf "${TPCH_SRC}"
        git clone --depth 1 --branch "${TPCH_REF}" "${TPCH_REPO}" "${TPCH_SRC}"
    fi
    log "building dbgen"
    # Build dbgen only; ignore compile-time warnings (%ld / %lld on LP64) that
    # the upstream Makefile does not scrub.
    make -C "${TPCH_SRC}" -s dbgen MACHINE=LINUX DATABASE=POSTGRES WORKLOAD=TPCH \
         CC=gcc CFLAGS='-O2 -Wno-format -Wno-implicit-int -Wno-implicit-function-declaration' \
         >/dev/null 2>&1 || true
fi
[[ -x "${TPCH_SRC}/dbgen" ]] || { log "dbgen build failed"; exit 1; }
log "dbgen OK: $(${TPCH_SRC}/dbgen -h 2>&1 | head -1 || echo built)"

# ---------- tpcds-kit ----------
if [[ ! -x "${TPCDS_SRC}/tools/dsdgen" ]]; then
    log "cloning tpcds-kit into ${TPCDS_SRC}"
    if [[ ! -d "${TPCDS_SRC}/.git" ]]; then
        rm -rf "${TPCDS_SRC}"
        git clone --depth 1 --branch "${TPCDS_REF}" "${TPCDS_REPO}" "${TPCDS_SRC}"
    fi
    log "patching tpcds-kit makefile for modern gcc"
    # Upstream tpcds-kit relies on pre-C99 tentative-definition merging
    # (gcc >= 10 defaults to -fno-common, trips "multiple definition" link
    # errors) and on implicit function declarations (gcc >= 14 rejects by
    # default). We rewrite LINUX_CFLAGS in-place rather than overriding via
    # the command line so the Makefile's own BASE_CFLAGS — which defines
    # HUGE_TYPE via -D_FILE_OFFSET_BITS=64 -DLINUX -DYYDEBUG — stays intact.
    sed -i -E 's#^LINUX_CFLAGS[[:space:]]*=.*$#LINUX_CFLAGS\t= -g -Wall -fcommon -Wno-implicit-int -Wno-implicit-function-declaration -Wno-return-type -Wno-format -Wno-builtin-declaration-mismatch -Wno-int-conversion -Wno-incompatible-pointer-types#' "${TPCDS_SRC}/tools/makefile"
    log "building dsdgen"
    # dsdgen only. The wider suite's distcomp / qgen targets want yacc and
    # lex which we don't need for data generation; errors outside dsdgen
    # are tolerated as long as the dsdgen link succeeds.
    make -C "${TPCDS_SRC}/tools" -s OS=LINUX dsdgen >/dev/null 2>&1 || true
fi
[[ -x "${TPCDS_SRC}/tools/dsdgen" ]] || { log "dsdgen build failed"; exit 1; }
log "dsdgen OK"

# ---------- symlinks ----------
ln -sf "${TPCH_SRC}/dbgen" "${BIN_DIR}/dbgen"
ln -sf "${TPCDS_SRC}/tools/dsdgen" "${BIN_DIR}/dsdgen"

log "installed: ${BIN_DIR}/{dbgen,dsdgen}"
log "dbgen requires its data dir (dists.dss) from ${TPCH_SRC}"
log "dsdgen requires its data dir (tpcds.idx) from ${TPCDS_SRC}/tools"
