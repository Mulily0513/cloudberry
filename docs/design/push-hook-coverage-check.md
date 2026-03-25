# Design Document: Pre-push Hook for Incremental Test Coverage Check

## 1. Background and Goals

Before code is pushed to a remote repository, automatically check the test
coverage of **newly added or modified code lines** to ensure:

1. Coverage data (`.gcda` files) exists for the new code.
2. Line coverage of new code is not below a configurable threshold (default 80%).
3. Both C code (`*.c`) and the ORCA optimizer C++ code (`*.cpp`) are covered.

When the threshold is not met the push is blocked and a report is printed.
Developers can bypass the check via an environment variable.

## 2. Architecture Overview

```
git clone -> ./configure -> make        # Automatic activation phase
  |              |            |
  |              |            +-- make all depends on setup-git-hooks
  |              +-- After AC_OUTPUT, sets core.hooksPath
  +-- Fetches scripts/hooks/ directory
                      |
                      v
                core.hooksPath = scripts/hooks  (written to local git config)
                      |
                      |  ... developer codes ...
                      |
                      v
git push
  |
  v
scripts/hooks/pre-push               # Pointed to by core.hooksPath, version-controlled
  |
  v
scripts/hooks/check-coverage.sh      # Main coverage check logic
  |
  +-- 1. Parse pre-push parameters, determine commit range
  +-- 2. Detect whether any C/C++ files were changed
  |      +-- No C/C++ changes -> allow push immediately (skip coverage check)
  +-- 3. git diff to extract new/modified C/C++ lines
  +-- 4. Parse lcov/gcov coverage data
  +-- 5. Compute incremental coverage
  +-- 6. Report results
```

**Fast-skip principle**: if all commits in a push only modify scripts (Bash,
Python, Perl), documentation (Markdown, SGML, XML), SQL, configuration files,
or other non-C/C++ files, then the entire coverage check is skipped.  No
`coverage.info` is required, no warnings or errors are produced, and the push
is allowed immediately.

## 3. Detailed Design

### 3.1 pre-push Hook Entry Point

The `pre-push` hook is called automatically by Git on `git push`.  It receives
refs on stdin in the following format:

```
<local_ref> <local_sha> <remote_ref> <remote_sha>
```

**Hook script (`scripts/hooks/pre-push`)**:

```bash
#!/bin/bash
# Git pre-push hook: incremental coverage check
#
# Skip: SKIP_COVERAGE_CHECK=1 git push

if [ "${SKIP_COVERAGE_CHECK}" = "1" ]; then
    echo "[coverage-check] Skipped by SKIP_COVERAGE_CHECK=1"
    exit 0
fi

HOOK_DIR="$(cd "$(dirname "$0")" && pwd)"
exec "$HOOK_DIR/check-coverage.sh" "$@"
```

### 3.2 Main Check Script Logic

**File**: `scripts/hooks/check-coverage.sh`

#### 3.2.0 Fast Skip: Non-C/C++ Change Detection

Before loading any coverage data, check whether the pushed commits contain
C/C++ source file changes.  If all changed files are non-C/C++ (scripts,
docs, SQL, config, etc.), skip the entire coverage check.
**No `coverage.info` is required and no warnings are produced.**

```bash
# Check whether the commit range contains C/C++ changes.
# Returns 0 if there are C/C++ changes, 1 otherwise.
has_c_cpp_changes() {
    local range="$1"
    local changed_files
    changed_files=$(git diff "$range" --name-only --diff-filter=AM -- '*.c' '*.cpp' '*.h')
    [ -n "$changed_files" ]
}
```

This check runs after determining the commit range and before loading
coverage data.  Typical scenarios that skip the check:

