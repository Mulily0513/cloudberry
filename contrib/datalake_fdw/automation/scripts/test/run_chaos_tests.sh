#!/bin/bash
# run_chaos_tests.sh
# Orchestrate chaos tests (crash / fault / network / disk).
# All chaos tests have a mandatory cleanup trap: unpause, del tc, rm fill files.
# This runner adds a global safety net that cleans up even if a test fails to
# cleanup after itself.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AUTOMATION_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

source "${AUTOMATION_DIR}/config/test_config.env"
source "${AUTOMATION_DIR}/scripts/utils/common_functions.sh"

log_info "==================================================================="
log_info "Running Datalake FDW Chaos Tests"
log_info "==================================================================="
log_warn "Chaos tests inject real faults (kill -9, docker pause, tc netem, disk fill)."
log_warn "They are self-cleaning but require elevated privileges."

# Safety net: clean up on exit regardless of how we exit
global_cleanup() {
    log_info "Global chaos cleanup running..."
    # Unpause any paused containers
    for c in lakehouse singlecluster-polaris-1; do
        docker unpause "$c" 2>/dev/null && log_warn "Unpaused ${c} (was still paused)" || true
    done
    # Remove any tc rules we might have left
    tc qdisc del dev eth0 root 2>/dev/null || true
    # Remove fill files
    rm -f /tmp/chaos_fill_* 2>/dev/null || true
    log_info "Global chaos cleanup done"
}
trap global_cleanup EXIT

CHAOS_REPORT_DIR="${REPORTS_DIR}/chaos/${REPORT_TIMESTAMP}"
ensure_dir "${CHAOS_REPORT_DIR}"

TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0
declare -a FAILED_TEST_LIST

run_chaos_category() {
    local category=$1
    local test_dir="${SQLREPO_DIR}/chaos/${category}"

    if [ ! -d "${test_dir}" ]; then
        log_warn "Chaos test directory not found: ${test_dir}"
        return 0
    fi
    if [ ! -f "${test_dir}/Makefile" ]; then
        log_warn "No Makefile in ${test_dir}"
        return 0
    fi

    log_info "-------------------------------------------------------------------"
    log_info "Running ${category} chaos tests..."
    log_info "-------------------------------------------------------------------"

    TOTAL_TESTS=$((TOTAL_TESTS + 1))

    pushd "${test_dir}" > /dev/null

    if make installcheck > "${CHAOS_REPORT_DIR}/${category}.log" 2>&1; then
        log_success "Chaos test passed: ${category}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        log_error "Chaos test failed: ${category}"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        FAILED_TEST_LIST+=("${category}")
        for diff_source in "regression.diffs" "output_iso/regression.diffs"; do
            if [ -f "${diff_source}" ]; then
                cp "${diff_source}" "${CHAOS_REPORT_DIR}/${category}.diffs"
                break
            fi
        done
    fi

    popd > /dev/null
}

if [ -n "${CHAOS_CATEGORIES:-}" ]; then
    CATEGORIES="${CHAOS_CATEGORIES}"
else
    CATEGORIES=$(find "${SQLREPO_DIR}/chaos" -mindepth 1 -maxdepth 1 -type d \
                  -exec basename {} \; 2>/dev/null | sort)
fi

for category in ${CATEGORIES}; do
    run_chaos_category "${category}"
done

SUMMARY_FILE="${CHAOS_REPORT_DIR}/summary.txt"
generate_test_summary ${TOTAL_TESTS} ${PASSED_TESTS} ${FAILED_TESTS} "${SUMMARY_FILE}"
cat "${SUMMARY_FILE}"

log_info "Report saved to: ${CHAOS_REPORT_DIR}"

trap - EXIT
global_cleanup

[ ${FAILED_TESTS} -gt 0 ] && exit 1 || exit 0
