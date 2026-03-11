# src/test/ — Test Suites

Comprehensive testing infrastructure for the Cloudberry database.

## Primary Test Suites

### regress/ — SQL Regression Tests (Main Suite)
```bash
cd src/test/regress
make installcheck-good                    # All tests (parallel + greenplum schedules)
make installcheck-cbdb                    # Cloudberry-only tests
make installcheck-cbdb-parallel           # Parallel query testing
make installcheck-orca-parallel           # ORCA optimizer tests
```
- SQL inputs in `sql/`, expected outputs in `expected/`.
- `.source` files in `input/` are preprocessed templates (e.g., UAO row/column variants).
- Schedules: `parallel_schedule` (PostgreSQL standard), `greenplum_schedule` (Cloudberry).
- `init_file` contains output masking patterns.

### isolation/ — Concurrent Transaction Tests (PostgreSQL Standard)
```bash
cd src/test/isolation && make installcheck
```
- Uses `.spec` files with multi-session syntax: `session`, `step`, `permutation`.
- Tests isolation levels, lock conflicts, deadlocks.
- Driven by `isolationtester` binary.

### isolation2/ — Distributed Concurrency Tests (Cloudberry)
```bash
cd src/test/isolation2
make installcheck-isolation2              # All isolation2 tests
make installcheck-resgroup                # Resource group tests
```
- Uses SQL files + Python framework (`sql_isolation_testcase.py`).
- Tests distributed snapshots, parallel retrieve cursors, segment failures.
- Multiple schedules: `isolation2_schedule`, `isolation2_crash_schedule`, `isolation2_resgroup_schedule`.

### singlenode_regress/ — Single-Node Regression
```bash
cd src/test/singlenode_regress && make installcheck-good-singlenode
```
- Same tests as regress/ but for single-node (no segments) deployments.

### singlenode_isolation2/ — Single-Node Concurrency Tests
- isolation2 variant for single-node deployments.

## Specialized Test Suites

| Directory | Type | Purpose |
|-----------|------|---------|
| `recovery/` | TAP (Perl) | Streaming replication, archiving, recovery targets, WAL |
| `walrep/` | SQL | Advanced WAL replication features |
| `authentication/` | TAP | Username/password, SCRAM auth |
| `ssl/` | TAP | Certificate validation, CRL revocation |
| `kerberos/` | TAP | GSSAPI/Kerberos auth (requires MIT Kerberos) |
| `ldap/` | TAP | LDAP authentication |
| `subscription/` | TAP | Logical replication |
| `modules/` | Mixed | Test extensions, hooks, C-level APIs |
| `unit/` | cmockery | C unit tests with mocked dependencies |
| `crypto/` | TAP | AES-GCM encryption, key rotation |
| `locale/` | SQL | Collation and character type tests |
| `mb/` | Shell | Multibyte encoding (run: `sh mbregress.sh`) |
| `performance/` | SQL | Load testing (`make perf-ao-load`) |
| `binary_swap/` | Mixed | Binary upgrade tests |

## TAP Test Infrastructure

TAP tests live in `t/*.pl` and use the shared Perl framework in `perl/`:
- `PostgresNode.pm` — Manages test database clusters.
- `TestLib.pm` — Common test utilities.
- Requires `--enable-tap-tests` at configure time.

```bash
cd src/test/recovery && make check
```

## File Conventions

- **Schedules**: Control test ordering and parallelism. Group size ~20 max to avoid max_connections limits.
- **Expected output variants**: `test_name_1.out`, `test_name_2.out` for platform/config differences.
- **Source templates**: `.source` files use `@variable@` substitution (e.g., `@abs_srcdir@`, `@amname@`).
- **Diff tool**: `gpdiff.pl` with masking/sorting via `atmsort.pm` and `explain.pm`.
