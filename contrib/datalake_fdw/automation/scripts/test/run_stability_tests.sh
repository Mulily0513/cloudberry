#!/bin/bash
# run_stability_tests.sh
# Orchestrate execution of all stability tests (concurrency / soak / repeat /
# schema_evolution / lightweight_recovery / catalog_consistency).
#
# Each category is a subdirectory under sqlrepo/stability/ with its own
# Makefile. Concurrency uses pg_isolation_regress; other categories use
# pg_regress. The runner iterates all subdirectories and runs `make
# installcheck` in each.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AUTOMATION_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

source "${AUTOMATION_DIR}/config/test_config.env"
source "${AUTOMATION_DIR}/scripts/utils/common_functions.sh"

log_info "==================================================================="
log_info "Running Datalake FDW Stability Tests"
log_info "==================================================================="

# Service check (informational; categories declare their own dependencies)
bash "${AUTOMATION_DIR}/scripts/setup/check_services.sh" || \
    log_warn "Some services unavailable - categories with hard dependencies will fail"

# Report directory
STAB_REPORT_DIR="${REPORTS_DIR}/stability/${REPORT_TIMESTAMP}"
ensure_dir "${STAB_REPORT_DIR}"

TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0
declare -a FAILED_TEST_LIST

run_stability_category() {
    local category=$1
    local test_dir="${SQLREPO_DIR}/stability/${category}"

    if [ ! -d "${test_dir}" ]; then
        log_warn "Stability test directory not found: ${test_dir}"
        return 0
    fi
    if [ ! -f "${test_dir}/Makefile" ]; then
        log_warn "No Makefile in ${test_dir}"
        return 0
    fi

    log_info "-------------------------------------------------------------------"
    log_info "Running ${category} stability tests..."
    log_info "-------------------------------------------------------------------"

    TOTAL_TESTS=$((TOTAL_TESTS + 1))

    pushd "${test_dir}" > /dev/null

    if make installcheck > "${STAB_REPORT_DIR}/${category}.log" 2>&1; then
        log_success "Stability test passed: ${category}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        log_error "Stability test failed: ${category}"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        FAILED_TEST_LIST+=("${category}")

        # pg_isolation_regress writes to output_iso/regression.diffs; pg_regress
        # writes to ./regression.diffs. Capture whichever exists.
        for diff_source in "output_iso/regression.diffs" "regression.diffs"; do
            if [ -f "${diff_source}" ]; then
                cp "${diff_source}" "${STAB_REPORT_DIR}/${category}.diffs"
                log_error "Diffs saved to: ${STAB_REPORT_DIR}/${category}.diffs"
                break
            fi
        done
    fi

    popd > /dev/null
}

log_info "Discovering stability test categories..."

# Categories to run - pass via STABILITY_CATEGORIES env var to override
if [ -n "${STABILITY_CATEGORIES:-}" ]; then
    STAB_CATEGORIES="${STABILITY_CATEGORIES}"
    log_info "Using explicit STABILITY_CATEGORIES='${STAB_CATEGORIES}'"
else
    STAB_CATEGORIES=$(find "${SQLREPO_DIR}/stability" -mindepth 1 -maxdepth 1 -type d \
                      -exec basename {} \; 2>/dev/null | sort)
fi

if [ -z "${STAB_CATEGORIES}" ]; then
    log_warn "No stability test categories found in ${SQLREPO_DIR}/stability"
    exit 0
fi

for category in ${STAB_CATEGORIES}; do
    run_stability_category "${category}"
done

# Summary
log_info "==================================================================="
log_info "Stability Test Summary"
log_info "==================================================================="
SUMMARY_FILE="${STAB_REPORT_DIR}/summary.txt"
generate_test_summary ${TOTAL_TESTS} ${PASSED_TESTS} ${FAILED_TESTS} "${SUMMARY_FILE}"
cat "${SUMMARY_FILE}"

if [ ${FAILED_TESTS} -gt 0 ]; then
    log_error "Failed tests:"
    for t in "${FAILED_TEST_LIST[@]}"; do
        log_error "  - ${t}"
    done
fi

log_info "Report saved to: ${STAB_REPORT_DIR}"

if [ ${FAILED_TESTS} -gt 0 ]; then
    exit 1
fi
exit 0