| Change type | File extension examples | Coverage check? |
|-------------|------------------------|----------------|
| Bash scripts | `*.sh` | No |
| Python scripts | `*.py` | No |
| Perl scripts | `*.pl`, `*.pm` | No |
| Documentation | `*.md`, `*.sgml`, `*.xml`, `*.txt` | No |
| SQL files | `*.sql` | No |
| Expected test output | `expected/*.out` | No |
| Configuration files | `*.yaml`, `*.yml`, `*.json`, `*.conf` | No |
| Makefile / CMake | `Makefile`, `*.mk`, `CMakeLists.txt` | No |
| C/C++ source code | `*.c`, `*.cpp` | **Yes** |
| C/C++ headers | `*.h` | **Yes** (optional) |
| Mixed changes (incl. C/C++) | `*.py` + `*.c` | **Yes** |

> **Note**: as long as **any** C/C++ file is changed, the coverage check runs
> (only the C/C++ portion is checked).  The check is skipped only when
> **all** changes are non-C/C++ files.

#### 3.2.1 Parameter Parsing and Commit Range Determination

```bash
# Read ref info from stdin (passed by the pre-push hook)
while read local_ref local_sha remote_ref remote_sha; do
    if [ "$remote_sha" = "0000000000000000000000000000000000000000" ]; then
        # New branch: compare against main
        range="$(git merge-base HEAD main)..${local_sha}"
    else
        # Existing branch: only check new commits
        range="${remote_sha}..${local_sha}"
    fi
done
```

Key points:
- Supports pushing multiple refs (loops over each).
- Uses `merge-base` for new branches.
- Branch deletions (`local_sha` all zeros) are skipped.

#### 3.2.2 Extracting New/Modified Code Lines

Use `git diff` to extract new line numbers, covering both C and C++ source files:

```bash
# Extract all new/modified C/C++ source lines.
# Output format: file_path:line_number
extract_changed_lines() {
    local range="$1"
    # Match *.c and *.cpp (covers ORCA C++ code)
    git diff "$range" --unified=0 --diff-filter=AM -- '*.c' '*.cpp' '*.h' \
    | awk '
        /^--- /{ next }
        /^\+\+\+ /{ file = substr($2, 3) }  # strip b/ prefix
        /^@@ /{
            split($3, a, /[,+]/)
            start = a[2]
            count = (a[3] == "" ? 1 : a[3])
            for (i = 0; i < count; i++) {
                print file ":" (start + i)
            }
        }
    '
}
```

Filtering rules:
- Checks `*.c` and `*.cpp` files (headers are optional).
- C++ files mainly come from the ORCA optimizer (~1085 `.cpp` files under
  `src/backend/gporca/` across `libgpos`, `libgpopt`, `libnaucrates`,
  `libgpdbcost`).
- Auto-generated files (`*_scanner.c`, `*_gram.c`, etc.) are excluded.
- Pure comment and blank lines may optionally be excluded.

#### 3.2.3 Obtaining Coverage Data

The project already supports the `--enable-coverage` configure option (based
on gcc `--coverage`).  Compilation produces `.gcno` files; running tests
produces `.gcda` files.

**C code**: enabled via `./configure --enable-coverage`.  The Make build
system automatically passes `-fprofile-arcs -ftest-coverage`.

**ORCA C++ code**: ORCA uses CMake (`src/backend/gporca/CMakeLists.txt`) and
requires the coverage flags to be passed explicitly:

```bash
# Option 1: via CXXFLAGS (set at configure time)
./configure --enable-coverage CXXFLAGS="--coverage" --prefix=$PREFIX

# Option 2: via CMake variables (if building ORCA standalone)
cmake -DCMAKE_CXX_FLAGS="--coverage" -DCMAKE_EXE_LINKER_FLAGS="--coverage" ..
```

> **Note**: `--coverage` is a GCC/Clang shorthand equivalent to
> `-fprofile-arcs -ftest-coverage` at compile time and `-lgcov` at link time.
> C and C++ code produce `.gcno`/`.gcda` files in the same format, and lcov
> can parse both.

**Approach A: lcov tracefile (recommended)**

