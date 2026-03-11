# contrib/ — PostgreSQL & Cloudberry Extensions

70+ extension modules, including standard PostgreSQL contrib and Cloudberry-specific additions.

## Build

```bash
# Build all enabled extensions (from top-level)
make -C contrib

# Build a single extension
cd contrib/hstore && make && make install

# Run extension regression tests
cd contrib/hstore && make installcheck
```

Extensions use the PGXS build system. Standard Makefile pattern:

```makefile
MODULE_big = extension_name
OBJS = file1.o file2.o
EXTENSION = extension_name
DATA = extension--1.0.sql
REGRESS = test_name

ifdef USE_PGXS
  PGXS := $(shell pg_config --pgxs)
  include $(PGXS)
else
  subdir = contrib/extension_name
  top_builddir = ../..
  include $(top_builddir)/src/Makefile.global
  include $(top_srcdir)/contrib/contrib-global.mk
endif
```

## Cloudberry-Specific Extensions

- **interconnect** — Inter-segment communication layer (tcp, udpifc, proxy). Must be preloaded.
- **cloudberry_fdw** — MPP-aware postgres_fdw with parallel retrieve cursors and distributed COPY.
- **datalake_fdw** — Access Hive, Iceberg, Hudi, Parquet, ORC, Avro; cloud storage (S3, OSS).
- **datalake_proxy**, **datalake_agent**, **datalake_apiary** — Datalake infrastructure.
- **hive_connector** — Direct Hive access.
- **unionstore_ext** — Cloud storage backend.
- **extprotocol** — Custom external table protocols.
- **formatter**, **formatter_fixedwidth** — Custom data formatters.

## Standard PostgreSQL Extensions (Subset)

`btree_gin`, `btree_gist`, `hstore`, `ltree`, `citext`, `intarray`, `bloom`,
`pg_buffercache`, `pg_stat_statements`, `pg_trgm`, `pgstattuple`,
`postgres_fdw`, `file_fdw`, `dblink`, `uuid-ossp`, `xml2`, etc.

## Conditional Build Flags

Controlled by `configure` options in `contrib/Makefile`:
- `enable_datalake` — Datalake suite (FDW, Hive, proxy, agent, apiary).
- `enable_vectorization` — Vectorization engine.
- `enable_pax` — PAX columnar storage format.
- `enable_ic_udp2` — UDP interconnect.
- `with_perl`, `with_python`, `with_ssl`, `with_uuid`, `with_libxml` — Optional dependencies.

## Conventions

- Each extension has a `.control` file (metadata: version, schema, relocatable).
- SQL migration scripts follow versioning: `extension--1.0.sql`, `extension--1.0--1.1.sql`.
- Tests: `sql/` inputs, `expected/` outputs.
- Some extensions disable LLVM bitcode (`with_llvm = no`) for complex C++ modules.
- Disabled PostgreSQL modules in Cloudberry: `cube`, `earthdistance` (keyword conflicts), `lo` (large object unsupported).
