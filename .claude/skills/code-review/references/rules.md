# Code Review Rules — Per-Language Reference

> **Scope:** This file contains only rules that affect **correctness, safety,
> and build integrity**. Pure style, formatting, and naming rules are excluded
> — those are enforced by automated tools (pgindent, clang-format, clang-tidy,
> ruff, gofmt, checkstyle, perltidy). See SKILL.md "Do NOT comment on" section.

---

## C (PostgreSQL Core — `src/`, `contrib/`, `gpcontrib/`)

### Type Compatibility

- `C-TYPE-001`: Use PostgreSQL typedefs (`int32`, `uint64`, `Size`, `Oid`,
  `Datum`, `bool`) not C99 `<stdint.h>` types in core code. Mixing type
  systems causes sign-mismatch bugs and platform-dependent behavior.

### Memory & Safety

- `C-MEM-001`: Use `palloc`/`pfree` (not `malloc`/`free`) in backend code.
  Allocate in the appropriate memory context.
- `C-MEM-002`: Check for missing `pfree` in long-lived contexts. Per-tuple
  context resets handle cleanup automatically.
- `C-MEM-003`: Use `pg_malloc`/`pg_strdup` in frontend utilities (`src/bin/`).
- `C-MEM-004`: No unbounded `alloca()` or variable-length arrays on stack.

### Error Handling

- `C-ERR-001`: Use `ereport()`/`elog()` for errors. Never `fprintf(stderr)`
  in backend code.
- `C-ERR-002`: `ereport(ERROR)` is non-returning — no dead code after it.
- `C-ERR-003`: Use `PG_TRY`/`PG_CATCH`/`PG_FINALLY` for external resource
  cleanup on error paths.

### Concurrency & Distributed

- `C-CONC-001`: Shared memory access needs proper locking (LWLock, SpinLock,
  atomics). Flag unprotected shared state.
- `C-CONC-002`: Check QD vs QE context — dispatch-only ops must check
  `Gp_role == GP_ROLE_DISPATCH`.
- `C-CONC-003`: Don't hold heavy locks across network calls (interconnect,
  motion nodes) — causes distributed deadlocks.

### SQL Injection / Catalog Safety

- `C-SQL-001`: Use `quote_identifier()`/`quote_literal()` when building SQL
  in C. Never `sprintf` with unescaped user input.
- `C-SQL-002`: Catalog lookups must use `SearchSysCache`/`ScanKeyInit` with
  appropriate locks.

### PostgreSQL Conventions

- `C-PG-001`: New GUCs registered in `guc_tables.c` with proper bounds,
  hooks, and documentation.
- `C-PG-002`: Catalog changes require bumping `CATALOG_VERSION_NO` and
  updating `pg_*.dat`/`pg_*.h` consistently.
- `C-PG-003`: New system functions need `pg_proc.dat` entries with correct
  `provolatile`, `proparallel`, `prorettype`.

---

## C++ — ORCA/GPOPT (`src/backend/gporca/`, `src/backend/gpopt/`)

### Build Correctness

- `CPP-BUILD-001`: Include order: `postgres.h` must be first (it defines
  platform macros that affect all subsequent headers). Then project headers,
  then system headers. Incorrect order causes silent compilation issues.

### Safety (from clang-tidy — correctness subset only)

- `CPP-SAFE-001`: Use `override` on virtual overrides. Missing `override`
  hides bugs when the base class signature changes — the derived function
  silently becomes a new unrelated method instead of a compile error.
- `CPP-SAFE-002`: Use `nullptr` instead of `NULL` or `0` for pointers.
  `NULL`/`0` can cause incorrect overload resolution in C++ (e.g.,
  `f(int)` selected instead of `f(T*)`).

### ORCA-Specific

- `CPP-ORCA-001`: Use `CMemoryPool` with `GPOS_NEW`/`GPOS_DELETE`, not raw
  `new`/`delete`.
- `CPP-ORCA-002`: DXL serialization must be symmetric — every `Serialize`
  needs a corresponding parse path.
- `CPP-ORCA-003`: New operators/xforms must be registered in the enum and
  factory.
- `CPP-ORCA-004`: Mark non-mutating member functions `const`.

---

## C++ — PAX Storage (`contrib/pax_storage/`)

### Build Correctness

- `CPP-PAX-BUILD-001`: Include order: `postgres.h` first, then project/gtest
  headers, then system headers. Same rationale as `CPP-BUILD-001`.

---

## SQL (`src/test/`, `contrib/`, `gpcontrib/`)

### Test Framework Correctness

- `SQL-TEST-001`: Regression test files must have a corresponding `.out`
  expected-output file. New tests must be added to `schedule` or
  `parallel_schedule`. Missing either breaks the test framework.

### Safety

- `SQL-SAFE-001`: DDL in extensions must use transactions and
  `IF NOT EXISTS`/`IF EXISTS` guards.