```bash
# Prerequisite: tests have been run, lcov_test.info exists
# Or run before push:
#   make check  (produces .gcda for both C and C++)
#   lcov --capture --directory . --output-file coverage.info

COVERAGE_INFO="${COVERAGE_INFO:-coverage.info}"

if [ ! -f "$COVERAGE_INFO" ]; then
    echo "[coverage-check] ERROR: coverage data not found: $COVERAGE_INFO"
    echo ""
    # Diagnose: check whether .gcno files exist to determine build config
    c_gcno_count=$(find src -path '*/gporca' -prune -o -name '*.gcno' -print 2>/dev/null | wc -l)
    cpp_gcno_count=$(find src/backend/gporca -name '*.gcno' 2>/dev/null | wc -l)

    if [ "$c_gcno_count" -eq 0 ] && [ "$cpp_gcno_count" -eq 0 ]; then
        echo "Cause: no .gcno files found -- project was not compiled with coverage."
        echo ""
        echo "Please re-configure and rebuild:"
        echo "  ./configure --enable-coverage CXXFLAGS='--coverage' --prefix=\$PREFIX"
        echo "  make clean && make -j\$(nproc) && make install"
    elif [ "$c_gcno_count" -gt 0 ] && [ "$cpp_gcno_count" -eq 0 ]; then
        echo "Cause: C code has coverage enabled, but ORCA C++ code does not."
        echo ""
        echo "Please re-configure with CXXFLAGS='--coverage':"
        echo "  ./configure --enable-coverage CXXFLAGS='--coverage' --prefix=\$PREFIX"
        echo "  make clean && make -j\$(nproc) && make install"
    elif [ "$c_gcno_count" -eq 0 ] && [ "$cpp_gcno_count" -gt 0 ]; then
        echo "Cause: ORCA C++ code has coverage enabled, but C code does not."
        echo ""
        echo "Please re-configure with --enable-coverage:"
        echo "  ./configure --enable-coverage CXXFLAGS='--coverage' --prefix=\$PREFIX"
        echo "  make clean && make -j\$(nproc) && make install"
    else
        echo ".gcno files exist but coverage data file (.gcda) is missing."
        echo "Likely cause: tests have not been run after building."
    fi
    echo ""
    echo "After building, run tests and generate coverage data:"
    echo "  make check"
    echo "  lcov --capture --directory src --output-file coverage.info"
    exit 1
fi
```

lcov `.info` file format:

```
SF:/path/to/file.c        # source file
DA:10,5                    # line 10, executed 5 times
DA:11,0                    # line 11, not executed
end_of_record
```

```bash
# Parse lcov info file, build file:line -> hit_count mapping
parse_coverage_info() {
    local info_file="$1"
    awk -F: '
        /^SF:/ { file = substr($0, 4) }
        /^DA:/ {
            split(substr($0, 4), a, ",")
            line = a[1]
            hits = a[2]
            print file ":" line ":" hits
        }
    ' "$info_file"
}
```

**Approach B: direct gcov parsing**

For environments without lcov, run `gcov` directly on `.gcda` files:

```bash
# Run gcov on each changed file (works for both C and C++)
for src_file in "${changed_files[@]}"; do
    case "$src_file" in
        *.c)   gcno_file="${src_file%.c}.gcno" ;;
        *.cpp) gcno_file="${src_file%.cpp}.gcno" ;;
        *)     continue ;;
    esac
    if [ -f "$gcno_file" ]; then
        gcov -b -p "$src_file" 2>/dev/null
    fi
done
```

#### 3.2.4 Incremental Coverage Calculation

```bash
# Core logic: cross-reference new lines with coverage data
calculate_incremental_coverage() {
    local changed_lines_file="$1"    # file:line format
    local coverage_data_file="$2"    # file:line:hits format

    local total=0
    local covered=0
    local uncovered_lines=""

    while IFS=: read -r file line; do
        total=$((total + 1))

        hits=$(grep "^${file}:${line}:" "$coverage_data_file" \
               | cut -d: -f3)

        if [ -z "$hits" ]; then
            # No coverage data (likely a declaration / non-executable line)
            total=$((total - 1))
        elif [ "$hits" -gt 0 ]; then
            covered=$((covered + 1))
        else
            uncovered_lines="${uncovered_lines}\n  ${file}:${line}"
        fi
    done < "$changed_lines_file"

    if [ "$total" -eq 0 ]; then
        echo "100"  # no executable lines -> pass
        return
    fi

    local pct=$((covered * 100 / total))
    echo "$pct"

    if [ -n "$uncovered_lines" ]; then
        echo -e "\n[coverage-check] Uncovered new lines:${uncovered_lines}" >&2
    fi
}
```

