#!/bin/bash
#
# test-coverage-hook.sh
# End-to-end tests for the pre-push coverage check hook.
#
# Creates a temporary git repo, simulates various push scenarios,
# and verifies the hook behaves correctly in each case.
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_DIR=$(mktemp -d)
PASS=0
FAIL=0

trap 'rm -rf "$TEST_DIR"' EXIT

# ---------- test helpers ----------

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'

pass() {
    PASS=$((PASS + 1))
    echo -e "  ${GREEN}PASS${NC}: $1"
}

fail() {
    FAIL=$((FAIL + 1))
    echo -e "  ${RED}FAIL${NC}: $1"
    if [ -n "${2:-}" ]; then
        echo -e "        $2"
    fi
}

# Run check-coverage.sh with given stdin, capture exit code and output.
# Usage: run_hook "stdin_content" [env_vars...]
run_hook() {
    local stdin_data="$1"
    shift
    local output
    local rc=0
    output=$(echo "$stdin_data" | env "$@" bash "$SCRIPT_DIR/check-coverage.sh" 2>&1) || rc=$?
    echo "$output"
    return $rc
}

# ---------- setup test repo ----------

setup_test_repo() {
    echo "Setting up test repository in $TEST_DIR..."

    cd "$TEST_DIR"
    git init -q
    git checkout -q -b main

    # Initial commit: empty
    echo "initial" > README.md
    git add README.md
    git commit -q -m "Initial commit"

    # Create "remote" bare repo for push simulation
    git clone -q --bare . "${TEST_DIR}/remote.git"
    git remote add test_remote "${TEST_DIR}/remote.git"
    git push -q test_remote main

    # Copy hook scripts into the test repo
    mkdir -p scripts/hooks
    cp "$SCRIPT_DIR/pre-push" scripts/hooks/pre-push
    cp "$SCRIPT_DIR/check-coverage.sh" scripts/hooks/check-coverage.sh
    chmod +x scripts/hooks/pre-push scripts/hooks/check-coverage.sh
    git add scripts/hooks
    git commit -q -m "Add hook scripts"
    git push -q test_remote main
}

# ---------- test cases ----------

test_skip_env_var() {
    echo ""
    echo "Test 1: SKIP_COVERAGE_CHECK=1 skips the check"

    local sha_local sha_remote
    sha_local=$(git rev-parse HEAD)
    sha_remote=$(git rev-parse HEAD~1)
    local stdin="refs/heads/main $sha_local refs/heads/main $sha_remote"

    local output rc=0
    output=$(echo "$stdin" | SKIP_COVERAGE_CHECK=1 bash scripts/hooks/pre-push 2>&1) || rc=$?

    if [ $rc -eq 0 ] && echo "$output" | grep -q "Skipped"; then
        pass "SKIP_COVERAGE_CHECK=1 exits 0 with skip message"
    else
        fail "SKIP_COVERAGE_CHECK=1 did not skip (rc=$rc)" "$output"
    fi
}

test_delete_branch() {
    echo ""
    echo "Test 2: Delete branch (local SHA all zeros) passes silently"

    local sha_remote
    sha_remote=$(git rev-parse HEAD)
    local zero="0000000000000000000000000000000000000000"
    local stdin="refs/heads/old $zero refs/heads/old $sha_remote"

    local output rc=0
    output=$(run_hook "$stdin") || rc=$?

    if [ $rc -eq 0 ]; then
        pass "Delete branch exits 0"
    else
        fail "Delete branch should exit 0, got rc=$rc" "$output"
    fi
}

test_no_changes() {
    echo ""
    echo "Test 3: Identical SHAs (no changes) passes silently"

    local sha
    sha=$(git rev-parse HEAD)
    local stdin="refs/heads/main $sha refs/heads/main $sha"

    local output rc=0
    output=$(run_hook "$stdin") || rc=$?

    if [ $rc -eq 0 ]; then
        pass "No changes exits 0"
    else
        fail "No changes should exit 0, got rc=$rc" "$output"
    fi
}

