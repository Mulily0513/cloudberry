#!/bin/bash
#
# check-coverage.sh
# Incremental test coverage check for newly added C/C++ code.
#
# Called by the pre-push hook.  Can also be invoked manually:
#   echo "refs/heads/main <local_sha> refs/heads/main <remote_sha>" \
#     | ./scripts/hooks/check-coverage.sh
#
# Environment variables:
#   SKIP_COVERAGE_CHECK=1       skip the check entirely
#   COVERAGE_THRESHOLD=80       minimum coverage percentage (default 80)
#   COVERAGE_INFO=coverage.info path to lcov tracefile
#

set -euo pipefail

COVERAGE_THRESHOLD="${COVERAGE_THRESHOLD:-80}"
COVERAGE_INFO="${COVERAGE_INFO:-coverage.info}"
TMPDIR_BASE=$(mktemp -d)
trap 'rm -rf "$TMPDIR_BASE"' EXIT

# File patterns to exclude from coverage checking (regex for grep -E)
EXCLUDE_PATTERNS="gram\.c$|scan\.c$|_scanner\.c$|snowball/|regex/|timezone/|gporca/data/|gporca/server/|gporca/concourse/"

ZERO_SHA="0000000000000000000000000000000000000000"

# ---------- helpers ----------

log() { echo "[coverage-check] $*"; }
err() { echo "[coverage-check] ERROR: $*" >&2; }

diagnose_coverage_build() {
    local c_gcno cpp_gcno
    c_gcno=$(find src -path '*/gporca' -prune -o -name '*.gcno' -print 2>/dev/null | wc -l || echo 0)
    if [ -d src/backend/gporca ]; then
        cpp_gcno=$(find src/backend/gporca -name '*.gcno' 2>/dev/null | wc -l || echo 0)
    else
        cpp_gcno=0
    fi

    if [ "$c_gcno" -eq 0 ] && [ "$cpp_gcno" -eq 0 ]; then
        err "No .gcno files found -- the project was not compiled with coverage enabled."
        err ""
        err "Please re-configure and rebuild:"
        err "  ./configure --enable-coverage CXXFLAGS='--coverage' --prefix=\$PREFIX"
        err "  make clean && make -j\$(nproc) && make install"
    elif [ "$c_gcno" -gt 0 ] && [ "$cpp_gcno" -eq 0 ]; then
        err "C code has coverage (.gcno=$c_gcno), but ORCA C++ code does not."
        err ""
        err "Please re-configure with CXXFLAGS='--coverage':"
        err "  ./configure --enable-coverage CXXFLAGS='--coverage' --prefix=\$PREFIX"
        err "  make clean && make -j\$(nproc) && make install"
    elif [ "$c_gcno" -eq 0 ] && [ "$cpp_gcno" -gt 0 ]; then
        err "ORCA C++ code has coverage (.gcno=$cpp_gcno), but C code does not."
        err ""
        err "Please re-configure with --enable-coverage:"
        err "  ./configure --enable-coverage CXXFLAGS='--coverage' --prefix=\$PREFIX"
        err "  make clean && make -j\$(nproc) && make install"
    else
        log "Found .gcno files (C=$c_gcno, C++=$cpp_gcno) -- build config looks correct."
        err "Coverage data file (.gcda / coverage.info) is missing."
        err ""
        err "Please run the tests and generate the coverage report:"
        err "  make check"
        err "  lcov --capture --directory src --output-file coverage.info"
    fi
}

has_c_cpp_changes() {
    local range="$1"
    local changed
    changed=$(git diff "$range" --name-only --diff-filter=AM -- '*.c' '*.cpp' '*.h' '*.hpp')
    [ -n "$changed" ]
}