#### 3.2.5 Result Determination and Output

```bash
COVERAGE_THRESHOLD="${COVERAGE_THRESHOLD:-80}"

coverage_pct=$(calculate_incremental_coverage \
    "$changed_lines_file" "$coverage_data_file")

echo "=================================================="
echo " Incremental Coverage Report"
echo "=================================================="
echo " Commit range:     ${range}"
echo " Changed files:    $(wc -l < "$changed_files_list")"
echo " Executable lines: ${total_lines}"
echo " Covered lines:    ${covered_lines}"
echo " Coverage:         ${coverage_pct}%"
echo " Threshold:        ${COVERAGE_THRESHOLD}%"
echo "=================================================="

if [ "$coverage_pct" -lt "$COVERAGE_THRESHOLD" ]; then
    echo ""
    echo "FAILED: coverage ${coverage_pct}% < threshold ${COVERAGE_THRESHOLD}%"
    echo ""

    # Diagnose: if many lines have no coverage data, the build may lack --coverage
    if [ "$non_executable_ratio" -gt 80 ]; then
        c_gcno=$(find src -path '*/gporca' -prune -o -name '*.gcno' -print 2>/dev/null | wc -l)
        cpp_gcno=$(find src/backend/gporca -name '*.gcno' 2>/dev/null | wc -l)
        if [ "$c_gcno" -eq 0 ] || [ "$cpp_gcno" -eq 0 ]; then
            echo "Note: many new lines have no coverage data at all."
            echo "Please verify your configure command includes:"
            echo "  ./configure --enable-coverage CXXFLAGS='--coverage'"
            echo ""
        fi
    fi

    echo "How to fix:"
    echo "  1. Add tests to cover the uncovered lines listed above."
    echo "  2. Verify build: ./configure --enable-coverage CXXFLAGS='--coverage'"
    echo "  3. Skip: SKIP_COVERAGE_CHECK=1 git push"
    echo "  4. Lower threshold: COVERAGE_THRESHOLD=60 git push"
    exit 1
fi

echo "PASSED"
exit 0
```

### 3.3 Configuration

| Environment variable | Default | Description |
|---------------------|---------|-------------|
| `SKIP_COVERAGE_CHECK` | `0` | Set to `1` to skip the check |
| `COVERAGE_THRESHOLD` | `80` | Minimum coverage percentage |
| `COVERAGE_INFO` | `coverage.info` | Path to lcov tracefile |
| `COVERAGE_CHECK_EXTENSIONS` | `*.c *.cpp` | File extensions to check (space-separated) |
| `COVERAGE_EXCLUDE_PATTERNS` | `*_gram.c\|*_scan.c` | Exclude file patterns |

### 3.4 File Exclusion Rules

The following files are excluded from coverage checking by default:

```bash
EXCLUDE_PATTERNS=(
    # ---- C code exclusions ----
    "*/gram.c"                       # bison-generated
    "*/scan.c"                       # flex-generated
    "*_scanner.c"                    # flex-generated
    "src/backend/snowball/*"         # third-party: snowball stemmer
    "src/backend/regex/*"            # third-party: regex library
    "src/timezone/*"                 # third-party: timezone data
    "contrib/*/expected/*"           # test expected output

    # ---- ORCA C++ code exclusions ----
    "src/backend/gporca/data/*"      # test data / XML
    "src/backend/gporca/server/*"    # standalone ORCA server (non-production)
    "src/backend/gporca/concourse/*" # CI scripts
)
```

### 3.5 Special Considerations for ORCA C++ Code

The ORCA optimizer lives under `src/backend/gporca/` and is written in C++
with four sub-libraries:

