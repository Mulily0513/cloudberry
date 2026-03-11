# CLAUDE.md

Guidance for Claude Code when working in the Cloudberry Database repository.

## Overview

Apache Cloudberry (Incubating) is an open-source Massively Parallel Processing
(MPP) database built on PostgreSQL 14.4. It evolved from Greenplum Database and
is written primarily in C with supporting Python, Perl, Java, and PL/pgSQL.
Licensed under Apache 2.0.

## Build

This project uses GNU Autotools. A typical build flow:

```bash
./configure --prefix=$PREFIX    # configure with desired options
make -j$(nproc)                 # build all components
make install                    # install to prefix
```

Key configure options: `--with-includes`, `--with-libraries`, `--enable-debug`,
`--enable-cassert`, `--with-thirdparty`.

Building inside a development Docker container is supported via the
`devops/` scripts. Use the `/build-database` skill or `devops/build/` tools.

## Testing

```bash
# Main regression tests
cd src/test/regress && make check

# Isolation tests (concurrency)
cd src/test/isolation2 && make check

# Single-node regression tests
cd src/test/singlenode_regress && make check

# MCP server tests
cd mcp-server && ./run_tests.sh
```

Test files: SQL inputs in `sql/`, expected outputs in `expected/`.

## Project Structure

Each module has its own `CLAUDE.md` with detailed guidance. See links below.

### Core Source (`src/`)

| Directory | Description | Details |
|-----------|-------------|---------|
| [src/backend/](../src/backend/CLAUDE.md) | Database backend engine (parser, optimizer, executor, storage, catalog) | Cloudberry MPP: cdb/, gpopt/, gporca/, fts/ |
| [src/include/](../src/include/CLAUDE.md) | Header files (35+ subdirectories) | Node types, access methods, catalog defs, cdb/ headers |
| [src/bin/](../src/bin/CLAUDE.md) | Client tools & admin utilities | psql, pg_dump, initdb, pg_basebackup, gpfdist |
| [src/test/](../src/test/CLAUDE.md) | Test suites | regress, isolation, isolation2, recovery, TAP, unit |
| [src/common/](../src/common/CLAUDE.md) | Shared core utilities | Crypto, compression, JSON, encoding (frontend + backend) |
| [src/interfaces/](../src/interfaces/CLAUDE.md) | Client interface libraries | libpq, ecpg, gppc |
| [src/pl/](../src/pl/CLAUDE.md) | Procedural languages | PL/pgSQL, PL/Python, PL/Perl, PL/Tcl |
| [src/fe_utils/](../src/fe_utils/CLAUDE.md) | Frontend utilities | Connection helpers, parallel slots, output formatting |
| [src/port/](../src/port/CLAUDE.md) | Platform portability layer | OS abstractions, CRC32, signal handling |
| [src/tools/](../src/tools/CLAUDE.md) | Developer tools | pgindent, ctags, keyword checks |
| [src/makefiles/](../src/makefiles/CLAUDE.md) | Build system helpers | PGXS extension build infrastructure |
| [src/template/](../src/template/CLAUDE.md) | Platform config templates | Per-OS compiler flags (Linux, Darwin, etc.) |

### Extensions

| Directory | Description | Details |
|-----------|-------------|---------|
| [contrib/](../contrib/CLAUDE.md) | PostgreSQL & Cloudberry extensions (~70 modules) | PGXS-based; includes interconnect, datalake_fdw, cloudberry_fdw |
| [gpcontrib/](../gpcontrib/CLAUDE.md) | Greenplum/Cloudberry-specific extensions (~16 modules) | gp_toolkit, gpcloud, pxf_fdw, orafce, pg_hint_plan |

### Management & Infrastructure

| Directory | Description | Details |
|-----------|-------------|---------|
| [gpMgmt/](../gpMgmt/CLAUDE.md) | Cluster management tools (Python/Bash) | gpinitsystem, gpstart/gpstop, gpexpand, gpconfig |
| [mcp-server/](../mcp-server/CLAUDE.md) | MCP server for AI integration (Python) | 40+ tools, read-only SQL, FastMCP framework |
| [devops/](../devops/CLAUDE.md) | Build & deployment automation (Docker) | Build containers, sandbox, release packaging |
| [gpAux/](../gpAux/CLAUDE.md) | Greenplum auxiliary components | gpdemo, third-party deps, client packaging |
| [config/](../config/CLAUDE.md) | Autoconf macros | m4 macros for configure system |
| [doc/](../doc/CLAUDE.md) | Documentation sources (SGML/XML) | Manual pages, online docs |
| [licenses/](../licenses/CLAUDE.md) | Third-party license files | 30 license attributions |

