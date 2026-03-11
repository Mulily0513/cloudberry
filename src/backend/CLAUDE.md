# src/backend/ — Database Backend Engine

Core server-side code for the Cloudberry/PostgreSQL database engine.

## Build

```bash
# Built as part of top-level make; produces the `postgres` binary
make -C src/backend
```

Entry point: `main/main.c` dispatches to PostmasterMain, BackendMain, or BootStrapMain.
Subdirectories compile to `.a` archives linked into the final `postgres` executable.

## Query Processing Pipeline

```
SQL text → parser/ → optimizer/ → executor/ → results
```

1. **parser/** — Tokenizer (`scan.l`), grammar (`gram.y`), parse analysis (`analyze.c`).
2. **optimizer/** — Path generation (`path/`), plan creation (`plan/`), preprocessing (`prep/`), genetic optimizer (`geqo/`).
3. **executor/** — Demand-pull tuple pipeline. Key entry: `execMain.c` (ExecutorStart/Run/Finish/End).
4. **rewrite/** — Query rewriting (view expansion, INSTEAD OF rules).
5. **tcop/** — Top-level command processing and portal management.

## Data Access & Storage

- **access/** — Table and index access methods.
  - `heap/` — Standard heap tables.
  - `appendonly/` — Append-only tables (Cloudberry).
  - `aocs/` — Append-Only Column Store (Cloudberry).
  - `bitmap/` — Bitmap indexes (Cloudberry).
  - `nbtree/`, `hash/`, `gin/`, `gist/`, `spgist/`, `brin/` — Index types.
  - `external/` — External table access.
- **storage/** — Buffer manager (`buffer/`), lock manager (`lmgr/`), storage manager (`smgr/`), IPC, free space map.
- **catalog/** — System catalog operations (pg_class, pg_attribute, pg_type, etc.).
- **statistics/** — Column/relation statistics for cost estimation.

## Cloudberry/Greenplum-Specific Modules

- **cdb/** — Distributed query engine: QD-QE coordination, plan serialization/dispatch, motion nodes, interconnect, gang management, distributed snapshots.
- **gpopt/** — C++ wrapper bridging GPDB catalog to the ORCA optimizer (`gpdbwrappers.cpp`).
- **gporca/** — ORCA query optimizer engine (C++, CMake-based). Uses DXL for plan representation. Tests use XML minidumps.
- **fts/** — Fault Tolerance Service: monitors segment health, promotes mirrors on failure. Runs on coordinator only.
- **task/** — Background job scheduling (`pg_cron.c`).
- **crypto/** — Cluster file encryption (two-tier KEK/DEK model).

## Supporting Subsystems

- **commands/** — DDL/DML command processing (CREATE, ALTER, DROP, VACUUM, ANALYZE, EXPLAIN).
- **utils/** — Function manager (`fmgr/`), caching (`cache/`), ADT support (`adt/`), memory management (`mmgr/`), GUC configuration (`misc/`), resource management (`resgroup/`, `resscheduler/`), global deadlock detection (`gdd/`).
- **postmaster/** — Process management, background workers, signal handling.
- **replication/** — WAL streaming replication (walsender/walreceiver).
- **libpq/** — Frontend/backend protocol, SSL/TLS support.
- **foreign/** — Foreign Data Wrapper framework.
- **partitioning/** — Partition pruning and management.
- **jit/** — LLVM JIT compilation support.
- **nodes/** — Node creation, copy, equal, serialize/deserialize utilities.
- **port/** — Atomics, spinlocks, platform-specific code.
- **bootstrap/** — Initial catalog creation.
- **regex/**, **tsearch/**, **snowball/** — Regex, text search, and stemming.

## Conventions

- Each subdirectory has its own `Makefile` producing a `SUBSYS.o` or archive.
- Expression evaluation uses flat ExprState arrays (not recursive trees) for performance.
- Memory contexts follow a hierarchy; per-query contexts are created in ExecutorStart.
- Plan nodes and their state counterparts live in `nodes/` headers (`plannodes.h`, `execnodes.h`).