compute_range() {
    local local_sha="$1" remote_sha="$2"
    local base default_branch

    # For existing branches: only check commits not yet on the remote.
    if [ "$remote_sha" != "$ZERO_SHA" ]; then
        echo "${remote_sha}..${local_sha}"
        return 0
    fi

    # New branch: discover default branch and find merge-base.
    default_branch=$(git symbolic-ref --short refs/remotes/origin/HEAD 2>/dev/null | sed 's|.*/||')
    default_branch="${default_branch:-main}"

    # Prefer remote default branch; fall back to local if fetch fails
    GIT_TERMINAL_PROMPT=0 GIT_SSH_COMMAND='ssh -oBatchMode=yes' git fetch origin "$default_branch" --quiet 2>/dev/null || true
    if git rev-parse --verify "origin/$default_branch" >/dev/null 2>&1; then
        base=$(git merge-base "$local_sha" "origin/$default_branch" 2>/dev/null || true)
    else
        base=$(git merge-base "$local_sha" "$default_branch" 2>/dev/null || true)
    fi
    if [ -z "$base" ]; then
        return 1
    fi
    echo "${base}..${local_sha}"
}

# ---------- 1. Save stdin (ref list) for two-pass processing ----------

REFS_FILE="$TMPDIR_BASE/refs"
cat > "$REFS_FILE"

# ---------- 2. Quick check: any C/C++ changes at all? ----------

any_c_cpp=false
while read -r local_ref local_sha remote_ref remote_sha; do
    [ "$local_sha" = "$ZERO_SHA" ] && continue
    range=$(compute_range "$local_sha" "$remote_sha") || continue
    if has_c_cpp_changes "$range"; then
        any_c_cpp=true
        break
    fi
done < "$REFS_FILE"

if [ "$any_c_cpp" = false ]; then
    # No C/C++ changes -- nothing to check, allow push silently.
    exit 0
fi

# ---------- 3. Verify coverage data exists ----------

if [ ! -f "$COVERAGE_INFO" ]; then
    # Try to generate coverage.info from .gcda files if available
    if find src -name '*.gcda' 2>/dev/null | head -1 | grep -q .; then
        log "coverage.info not found, generating from .gcda files..."
        if lcov --capture --directory src --output-file "$COVERAGE_INFO" --quiet 2>/dev/null; then
            log "Generated $COVERAGE_INFO successfully."
        else
            err "Failed to generate coverage.info via lcov."
            err ""
            diagnose_coverage_build
            err ""
            err "Skip this check: SKIP_COVERAGE_CHECK=1 git push"
            exit 1
        fi
    else
        err "Coverage data not found: $COVERAGE_INFO"
        err ""
        diagnose_coverage_build
        err ""
        err "Skip this check: SKIP_COVERAGE_CHECK=1 git push"
        exit 1
    fi
fi

# Warn if ORCA .gcno files are missing
if [ -d src/backend/gporca ]; then
    orca_gcno_count=$(find src/backend/gporca -name '*.gcno' 2>/dev/null | wc -l || echo 0)
else
    orca_gcno_count=0
fi
if [ "$orca_gcno_count" -eq 0 ] && [ -d src/backend/gporca ]; then
    log "WARNING: no .gcno files in src/backend/gporca/ -- C++ coverage data may be missing."
    log "Ensure you configured with: CXXFLAGS='--coverage'"
fi

# ---------- 4. Parse lcov data ----------

COV_DATA="$TMPDIR_BASE/cov_data"
REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || echo "")"
awk -v root="$REPO_ROOT" '
    /^SF:/ {
        file = substr($0, 4)
        if (root != "" && index(file, root "/") == 1)
            file = substr(file, length(root) + 2)
    }
    /^DA:/ {
        split(substr($0, 4), a, ",")
        print file ":" a[1] ":" a[2]
    }
' "$COVERAGE_INFO" > "$COV_DATA"

# ---------- 5. Process each pushed ref ----------

overall_pass=true

