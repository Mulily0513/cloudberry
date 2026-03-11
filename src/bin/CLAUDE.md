# src/bin/ — Client Utilities & Administrative Tools

27+ command-line tools for database administration and client interaction.

## Key Tools

| Binary | Purpose |
|--------|---------|
| `psql` | Interactive SQL terminal |
| `pg_dump` / `pg_dumpall` | Logical backup |
| `pg_restore` | Restore from dump |
| `pg_basebackup` | Physical backup via streaming replication |
| `initdb` | Initialize a new database cluster |
| `pg_ctl` | Start, stop, restart the server |
| `pg_upgrade` | In-place major version upgrade |
| `pgbench` | Benchmarking tool |
| `pg_isready` | Connection check |
| `pg_rewind` | Resynchronize a diverged server |
| `pg_resetwal` | Reset WAL state |
| `pg_checksums` | Enable/verify data checksums |
| `gpfdist` | Greenplum file distribution server |

## Build

Each subdirectory has its own `Makefile`. Built as part of `make -C src/bin`.

## Conventions

- Tools share frontend utilities from `src/fe_utils/` and `src/common/`.
- Connection handling via `src/interfaces/libpq/`.
- Greenplum-specific tools (gpfdist) coexist with standard PostgreSQL tools.