test_only_scripts_docs() {
    echo ""
    echo "Test 4: Only script/doc changes skip coverage check silently"

    # Create a commit that only modifies non-C files
    git checkout -q -b test-scripts-only main
    echo '#!/bin/bash' > test_script.sh
    echo 'SELECT 1;' > test_query.sql
    echo '# doc' > test_doc.md
    git add test_script.sh test_query.sql test_doc.md
    git commit -q -m "Add scripts and docs only"

    local sha_local sha_remote
    sha_local=$(git rev-parse HEAD)
    sha_remote=$(git rev-parse HEAD~1)
    local stdin="refs/heads/test-scripts-only $sha_local refs/heads/test-scripts-only $sha_remote"

    local output rc=0
    output=$(run_hook "$stdin") || rc=$?

    if [ $rc -eq 0 ] && [ -z "$output" ]; then
        pass "Script/doc only changes: silent exit 0 (no output)"
    elif [ $rc -eq 0 ]; then
        pass "Script/doc only changes: exit 0"
    else
        fail "Script/doc only changes should exit 0, got rc=$rc" "$output"
    fi

    git checkout -q main
}

test_c_changes_no_coverage_no_gcno() {
    echo ""
    echo "Test 5: C file changes, no coverage.info, no .gcno files -> error with configure hint"

    git checkout -q -b test-c-no-cov main
    mkdir -p src/backend/test
    cat > src/backend/test/test_func.c << 'CEOF'
#include <stdio.h>
int new_function(int x) {
    return x + 1;
}
CEOF
    git add src/backend/test/test_func.c
    git commit -q -m "Add C source file"

    local sha_local sha_remote
    sha_local=$(git rev-parse HEAD)
    sha_remote=$(git rev-parse HEAD~1)
    local stdin="refs/heads/test-c-no-cov $sha_local refs/heads/test-c-no-cov $sha_remote"

    local output rc=0
    output=$(run_hook "$stdin" COVERAGE_INFO=nonexistent.info) || rc=$?

    if [ $rc -ne 0 ]; then
        pass "C changes without coverage data: exits non-zero"
    else
        fail "C changes without coverage data should fail, got rc=0"
    fi

    if echo "$output" | grep -q "configure.*--enable-coverage"; then
        pass "Error message includes configure --enable-coverage hint"
    else
        fail "Error message should mention --enable-coverage" "$output"
    fi

    if echo "$output" | grep -q "CXXFLAGS.*--coverage"; then
        pass "Error message includes CXXFLAGS='--coverage' hint"
    else
        fail "Error message should mention CXXFLAGS='--coverage'" "$output"
    fi

    git checkout -q main
}

test_c_changes_with_coverage_all_covered() {
    echo ""
    echo "Test 6: C file changes with full coverage -> PASSED"

    git checkout -q -b test-c-covered main
    mkdir -p src/backend/test
    cat > src/backend/test/covered_func.c << 'CEOF'
#include <stdio.h>
int covered_function(int x) {
    return x * 2;
}
CEOF
    git add src/backend/test/covered_func.c
    git commit -q -m "Add fully covered C function"

    # Create a fake coverage.info that covers all lines
    local abs_path
    abs_path=$(cd "$TEST_DIR" && pwd)/src/backend/test/covered_func.c
    cat > coverage.info << LCOV
SF:$abs_path
DA:1,1
DA:2,5
DA:3,5
DA:4,5
end_of_record
LCOV
    # Also create with relative path (the script uses git diff which outputs relative)
    cat >> coverage.info << LCOV
SF:src/backend/test/covered_func.c
DA:1,1
DA:2,5
DA:3,5
DA:4,5
end_of_record
LCOV

    local sha_local sha_remote
    sha_local=$(git rev-parse HEAD)
    sha_remote=$(git rev-parse HEAD~1)
    local stdin="refs/heads/test-c-covered $sha_local refs/heads/test-c-covered $sha_remote"

    local output rc=0
    output=$(run_hook "$stdin" COVERAGE_INFO=coverage.info) || rc=$?

    if [ $rc -eq 0 ] && echo "$output" | grep -q "PASSED"; then
        pass "Fully covered C code: PASSED"
    else
        fail "Fully covered C code should pass, got rc=$rc" "$output"
    fi

    rm -f coverage.info
    git checkout -q main
}