- `SQL-SAFE-002`: No `SELECT *` in catalog queries — list columns explicitly
  for forward-compatibility.
- `SQL-SAFE-003`: Test SQL must clean up (`DROP TABLE IF EXISTS`, etc.).

### Distributed / Greenplum

- `SQL-GP-001`: `CREATE TABLE` should specify distribution policy
  (`DISTRIBUTED BY`, `DISTRIBUTED RANDOMLY`, `DISTRIBUTED REPLICATED`).
- `SQL-GP-002`: Test queries must use `ORDER BY` for deterministic output
  comparison across segments.
- `SQL-GP-003`: Check partition pruning correctness for partition-related
  changes.

---

## Python (`gpMgmt/`, `devops/`, `contrib/datalake_*/`)

### Safety

- `PY-SAFE-001`: No hardcoded passwords, tokens, or connection strings.
- `PY-SAFE-002`: Use `subprocess.run()` with list args, never `shell=True`
  with user input.
- `PY-SAFE-003`: Use parameterized queries, not string formatting for SQL.

### gpMgmt Conventions

- `PY-MGMT-001`: Management scripts must handle SIGINT/SIGTERM and clean up
  temp files.
- `PY-MGMT-002`: Use the project logging framework, not bare `print()`.
- `PY-MGMT-003`: New management commands need Behave tests in
  `gpMgmt/test/behave/`.

---

## Java (`contrib/datalake_agent/`, `contrib/apache-arrow/`)

### Safety

- `JAVA-SAFE-001`: Close resources with try-with-resources. Flag manual
  `close()` in finally blocks.
- `JAVA-SAFE-002`: No empty catch blocks — at minimum log the exception.
- `JAVA-SAFE-003`: Validate external input (paths, URLs, config) before use.

### Datalake-Specific

- `JAVA-DL-001`: Connectors must implement connection pooling and timeouts.
- `JAVA-DL-002`: Iceberg/Hudi operations must handle schema evolution —
  check column type mismatches.

---

## Go (`contrib/apache-arrow/`)

### Safety

- `GO-SAFE-001`: Always check returned errors. Flag `_` on error returns.
- `GO-SAFE-002`: Use `defer` for cleanup. Check LIFO ordering.
- `GO-SAFE-003`: Goroutines must have termination paths (context cancel,
  done channel). No goroutine leaks.

---

## Perl (`src/tools/`, `contrib/` TAP tests)

### Correctness

- `PERL-CORRECT-001`: `use strict; use warnings;` in all Perl files. Missing
  these pragmas silently hides real bugs (typos in variable names,
  uninitialized values).

### Safety

- `PERL-SAFE-001`: No backtick execution (`` `cmd` ``). Use `system()` or
  `IPC::Run` for subprocess execution.

### Conventions

- `PERL-CONV-001`: TAP tests use `Test::More` following PostgreSQL TAP
  conventions.

---

## Shell / Bash (`devops/`, `gpMgmt/`, scripts)

### Correctness

- `SH-CORRECT-001`: Use `#!/bin/bash` or `#!/usr/bin/env bash` shebang.
  Missing or wrong shebang causes the script to run under the wrong
  interpreter. Scripts must be executable.

### Safety

- `SH-SAFE-001`: `set -euo pipefail` at top (or document why not).
- `SH-SAFE-002`: Quote all variable expansions: `"$var"`, `"${array[@]}"`.
  Unquoted variables cause word splitting and glob expansion bugs.
- `SH-SAFE-003`: `mktemp` for temp files, `trap EXIT` for cleanup.
- `SH-SAFE-004`: No `eval` with user-controlled input.

### Conventions

- `SH-CONV-001`: Source `greenplum_path.sh` where required.
- `SH-CONV-002`: Consistent log format with timestamp and script name.
  Errors to `>&2`.
- `SH-CONV-003`: Support SIGTERM, clean up child processes.

---

## Makefile (`GNUmakefile*`, `Makefile*`, `*.mk`)

### Build Correctness

- `MAKE-BUILD-001`: Recipes must be indented with tabs. Spaces cause silent
  build failures (make syntax requirement).
- `MAKE-BUILD-002`: Follow PostgreSQL makefile patterns — `subdir`,
  `top_builddir`, `include $(top_builddir)/src/Makefile.global`. Incorrect
  patterns cause build or install failures.

---

## YAML / CI (`.github/`, `.gitlab-ci.yml`)

### CI Safety

- `CI-SAFE-001`: Pin GitHub Action versions to full SHA or major version tag.
  Never `@main`.
- `CI-SAFE-002`: Never log or echo secrets. Use masking.
- `CI-SAFE-003`: `set -e` in CI run steps.

---

## Protocol Buffers (`.proto`)

### Correctness

- `PROTO-CORRECT-001`: Reserve removed field numbers to prevent reuse.
  Reusing a deleted field number causes silent data corruption when old and
  new clients communicate.
