# tools/tpc/ — TPC Data Generators

This directory holds the TPC-H `dbgen` and TPC-DS `dsdgen` binaries used by
the iceberg AM scale tests (`sqlrepo/scale/iceberg_am_tpch`,
`sqlrepo/scale/iceberg_am_tpcds`).

## Populate

```
INSTALL_TPC_TOOLS=1 bash ../scripts/setup/install_tools.sh
# or, directly:
bash ../scripts/setup/install_tpc_tools.sh
```

That clones and builds:

- `tpch-dbgen/`   — from [electrum/tpch-dbgen](https://github.com/electrum/tpch-dbgen)
- `tpcds-kit/`    — from [gregrahn/tpcds-kit](https://github.com/gregrahn/tpcds-kit)

and drops symlinks into `bin/{dbgen,dsdgen}`.

## Licensing

`dbgen` and `dsdgen` are derivative works of the TPC-H and TPC-DS
specifications. The TPC's "fair-use" terms allow using the tools to
benchmark internally, but **redistributing generated .tbl / .dat data
outside your organisation is restricted**. We therefore:

- never vendor the upstream source trees (`tpch-dbgen/`, `tpcds-kit/`)
  into git — they're in `.gitignore` and get cloned on demand;
- never commit generated data to git.

Generated data lives outside this directory (see `SCALE_DATA_DIR` env
in the scale-test run scripts).