| Library | Path | Description |
|---------|------|-------------|
| `libgpos` | `src/backend/gporca/libgpos/` | Base library (memory, sync, exceptions) |
| `libgpopt` | `src/backend/gporca/libgpopt/` | Optimizer core (operators, xforms, search) |
| `libnaucrates` | `src/backend/gporca/libnaucrates/` | Metadata and statistics (DXL, MD) |
| `libgpdbcost` | `src/backend/gporca/libgpdbcost/` | Cost model |

**Differences from C code and handling:**

1. **Different build system**: ORCA uses CMake (invoked by the outer Make).
   `--enable-coverage` only affects C code.  `CXXFLAGS="--coverage"` must be
   passed to propagate coverage flags to CMake.

2. **`.gcno`/`.gcda` paths**: ORCA's `.gcno` files are generated in the CMake
   build directory (typically `CMakeFiles/` subdirectories under
   `src/backend/gporca/`), not alongside source files.  lcov's `--directory`
   flag recursively scans and finds them automatically.

3. **Templates and inline functions**: ORCA heavily uses C++ templates and
   header-defined inline functions.  In lcov these may:
   - Have no `DA` records (not instantiated) -> treated as non-executable
     lines, excluded from coverage statistics.
   - Have multiple `DA` records from multiple instantiations -> lcov
     automatically merges them, taking the maximum hit count.

4. **Exception handling paths**: ORCA uses C++ exceptions
   (`gpos::CException`), and catch blocks are typically hard to cover with
   regular tests.  For such defensive code, specific patterns can be excluded
   or a separate ORCA threshold can be configured:
   ```bash
   # Optional: set a separate threshold for ORCA
   ORCA_COVERAGE_THRESHOLD="${ORCA_COVERAGE_THRESHOLD:-${COVERAGE_THRESHOLD}}"
   ```

5. **Coverage data collection command**:
   ```bash
   # Include the ORCA build directory when collecting
   lcov --capture \
       --directory src/backend \
       --directory src/backend/gporca \
       --output-file coverage.info
   ```

### 3.6 Automatic Activation: Works from Clone

**Goal**: new developers get the pre-push hook automatically after cloning
the repository, without any manual setup.

#### 3.6.1 Approach Selection

| Approach | Automatic | Intrusiveness | Compatibility | Selected |
|----------|-----------|---------------|---------------|----------|
| A. Manually copy hook to `.git/hooks/` | Poor (needs docs) | None | Good | No |
| B. `Makefile` sets `core.hooksPath` | **Good (build-time)** | Low | Good | No |
| C. `configure` sets it at config time | Good | Medium | Good | No |
| D. **In-repo hooks dir + build auto-activation** | **Good (build-time)** | **Low** | **Good** | **Yes** |
| E. Git template directory (global config) | Poor (global setup) | High | Poor | No |

**Selected approach D**: store hook scripts in `scripts/hooks/` (version-
controlled in the repository) and automatically run
`git config core.hooksPath scripts/hooks` during the build process.

#### 3.6.2 Implementation

**Core idea**: on each developer's first build (or every `./configure`), the
local git config is set to point `core.hooksPath` at the in-repo
`scripts/hooks/` directory.

**Layer 1: In-repo hook directory structure**

```
scripts/
└── hooks/
    ├── pre-push              # Coverage check entry point
    └── check-coverage.sh     # Coverage analysis logic
```

All hook scripts are committed to the repository and version-controlled.
Every developer gets the same version of the hooks automatically.

**Layer 2: GNUmakefile.in auto-activation**

A `setup-git-hooks` target is added to the top-level `GNUmakefile.in` and
made a dependency of `all`:

```makefile
# Automatically configure git hooks (only inside a git repository)
.PHONY: setup-git-hooks
setup-git-hooks:
	@if [ -d .git ] && [ -d scripts/hooks ]; then \
	    current=$$(git config --local core.hooksPath 2>/dev/null); \
	    if [ "$$current" != "scripts/hooks" ]; then \
	        git config --local core.hooksPath scripts/hooks; \
	        echo "Git hooks activated: core.hooksPath = scripts/hooks"; \
	    fi \
	fi

all: setup-git-hooks
```

**Activation timing**:

