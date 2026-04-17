#!/bin/bash
# run_performance_tests.sh
# Orchestrate execution of all performance tests

set -e

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AUTOMATION_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# Source configuration and common functions
source "${AUTOMATION_DIR}/config/test_config.env"
source "${AUTOMATION_DIR}/scripts/utils/common_functions.sh"

log_info "==================================================================="
log_info "Running Datalake FDW Performance Tests"
log_info "==================================================================="

# Check if services are ready
log_info "Checking services..."
if ! bash "${AUTOMATION_DIR}/scripts/setup/check_services.sh"; then
    log_error "Services are not ready. Aborting tests."
    exit 1
fi

# Create report directory
PERF_REPORT_DIR="${REPORTS_DIR}/performance/${REPORT_TIMESTAMP}"
ensure_dir "${PERF_REPORT_DIR}"

# Initialize counters
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# Array to store failed tests
declare -a FAILED_TEST_LIST

# Cumulative perf summary CSV - appended after each category since pg_regress
# drops+recreates the regression DB at the start of each invocation.
PERF_DUMP_CSV="${PERF_REPORT_DIR}/perf_dump.csv"
: > "${PERF_DUMP_CSV}"  # truncate

# pg_regress runs tests in the "regression" database; perf_results_summary
# lives there at the moment we query it (after pg_regress finishes one category
# and before the next one drops the DB).
PG_REGRESS_DB="${PG_REGRESS_DB:-regression}"

dump_perf_summary() {
    local category=$1
    # Append CSV rows from regression DB. Schema must match the columns the
    # python parser in compare_baseline.sh expects:
    # test_name,operation,p50_ms,p95_ms,min_ms,max_ms,iterations,rows_count,throughput
    PGDATABASE="${PG_REGRESS_DB}" psql -At -F',' -c "
        SELECT DISTINCT ON (test_name, operation)
            test_name, operation,
            ROUND(p50_ms, 2),
            ROUND(p95_ms, 2),
            ROUND(min_ms, 2),
            ROUND(max_ms, 2),
            iterations,
            COALESCE(rows_count, 0),
            ROUND(COALESCE(throughput_rows_per_sec, 0), 2)
        FROM public.perf_results_summary
        ORDER BY test_name, operation, created_at DESC;
    " 2>/dev/null >> "${PERF_DUMP_CSV}" || \
        log_warn "Could not dump perf data for category ${category} (db=${PG_REGRESS_DB} may be empty)"
}

# Function to run a performance test category
run_performance_category() {
    local category=$1
    local test_dir="${SQLREPO_DIR}/performance/${category}"

    if [ ! -d "${test_dir}" ]; then
        log_warn "Performance test directory not found: ${test_dir}"
        return 0
    fi

    log_info "-------------------------------------------------------------------"
    log_info "Running ${category} performance tests..."
    log_info "-------------------------------------------------------------------"

    # Check if Makefile exists
    if [ ! -f "${test_dir}/Makefile" ]; then
        log_warn "No Makefile found in ${test_dir}"
        return 0
    fi

    TOTAL_TESTS=$((TOTAL_TESTS + 1))

    log_info "Running performance test: ${category}"

    pushd "${test_dir}" > /dev/null

    # Run the test
    if make installcheck > "${PERF_REPORT_DIR}/${category}.log" 2>&1; then
        log_success "Performance test passed: ${category}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        log_error "Performance test failed: ${category}"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        FAILED_TEST_LIST+=("${category}")

        # Copy regression.diffs if it exists
        if [ -f "regression.diffs" ]; then
            cp "regression.diffs" "${PERF_REPORT_DIR}/${category}.diffs"
            log_error "Regression diffs saved to: ${PERF_REPORT_DIR}/${category}.diffs"
        fi
    fi

    popd > /dev/null

    # Dump perf data even if pg_regress diff failed - the underlying queries
    # may still have produced timing data we want to capture.
    dump_perf_summary "${category}"
}

# Run performance tests for each category
log_info "Discovering performance test categories..."

# Allow caller to override via PERF_CATEGORIES or SMOKE_CATEGORIES env var
# (the Makefile passes CATEGORIES= via SMOKE_CATEGORIES for smoke, and we
# accept PERF_CATEGORIES for performance tests).
if [ -n "${PERF_CATEGORIES:-}" ]; then
    log_info "Using explicit PERF_CATEGORIES='${PERF_CATEGORIES}'"
else
    PERF_CATEGORIES=$(find "${SQLREPO_DIR}/performance" -mindepth 1 -maxdepth 1 -type d -exec basename {} \;)
fi

if [ -z "${PERF_CATEGORIES}" ]; then
    log_error "No performance test categories found in ${SQLREPO_DIR}/performance"
    exit 1
fi

for category in ${PERF_CATEGORIES}; do
    run_performance_category "${category}"
done

# Generate summary report
log_info "==================================================================="
log_info "Performance Test Summary"
log_info "==================================================================="

SUMMARY_FILE="${PERF_REPORT_DIR}/summary.txt"
generate_test_summary ${TOTAL_TESTS} ${PASSED_TESTS} ${FAILED_TESTS} "${SUMMARY_FILE}"

cat "${SUMMARY_FILE}"

# List failed tests
if [ ${FAILED_TESTS} -gt 0 ]; then
    log_error "Failed tests:"
    for failed_test in "${FAILED_TEST_LIST[@]}"; do
        log_error "  - ${failed_test}"
    done
fi

# Compare against baseline (writes perf_summary.txt + regressions.txt or
# baseline_suggested.json). Non-zero exit just means regressions found - it
# does not by itself mark the run as failed.
if [ -x "${SCRIPT_DIR}/compare_baseline.sh" ]; then
    log_info "Running baseline comparison..."
    if ! bash "${SCRIPT_DIR}/compare_baseline.sh" "${PERF_REPORT_DIR}"; then
        BASELINE_EXIT=$?
        if [ ${BASELINE_EXIT} -eq 2 ]; then
            log_warn "Baseline regressions detected (see ${PERF_REPORT_DIR}/regressions.txt)"
        else
            log_error "Baseline comparison script error (exit ${BASELINE_EXIT})"
        fi
    fi
fi

log_info "==================================================================="
log_info "Report saved to: ${PERF_REPORT_DIR}"
log_info "==================================================================="

# Exit with appropriate code
if [ ${FAILED_TESTS} -gt 0 ]; then
    exit 1
else
    exit 0
fi
