# gpcontrib/ — Greenplum/Cloudberry-Specific Extensions

16 extensions specifically for Cloudberry/Greenplum MPP functionality.

## Build

```bash
make -C gpcontrib
cd gpcontrib/gp_toolkit && make && make install && make installcheck
```

Same PGXS build pattern as `contrib/`.

## Key Extensions

### Cluster Management & Diagnostics
- **gp_toolkit** — Cluster diagnostics, resource group management, partition maintenance.
- **gp_internal_tools** — Session state inspection and internal cluster tools.
- **gp_distribution_policy** — Check and validate data distribution policies.
- **gp_replica_check** — Verify primary-mirror data consistency.
- **gp_inject_fault** — Fault injection for testing (development/QA only).
- **gp_debug_numsegments** — Control segment count for testing.

### External Data Access
- **gpcloud** — AWS S3 integration with `gpcheckcloud` utility.
- **pxf_fdw** — Federated query engine for Hadoop, Hive, HBase, S3, GCS, external databases.
- **gp_exttable_fdw** — External table FDW interface.

### Data Types & Compatibility
- **gp_sparse_vector** — Sparse vector datatype for ML/analytics workloads.
- **gp_legacy_string_agg** — Legacy string_agg behavior compatibility.
- **orafce** — Oracle compatibility functions.

### Query Optimization
- **pg_hint_plan** — Query plan hints (force specific join/scan methods).

### Compression
- **zstd** — Zstandard compression support.

## Naming Convention

Cloudberry-specific modules use the `gp_` prefix. Third-party ports (orafce, pg_hint_plan, zstd) keep their original names.
