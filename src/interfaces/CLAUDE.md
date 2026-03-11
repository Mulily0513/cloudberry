# src/interfaces/ — Client Interface Libraries

## Subdirectories

- **libpq/** — The official PostgreSQL C client library (37 files). Provides connection management, query execution, SSL/TLS, SCRAM auth. Used by all client tools.
- **ecpg/** — Embedded SQL in C preprocessor (11 files). Compiles SQL statements embedded in C source code.
- **gppc/** — Greenplum-specific interface (6 files). Additional APIs for Cloudberry/Greenplum extensions.

## Build

```bash
make -C src/interfaces          # Build all interfaces
make -C src/interfaces/libpq    # Build libpq only
```