while read -r local_ref local_sha remote_ref remote_sha; do
    [ "$local_sha" = "$ZERO_SHA" ] && continue

    range=$(compute_range "$local_sha" "$remote_sha") || {
        log "Cannot determine base commit, skipping $local_ref"
        continue
    }

    log "Checking range: $range ($local_ref)"

    # -- 6. Extract changed lines --
    CHANGED_LINES="$TMPDIR_BASE/changed_lines"
    git diff "$range" --unified=0 --diff-filter=AM -- '*.c' '*.cpp' '*.h' '*.hpp' \
    | awk '
        /^\+\+\+ / { file = substr($2, 3) }
        /^@@ / {
            n = split($3, a, /[,+]/)
            start = a[2] + 0
            count = (n >= 3 && a[3] != "") ? a[3] + 0 : 1
            for (i = 0; i < count; i++)
                print file ":" (start + i)
        }
    ' | grep -Ev "$EXCLUDE_PATTERNS" > "$CHANGED_LINES" || true

    total=$(wc -l < "$CHANGED_LINES")
    if [ "$total" -eq 0 ]; then
        log "No new C/C++ lines in this ref, skipping."
        continue
    fi

    # -- 7. Cross-reference with coverage data --
    UNCOVERED_REPORT="$TMPDIR_BASE/uncovered"
    MATCH_RESULT="$TMPDIR_BASE/match_result"

    # Build set of files that appear in coverage data
    COV_FILES="$TMPDIR_BASE/cov_files"
    awk -F: '{ print $1 }' "$COV_DATA" | sort -u > "$COV_FILES"

    awk -F: '
        pass == 1 {
            # COV_FILES: files present in coverage data
            known_files[$1] = 1
            next
        }
        pass == 2 {
            # COV_DATA: file:line:hit_count
            key = $1 ":" $2
            if (!(key in cov) || $3 + 0 > cov[key] + 0)
                cov[key] = $3
            next
        }
        pass == 3 {
            # CHANGED_LINES: file:line
            key = $1 ":" $2
            if ($1 in known_files) {
                # File has coverage data
                if (key in cov) {
                    if (cov[key] + 0 > 0) covered++
                    else {
                        uncovered++
                        print "  " $1 ":" $2 > uncov_file
                    }
                } else {
                    # Line not executable (comment, declaration, etc.)
                    non_exec++
                }
            } else if ($1 ~ /\.(h|hpp)$/) {
                # Header not in coverage data -- declarations are non-executable
                non_exec++
            } else {
                # Source file not in coverage data at all -- treat as uncovered
                uncovered++
                print "  " $1 ":" $2 > uncov_file
            }
        }
        END {
            print covered + 0, uncovered + 0, non_exec + 0
        }
    ' uncov_file="$UNCOVERED_REPORT" \
      pass=1 "$COV_FILES" \
      pass=2 "$COV_DATA" \
      pass=3 "$CHANGED_LINES" > "$MATCH_RESULT"

    read -r covered uncovered_count non_executable < "$MATCH_RESULT"

    executable=$((total - non_executable))

    if [ "$executable" -eq 0 ]; then
        log "All new lines are non-executable, skipping."
        continue
    fi

    pct=$((covered * 100 / executable))

    # -- 8. Report --
    echo ""
    echo "=================================================="
    echo " Incremental Coverage Report"
    echo "=================================================="
    echo " Branch:           $local_ref"
    echo " Commit range:     $range"
    echo " New lines:        $total"
    echo " Executable lines: $executable"
    echo " Covered:          $covered"
    echo " Uncovered:        $uncovered_count"
    echo " Coverage:         ${pct}%"
    echo " Threshold:        ${COVERAGE_THRESHOLD}%"
    echo "=================================================="

    if [ "$pct" -lt "$COVERAGE_THRESHOLD" ]; then
        echo ""
        echo "FAILED: coverage ${pct}% < threshold ${COVERAGE_THRESHOLD}%"
        echo ""
        echo "Uncovered lines:"
        cat "$UNCOVERED_REPORT"
        echo ""

        # Secondary diagnosis: many lines with no coverage data at all
        if [ "$total" -gt 0 ] && [ "$non_executable" -gt 0 ]; then
            no_data_ratio=$((non_executable * 100 / total))
            if [ "$no_data_ratio" -gt 80 ]; then
                echo "WARNING: ${no_data_ratio}% of new lines have no coverage data."
                echo "This usually means the project was not compiled with --coverage."
                echo ""
                diagnose_coverage_build
                echo ""
            fi
        fi

        echo "How to fix:"
        echo "  1. Add tests to cover the uncovered lines listed above."
        echo "  2. Verify build: ./configure --enable-coverage CXXFLAGS='--coverage'"
        echo "  3. Skip this check: SKIP_COVERAGE_CHECK=1 git push"
        overall_pass=false
    else
        echo "PASSED"
    fi
done < "$REFS_FILE"

if [ "$overall_pass" = false ]; then
    exit 1
fi

exit 0