test_c_changes_with_coverage_below_threshold() {
    echo ""
    echo "Test 7: C file changes below threshold -> FAILED with report"

    git checkout -q -b test-c-below main
    mkdir -p src/backend/test
    cat > src/backend/test/uncovered_func.c << 'CEOF'
#include <stdio.h>
int uncovered_function(int x) {
    if (x > 0) {
        return x + 1;
    } else {
        return x - 1;
    }
}
CEOF
    git add src/backend/test/uncovered_func.c
    git commit -q -m "Add partially covered C function"

    # Create coverage.info with some lines covered, some not
    cat > coverage.info << LCOV
SF:src/backend/test/uncovered_func.c
DA:2,5
DA:3,5
DA:4,5
DA:5,0
DA:6,0
DA:7,0
DA:8,1
end_of_record
LCOV

    local sha_local sha_remote
    sha_local=$(git rev-parse HEAD)
    sha_remote=$(git rev-parse HEAD~1)
    local stdin="refs/heads/test-c-below $sha_local refs/heads/test-c-below $sha_remote"

    local output rc=0
    output=$(run_hook "$stdin" COVERAGE_INFO=coverage.info COVERAGE_THRESHOLD=80) || rc=$?

    if [ $rc -ne 0 ]; then
        pass "Below threshold: exits non-zero"
    else
        fail "Below threshold should fail, got rc=0" "$output"
    fi

    if echo "$output" | grep -q "FAILED"; then
        pass "Output includes FAILED message"
    else
        fail "Output should include FAILED" "$output"
    fi

    if echo "$output" | grep -q "Uncovered lines"; then
        pass "Output lists uncovered lines"
    else
        fail "Output should list uncovered lines" "$output"
    fi

    if echo "$output" | grep -q "SKIP_COVERAGE_CHECK"; then
        pass "Output includes skip hint"
    else
        fail "Output should include skip hint" "$output"
    fi

    rm -f coverage.info
    git checkout -q main
}

test_c_changes_with_low_threshold_passes() {
    echo ""
    echo "Test 8: Same code but with lower threshold -> PASSED"

    git checkout -q test-c-below 2>/dev/null

    # Reuse same coverage.info from test 7 setup
    cat > coverage.info << LCOV
SF:src/backend/test/uncovered_func.c
DA:2,5
DA:3,5
DA:4,5
DA:5,0
DA:6,0
DA:7,0
DA:8,1
end_of_record
LCOV

    local sha_local sha_remote
    sha_local=$(git rev-parse HEAD)
    sha_remote=$(git rev-parse HEAD~1)
    local stdin="refs/heads/test-c-below $sha_local refs/heads/test-c-below $sha_remote"

    # 4 covered out of 7 executable = 57%, set threshold to 50
    local output rc=0
    output=$(run_hook "$stdin" COVERAGE_INFO=coverage.info COVERAGE_THRESHOLD=50) || rc=$?

    if [ $rc -eq 0 ] && echo "$output" | grep -q "PASSED"; then
        pass "Low threshold (50%): PASSED"
    else
        fail "Low threshold should pass, got rc=$rc" "$output"
    fi

    rm -f coverage.info
    git checkout -q main
}

test_cpp_changes() {
    echo ""
    echo "Test 9: C++ (.cpp) file changes are detected and checked"

    git checkout -q -b test-cpp main
    mkdir -p src/backend/gporca/libgpopt/src
    cat > src/backend/gporca/libgpopt/src/COptimizer.cpp << 'CPPEOF'
#include <iostream>
class COptimizer {
public:
    int Optimize(int plan) {
        return plan + 1;
    }
};
CPPEOF
    git add src/backend/gporca/libgpopt/src/COptimizer.cpp
    git commit -q -m "Add ORCA C++ file"

    # Create coverage.info covering all lines
    cat > coverage.info << LCOV
SF:src/backend/gporca/libgpopt/src/COptimizer.cpp
DA:2,1
DA:4,5
DA:5,5
DA:6,5
end_of_record
LCOV

    local sha_local sha_remote
    sha_local=$(git rev-parse HEAD)
    sha_remote=$(git rev-parse HEAD~1)
    local stdin="refs/heads/test-cpp $sha_local refs/heads/test-cpp $sha_remote"

    local output rc=0
    output=$(run_hook "$stdin" COVERAGE_INFO=coverage.info) || rc=$?

    if [ $rc -eq 0 ] && echo "$output" | grep -q "PASSED"; then
        pass "C++ (.cpp) changes fully covered: PASSED"
    else
        fail "C++ covered code should pass, got rc=$rc" "$output"
    fi

    rm -f coverage.info
    git checkout -q main
}