| Developer action | Triggers hook install? | Notes |
|-----------------|----------------------|-------|
| `git clone` | No | Clone alone does not trigger |
| `./configure && make` | **Yes** | `make all` depends on `setup-git-hooks` |
| `make` (incremental) | **Yes** | Checked every time (idempotent, no overhead) |
| `make install` | No | But `make` is usually run first |
| `make clean` | No | Cleanup does not affect git config |

Key properties:

1. **Idempotent**: if `core.hooksPath` is already set correctly, nothing
   happens and no output is produced.
2. **Git-repo only**: checks for `.git` directory; tarball-extracted sources
   are not affected.
3. **Local config**: uses `--local`, only affects the current repo, does not
   pollute the user's global git config.
4. **Non-blocking**: `setup-git-hooks` failure does not break the build
   (`@` prefix + conditional guards).

**Layer 3: configure-time supplementary activation (optional)**

As a secondary activation point, `configure.ac` runs the same setup after
`AC_OUTPUT`, so that hooks are active right after `./configure` (before
`make` is even run):

```bash
# Appended after AC_OUTPUT in configure.ac:
if test -d "$srcdir/.git" && test -d "$srcdir/scripts/hooks"; then
    echo "Configuring git hooks..."
    git -C "$srcdir" config --local core.hooksPath scripts/hooks 2>/dev/null
fi
```

#### 3.6.3 Developer Experience

**New developer (zero configuration)**:

```bash
git clone <repo-url>
cd hashdata-lightning
./configure --prefix=$PREFIX    # <- hooks activated here
make -j$(nproc)                 # <- hooks confirmed here (double guarantee)

# Every push now automatically checks coverage -- no extra steps needed
git push origin my-branch
```

**Existing developer (has a clone)**:

Hooks are activated on the next `make` run, transparently.

**Temporarily disabling hooks**:

```bash
# Option 1: skip coverage check for one push
SKIP_COVERAGE_CHECK=1 git push

# Option 2: fully disable custom hooks (restore git default behavior)
git config --local --unset core.hooksPath

# Option 3: skip all hooks (not recommended, affects all hooks)
git push --no-verify
```

#### 3.6.4 Version Control and Updates

Hook scripts live in `scripts/hooks/` and are version-controlled with the code:

- **Updating hook logic**: modify `scripts/hooks/pre-push` or
  `scripts/hooks/check-coverage.sh`, commit, and all developers get the
  update on their next `git pull`.  No reinstallation needed.
- **Rolling back**: `git checkout <old-commit> -- scripts/hooks/` restores
  a previous version.
- **Code review**: hook changes go through the normal MR review process,
  treated the same as production code.

Comparison with the traditional `.git/hooks/` approach:

| Aspect | `.git/hooks/` (traditional) | `scripts/hooks/` (this design) |
|--------|-----------------------------|-------------------------------|
| Version control | Not tracked by git | Version-controlled with code |
| Multi-developer sync | Manual distribution/updates | `git pull` syncs automatically |
| New developer onboarding | Must read docs and install manually | Works after `make` |
| Consistency | Developers may have different versions | Everyone uses the same version |
| Code review | None | Reviewed via MR |

## 4. Usage

### 4.1 Initial Setup (automatic, no manual steps)

The normal build process completes the hook installation -- **no extra steps
are needed**:

```bash
git clone <repo-url>
cd hashdata-lightning
./configure --prefix=$PREFIX    # hooks activated (configure phase)
make -j$(nproc)                 # hooks confirmed (make phase, double guarantee)
```

> To verify hooks are active:
> ```bash
> git config --local core.hooksPath
> # Expected output: scripts/hooks
> ```

### 4.2 Daily Development Workflow

```bash
# 1. Enable coverage build (first time or after clean)
#    CXXFLAGS="--coverage" ensures ORCA C++ code also has coverage
./configure --enable-coverage --enable-debug CXXFLAGS="--coverage" --prefix=$PREFIX
make -j$(nproc) && make install

# 2. Develop normally, write code and tests

# 3. Run tests to generate coverage data
make check                    # or targeted: cd src/test/regress && make check
lcov --capture --directory src --output-file coverage.info

# 4. Push triggers automatic check
git push origin feature-branch
# Automatically triggers pre-push hook -> check-coverage.sh
```