## Coding Style

Follow PostgreSQL coding conventions. Key rules:

### Formatting

- **Tabs** for indentation in C/C++ code (tab width = 4).
- **Spaces** (4) for Python; **spaces** (1-2) for SGML/XML/XSL.
- **78 characters** maximum line length for C code.
- Enforced via `.editorconfig` in the repo root.

### C Code Conventions

- Function return type on its own line, then function name on the next.
- Variable declarations at the top of the block (C89 style).
- Use `/* */` comments, not `//` (for PostgreSQL < 17 compatibility).
- Comments should be full sentences, explain *why* not *what*.
- Naming: `lowercase_with_underscores` for functions, structs, variables.
- Use standard PostgreSQL file header with copyright and IDENTIFICATION.

### File Header Format

```c
/*-------------------------------------------------------------------------
 *
 * filename.c
 *    Brief description of what this file does.
 *
 * Portions Copyright (c) 2023-2025, HashData Technology Limited.
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *        src/backend/path/to/filename.c
 *-------------------------------------------------------------------------
 */
```

### Memory Management

- Use `palloc`/`pfree` with proper `MemoryContext`.
- Never pollute `TopMemoryContext` with per-query allocations.
- Always restore `CurrentMemoryContext` in `PG_CATCH` blocks.

### Error Handling

- Use `ereport()`/`elog()` with correct error levels (ERROR, WARNING, NOTICE).
- Error messages: lowercase primary message, no trailing period.
- `PG_TRY`/`PG_CATCH` blocks must clean up resources and restore state.
- Always set appropriate `errcode()`.

### Concurrency & MPP

- Follow established lock hierarchy to prevent deadlocks.
- Match every `LockAcquire` with `LockRelease`.
- For Cloudberry MPP code: handle QD-QE coordination, motion nodes, gang cleanup.
- Verify interconnect flow control and buffer management.

### Catalog Operations

- Every `SearchSysCache` must have a matching `ReleaseSysCache`.
- Handle cache invalidation via `RegisterSysCacheCallback`.
- Catalog changes must update `pg_depend` and handle dependencies.

## Commit Messages

Follow the `.gitmessage` template:

- **Title**: imperative verb, 50 chars max, no trailing period.
  - `Fix ...` for bugs, `Feature: ...` for new features, `Enhancement: ...` for optimizations, `Doc: ...` for docs.
- **Body**: explain *what*, *why*, and *how*. 72 chars max width.
- **Trailers**: `Co-authored-by`, `Reported-by`, `See: Issue#id` as needed.

## CI/CD

- **GitLab CI** (`.gitlab-ci.yml`): primary CI with build + test stages, multi-arch (aarch64, x86_64).
- **GitHub Actions** (`.github/workflows/`): mirrors with Coverity, SonarQube, Apache RAT audits.
- Pipelines auto-cancel on new commits. Artifacts retained for 1 week.

## Code Quality Tools

- `.clang-tidy` — static analysis for C++ (modernize, readability checks).
- `.clang-format` — formatting for C++ code (gpopt, gporca directories).
- `.editorconfig` — editor formatting rules.
- SonarQube and Coverity scans via CI.

## Custom Commands

- `/code-review` — Review code changes against PostgreSQL/Greenplum/Cloudberry standards.

## Key Domain Concepts

- **QD/QE**: Query Dispatcher (coordinator) / Query Executor (segment workers).
- **Motion nodes**: Data redistribution operators between segments.
- **Gang**: Group of QE processes that execute a slice of the query plan.
- **Interconnect**: Network layer for inter-segment communication.
- **ORCA**: Greenplum's cost-based query optimizer (C++, in `src/backend/gporca/`).
- **Slice table**: Mapping of plan fragments to gangs/segments.