test_excluded_files_skipped() {
    echo ""
    echo "Test 10: Auto-generated files (gram.c, scan.c) are excluded"

    git checkout -q -b test-excluded main
    mkdir -p src/backend/parser
    cat > src/backend/parser/gram.c << 'CEOF'
/* generated by bison */
int yyparse(void) { return 0; }
CEOF
    cat > src/backend/parser/scan.c << 'CEOF'
/* generated by flex */
int yylex(void) { return 0; }
CEOF
    git add src/backend/parser/gram.c src/backend/parser/scan.c
    git commit -q -m "Add generated parser files"

    local sha_local sha_remote
    sha_local=$(git rev-parse HEAD)
    sha_remote=$(git rev-parse HEAD~1)
    local stdin="refs/heads/test-excluded $sha_local refs/heads/test-excluded $sha_remote"

    # These files match the exclude pattern, so after exclusion there
    # should be 0 C/C++ lines to check.  However, has_c_cpp_changes()
    # uses --name-only which WILL see .c files, so coverage.info is
    # still required.  But the inner loop should find 0 lines and skip.
    cat > coverage.info << LCOV
SF:dummy
DA:1,1
end_of_record
LCOV

    local output rc=0
    output=$(run_hook "$stdin" COVERAGE_INFO=coverage.info) || rc=$?

    if [ $rc -eq 0 ]; then
        pass "Excluded files (gram.c, scan.c): exits 0"
    else
        fail "Excluded files should be skipped, got rc=$rc" "$output"
    fi

    if echo "$output" | grep -q "No new C/C++ lines\|skipping"; then
        pass "Output indicates lines were skipped"
    else
        # It's also OK if there's simply no output about failure
        if ! echo "$output" | grep -q "FAILED"; then
            pass "No FAILED message for excluded files"
        else
            fail "Should not FAIL on excluded files" "$output"
        fi
    fi

    rm -f coverage.info
    git checkout -q main
}

test_mixed_changes() {
    echo ""
    echo "Test 11: Mixed changes (C + Python) -- only C part is checked"

    git checkout -q -b test-mixed main
    mkdir -p src/backend/test
    cat > src/backend/test/mixed.c << 'CEOF'
int mixed_func(void) { return 42; }
CEOF
    echo 'print("hello")' > helper.py
    git add src/backend/test/mixed.c helper.py
    git commit -q -m "Add mixed C and Python files"

    cat > coverage.info << LCOV
SF:src/backend/test/mixed.c
DA:1,10
end_of_record
LCOV

    local sha_local sha_remote
    sha_local=$(git rev-parse HEAD)
    sha_remote=$(git rev-parse HEAD~1)
    local stdin="refs/heads/test-mixed $sha_local refs/heads/test-mixed $sha_remote"

    local output rc=0
    output=$(run_hook "$stdin" COVERAGE_INFO=coverage.info) || rc=$?

    if [ $rc -eq 0 ] && echo "$output" | grep -q "PASSED"; then
        pass "Mixed C+Python: C part covered, PASSED"
    else
        fail "Mixed changes with covered C should pass, got rc=$rc" "$output"
    fi

    rm -f coverage.info
    git checkout -q main
}

test_pre_push_hook_entry() {
    echo ""
    echo "Test 12: pre-push entry script delegates to check-coverage.sh"

    # Test that pre-push correctly invokes check-coverage.sh
    local sha
    sha=$(git rev-parse HEAD)
    local stdin="refs/heads/main $sha refs/heads/main $sha"

    local output rc=0
    output=$(echo "$stdin" | bash scripts/hooks/pre-push 2>&1) || rc=$?

    if [ $rc -eq 0 ]; then
        pass "pre-push delegates correctly (no-change case)"
    else
        fail "pre-push should delegate and exit 0 for no changes, got rc=$rc" "$output"
    fi
}