### 4.3 CI Integration (optional enhancement)

Add a coverage stage in `.gitlab-ci.yml` as a server-side double-check:

```yaml
coverage-check:
  stage: test
  script:
    - ./configure --enable-coverage CXXFLAGS="--coverage" && make -j$(nproc)
    - make check
    - lcov --capture --directory src --output-file coverage.info
    - scripts/hooks/check-coverage.sh --ci-mode
  rules:
    - if: $CI_PIPELINE_SOURCE == "merge_request_event"
  artifacts:
    reports:
      coverage_report:
        coverage_format: cobertura
        path: coverage.xml
```

## 5. Edge Cases

| Scenario | Handling |
|----------|----------|
| `--enable-coverage` not used (C code) | Detects missing `.gcno` files, reports error with `./configure --enable-coverage CXXFLAGS='--coverage'` hint |
| `--coverage` not used (ORCA C++ code) | Detects no `.gcno` in ORCA directory, suggests `CXXFLAGS='--coverage'` |
| C and C++ coverage partially enabled | Detects `.gcno` separately for each, gives targeted configure advice |
| No `.gcda` files (built but tests not run) | Detects `.gcno` present but no `.gcda`, suggests `make check` |
| Coverage check fails with many lines having no data | Secondary diagnosis checks `.gcno` existence, distinguishes "missing tests" from "missing build flags" |
| All new lines are non-executable (comments/declarations) | Treated as pass (100% coverage) |
| Pushing multiple branches | Each ref checked independently; any failure blocks the entire push |
| Merge commits | Use `--first-parent` to filter; only check lines from the current branch |
| Only scripts/docs/SQL changed (no C/C++) | Early exit before loading coverage data; no check, no warning, no error |
| Mixed changes (C/C++ + scripts/docs) | Coverage check runs but only examines the C/C++ portion |
| ORCA C++ not built with `--coverage` | Detects no `.gcno` in `src/backend/gporca/`, warns with hint |
| ORCA template/generic code (header-only) | Lines with no lcov DA record treated as non-executable, excluded from stats |
| Force push (after rebase) | Works normally; range is based on old and new SHA |
| New developer clones but does not configure/make before push | Hook not activated, push is not blocked (no `core.hooksPath` set) |
| Source extracted from tarball (not a git repo) | Detects no `.git` directory, skips hook installation, build unaffected |
| Developer manually unsets core.hooksPath | Hook inactive; next `make` restores it automatically |
| Hook scripts updated in `scripts/hooks/` | Takes effect immediately after `git pull`; no reinstall needed |

## 6. Reference Implementation

See the actual implementation files:

- **`scripts/hooks/pre-push`** -- hook entry point
- **`scripts/hooks/check-coverage.sh`** -- main coverage analysis logic
- **`scripts/hooks/test-coverage-hook.sh`** -- end-to-end test suite (13
  tests, 19 assertions)

The implementation uses `set -euo pipefail` for robustness.  Key
implementation details beyond the pseudocode above:

- `find` commands on potentially nonexistent directories (e.g.,
  `src/backend/gporca`) are guarded with directory existence checks to avoid
  failures under `pipefail`.
- `grep` pipelines that may produce no matches use `|| true` to avoid
  `pipefail` errors.
- stdin is saved to a temp file (`$REFS_FILE`) on first read so it can be
  iterated twice (once for fast C/C++ detection, once for the actual check).
- The `compute_range()` helper function centralizes range calculation for
  both new and existing branches.

## 7. Limitations and Future Improvements

| Limitation | Improvement direction |
|------------|----------------------|
| Only supports C/C++ code | Extend to Python (coverage.py), PL/pgSQL |
| Depends on local coverage.info | CI integration, auto-trigger `make check` before push |
| Line coverage granularity | Upgrade to branch coverage (`lcov --rc lcov_branch_coverage=1`) |
| Per-line grep matching performance | Use `awk`/`sort+join` for large diffs |
| Path matching depends on consistent absolute paths | Add path normalization (`realpath`) logic |
