#!/bin/bash
# run_all_tests.sh
# Run complete test suite: smoke → performance → feature

set -e

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AUTOMATION_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# Source configuration and common functions
source "${AUTOMATION_DIR}/config/test_config.env"
source "${AUTOMATION_DIR}/scripts/utils/common_functions.sh"

log_info "==================================================================="
log_info "Running Complete Datalake FDW Test Suite"
log_info "==================================================================="

# Record start time
START_TIME=$(date +%s)

# Create overall report directory
OVERALL_REPORT_DIR="${REPORTS_DIR}/all/${REPORT_TIMESTAMP}"
ensure_dir "${OVERALL_REPORT_DIR}"

# Track overall status
OVERALL_STATUS=0

# Run smoke tests
if [ "${ENABLE_SMOKE_TESTS}" = "true" ]; then
    log_info "==================================================================="
    log_info "Phase 1: Smoke Tests"
    log_info "==================================================================="

    if bash "${SCRIPT_DIR}/run_smoke_tests.sh"; then
        log_success "Smoke tests passed"
        echo "SMOKE: PASSED" >> "${OVERALL_REPORT_DIR}/summary.txt"
    else
        log_error "Smoke tests failed"
        echo "SMOKE: FAILED" >> "${OVERALL_REPORT_DIR}/summary.txt"
        OVERALL_STATUS=1
    fi
else
    log_info "Smoke tests disabled"
fi

# Run performance tests
if [ "${ENABLE_PERFORMANCE_TESTS}" = "true" ]; then
    log_info "==================================================================="
    log_info "Phase 2: Performance Tests"
    log_info "==================================================================="

    if [ -f "${SCRIPT_DIR}/run_performance_tests.sh" ]; then
        if bash "${SCRIPT_DIR}/run_performance_tests.sh"; then
            log_success "Performance tests passed"
            echo "PERFORMANCE: PASSED" >> "${OVERALL_REPORT_DIR}/summary.txt"
        else
            log_error "Performance tests failed"
            echo "PERFORMANCE: FAILED" >> "${OVERALL_REPORT_DIR}/summary.txt"
            OVERALL_STATUS=1
        fi
    else
        log_warn "Performance test script not found (Phase 2 not implemented yet)"
        echo "PERFORMANCE: SKIPPED" >> "${OVERALL_REPORT_DIR}/summary.txt"
    fi
else
    log_info "Performance tests disabled"
fi

# Run feature tests
if [ "${ENABLE_FEATURE_TESTS}" = "true" ]; then
    log_info "==================================================================="
    log_info "Phase 3: Feature Tests"
    log_info "==================================================================="

    if [ -f "${SCRIPT_DIR}/run_feature_tests.sh" ]; then
        if bash "${SCRIPT_DIR}/run_feature_tests.sh"; then
            log_success "Feature tests passed"
            echo "FEATURE: PASSED" >> "${OVERALL_REPORT_DIR}/summary.txt"
        else
            log_error "Feature tests failed"
            echo "FEATURE: FAILED" >> "${OVERALL_REPORT_DIR}/summary.txt"
            OVERALL_STATUS=1
        fi
    else
        log_warn "Feature test script not found (Phase 3 not implemented yet)"
        echo "FEATURE: SKIPPED" >> "${OVERALL_REPORT_DIR}/summary.txt"
    fi
else
    log_info "Feature tests disabled"
fi

# Calculate duration
END_TIME=$(date +%s)
DURATION=$((END_TIME - START_TIME))
MINUTES=$((DURATION / 60))
SECONDS=$((DURATION % 60))

# Generate final report
log_info "==================================================================="
log_info "Complete Test Suite Summary"
log_info "==================================================================="

cat "${OVERALL_REPORT_DIR}/summary.txt"

log_info "-------------------------------------------------------------------"
log_info "Total Duration: ${MINUTES}m ${SECONDS}s"
log_info "Report Directory: ${OVERALL_REPORT_DIR}"
log_info "==================================================================="

if [ ${OVERALL_STATUS} -eq 0 ]; then
    log_success "All enabled test suites passed!"
    exit 0
else
    log_error "Some test suites failed"
    exit 1
fi