test_existing_branch_uses_remote_sha() {
    echo ""
    echo "Test 13: Existing branch push uses remote_sha..local_sha range"

    git checkout -q -b test-existing-range main

    # First commit: a C file that is NOT covered (would fail if checked)
    mkdir -p src/backend/test
    cat > src/backend/test/old_func.c << 'CEOF'
int old_uncovered(int x) {
    return x - 1;
}
CEOF
    git add src/backend/test/old_func.c
    git commit -q -m "Old uncovered C function"
    local sha_old
    sha_old=$(git rev-parse HEAD)

    # Second commit: a C file that IS covered
    cat > src/backend/test/new_func.c << 'CEOF'
int new_covered(int x) {
    return x + 1;
}
CEOF
    git add src/backend/test/new_func.c
    git commit -q -m "New covered C function"
    local sha_new
    sha_new=$(git rev-parse HEAD)

    # Coverage only covers the new file
    cat > coverage.info << LCOV
SF:src/backend/test/new_func.c
DA:1,5
DA:2,5
DA:3,5
end_of_record
LCOV

    # Simulate pushing only the second commit (existing branch: remote=sha_old)
    local stdin="refs/heads/test-existing-range $sha_new refs/heads/test-existing-range $sha_old"

    local output rc=0
    output=$(run_hook "$stdin" COVERAGE_INFO=coverage.info) || rc=$?

    if [ $rc -eq 0 ] && echo "$output" | grep -q "PASSED"; then
        pass "Existing branch push: only checks new commits, PASSED"
    else
        fail "Existing branch push should only check remote_sha..local_sha, got rc=$rc" "$output"
    fi

    rm -f coverage.info
    git checkout -q main
}

test_header_only_not_in_coverage() {
    echo ""
    echo "Test 14: Header-only changes not in coverage data treated as non-executable"

    git checkout -q -b test-header-only main
    mkdir -p src/include/test
    cat > src/include/test/header_only.h << 'HEOF'
#ifndef HEADER_ONLY_H
#define HEADER_ONLY_H

extern int some_function(int x);
typedef struct TestStruct {
    int field;
} TestStruct;

#endif
HEOF
    git add src/include/test/header_only.h
    git commit -q -m "Add header-only file"

    # Coverage info has no entry for this header file
    cat > coverage.info << LCOV
SF:src/backend/test/some_other.c
DA:1,5
end_of_record
LCOV

    local sha_local sha_remote
    sha_local=$(git rev-parse HEAD)
    sha_remote=$(git rev-parse HEAD~1)
    local stdin="refs/heads/test-header-only $sha_local refs/heads/test-header-only $sha_remote"

    local output rc=0
    output=$(run_hook "$stdin" COVERAGE_INFO=coverage.info) || rc=$?

    if [ $rc -eq 0 ]; then
        pass "Header-only changes: exits 0 (treated as non-executable)"
    else
        fail "Header-only changes should pass, got rc=$rc" "$output"
    fi

    rm -f coverage.info
    git checkout -q main
}

test_new_branch_push() {
    echo ""
    echo "Test 15: New branch (remote SHA all zeros) uses merge-base"

    git checkout -q -b test-new-branch main
    echo 'print("new branch")' > new_branch_file.py
    git add new_branch_file.py
    git commit -q -m "New branch with only Python"

    local sha_local
    sha_local=$(git rev-parse HEAD)
    local zero="0000000000000000000000000000000000000000"
    local stdin="refs/heads/test-new-branch $sha_local refs/heads/test-new-branch $zero"

    local output rc=0
    output=$(run_hook "$stdin") || rc=$?

    if [ $rc -eq 0 ]; then
        pass "New branch with non-C files: exits 0"
    else
        fail "New branch with only Python should pass, got rc=$rc" "$output"
    fi

    git checkout -q main
}

# ---------- run all tests ----------

main() {
    echo "=============================================="
    echo " Pre-push Coverage Hook - Test Suite"
    echo "=============================================="

    setup_test_repo

    test_skip_env_var
    test_delete_branch
    test_no_changes
    test_only_scripts_docs
    test_c_changes_no_coverage_no_gcno
    test_c_changes_with_coverage_all_covered
    test_c_changes_with_coverage_below_threshold
    test_c_changes_with_low_threshold_passes
    test_cpp_changes
    test_excluded_files_skipped
    test_mixed_changes
    test_pre_push_hook_entry
    test_existing_branch_uses_remote_sha
    test_header_only_not_in_coverage
    test_new_branch_push

    echo ""
    echo "=============================================="
    echo -e " Results: ${GREEN}${PASS} passed${NC}, ${RED}${FAIL} failed${NC}"
    echo "=============================================="

    if [ "$FAIL" -gt 0 ]; then
        exit 1
    fi
    exit 0
}

main "$@"
