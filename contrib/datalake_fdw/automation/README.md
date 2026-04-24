# Datalake FDW Automation Test Framework

Comprehensive test framework for datalake_fdw supporting smoke tests, performance tests, and feature tests.

## Quick Start

```bash
# Check if services are ready
make check-services

# Run all smoke tests
make smoke-test

# Run performance tests
make performance-test

# Run feature tests
make feature-test

# Run complete test suite
bash scripts/test/run_all_tests.sh
```

## TPC-H / TPC-DS scale tests for Iceberg AM

Two scale categories — `sqlrepo/scale/iceberg_am_tpch/` and
`sqlrepo/scale/iceberg_am_tpcds/` — exercise the Iceberg access method
with real dbgen / dsdgen data at configurable scale factors.

```bash
# Build the TPC tools once (opt-in; slows down stock install_tools.sh).
# Run from inside the CBDB container to avoid glibc / compiler drift
# with the host; the source tree is visible there at /workspace.
INSTALL_TPC_TOOLS=1 bash scripts/setup/install_tools.sh

# Convenience launcher. Handles DB creation, dbgen/dsdgen invocation,
# staging COPY + INSERT into Iceberg, and per-query PASS / FAIL / CRASH.
./scripts/test/run_tpc.sh <tpch|tpcds> [scale] [mode]

# Examples:
./scripts/test/run_tpc.sh tpch 1            # TPC-H SF=1 full run
./scripts/test/run_tpc.sh tpcds 10 query    # re-run DS queries against an existing SF=10 DB
./scripts/test/run_tpc.sh tpch 100 clean    # drop SF=100 database and raw data

# Or integrate via the Makefile:
TPC_SCALE=1 make scale-test CATEGORIES="iceberg_am_tpch iceberg_am_tpcds"
```

Per-run summaries land in `reports/scale/<timestamp>/iceberg_am_<bench>/sf<N>/summary.tsv`
with one row per query: `query  status  latency_ms  note`.

**Scale-factor tiers**

| SF | Data volume (raw) | Recommended use |
|----|-------------------|-----------------|
| 1    | ~1 GB   | CI / per-PR smoke |
| 10   | ~10 GB  | nightly |
| 100  | ~100 GB | release gating / on-demand |
| 1000 | ~1 TB   | quarterly on an XL-disk runner (the driver exits 77 if local disk can't fit the raw + Iceberg copy) |

**TPC licensing note.** `dbgen` (TPC-H) and `dsdgen` (TPC-DS) are TPC
derivative works. The installer clones their upstream repos into
`tools/tpc/` on demand but never vendors the sources into this repo,
and generated `.tbl` / `.dat` data files are kept outside the build tree
(default `../../../../.tpc-data/`). Do not publish generated data.

## Directory Structure

```
automation/
├── config/                      # Centralized configuration
│   ├── test_config.env         # Environment variables and settings
│   └── baseline.json           # Performance baselines
│
├── lib/                        # Reusable SQL components
│   └── sql/
│       ├── common_setup.sql           # Base setup (extensions, wrapper, utilities)
│       ├── hive_server_setup.sql      # Hive server configuration
│       ├── s3_server_setup.sql        # S3/MinIO server setup
│       ├── hdfs_server_setup.sql      # HDFS server configuration
│       ├── iceberg_server_setup.sql   # Iceberg catalog & volume
│       ├── performance_helpers.sql    # Performance measurement functions
│       └── common_teardown.sql        # Standard cleanup
│
├── scripts/                    # Automation scripts
│   ├── setup/                  # Setup and installation
│   │   ├── install_beeline.sh
│   │   └── check_services.sh
│   ├── data/                   # Test data management
│   │   └── load_hive_data.sh
│   ├── test/                   # Test orchestration
│   │   ├── run_smoke_tests.sh
│   │   ├── run_performance_tests.sh
│   │   ├── run_feature_tests.sh
│   │   └── run_all_tests.sh
│   └── utils/                  # Shared utilities
│       └── common_functions.sh
│
├── sqlrepo/                    # Test SQL files
│   ├── smoke/                  # Quick sanity checks
│   │   ├── hive/
│   │   ├── iceberg/
│   │   ├── s3/
│   │   └── hdfs/
│   ├── performance/            # Performance benchmarks
│   │   ├── hive/
│   │   ├── iceberg/
│   │   ├── s3/
│   │   └── hdfs/
│   └── feature/                # Comprehensive functional tests
│       ├── partitioning/
│       ├── data_types/
│       ├── compression/
│       ├── transactions/
│       ├── schema_evolution/
│       ├── time_travel/
│       └── advanced_queries/
│
├── reports/                    # Test reports (gitignored)
│   ├── logs/
│   ├── smoke/
│   ├── performance/
│   └── feature/
│
├── Makefile                    # Main automation makefile
└── README.md                   # This file
```

## Test Types

### 1. Smoke Tests
Quick sanity checks to verify basic functionality.

```bash
# Run all smoke tests
make smoke-test

# Or directly
bash scripts/test/run_smoke_tests.sh
```

**Categories:**
- **hive**: Hive table sync and queries
- **iceberg**: Iceberg builtin and Polaris catalog
- **s3**: S3/MinIO foreign tables
- **hdfs**: HDFS connectivity and formats

### 2. Performance Tests
Benchmark tests with timing and throughput measurement.

```bash
# Run all performance tests
make performance-test

# Or directly
bash scripts/test/run_performance_tests.sh
```

**Test Files:**
- `hive_scan_performance.sql`: ORC/Parquet scan performance
- `hive_write_performance.sql`: Write operation performance
- `iceberg_write_performance.sql`: Iceberg write operations
- `iceberg_read_performance.sql`: Iceberg read operations
- `s3_read_performance.sql`: S3 read throughput
- `hdfs_throughput_performance.sql`: HDFS throughput

**Performance Helpers:**
```sql
-- Start timer
SELECT perf_start_timer('test_name', 'operation_name');

-- Run your test query
SELECT COUNT(*) FROM large_table;

-- End timer and record results
SELECT perf_end_timer('test_name', 'operation_name', row_count, 'notes');

-- View results
SELECT * FROM perf_report('test_name');

-- Clear results
SELECT perf_clear('test_name');
```

### 3. Feature Tests
Comprehensive functional coverage organized by feature area.

```bash
# Run all feature tests
make feature-test

# Run specific category
bash scripts/test/run_feature_tests.sh partitioning

# Or directly in category directory
cd sqlrepo/feature/partitioning && make installcheck
```

**Categories:**
- **partitioning**: Partition pruning, multi-level partitions
- **data_types**: Numeric, string, datetime, complex types
- **transactions**: Iceberg ACID operations
- **advanced_queries**: Joins, subqueries, window functions
- **compression**: (Planned) Snappy, gzip, lz4, zstd
- **schema_evolution**: (Planned) Add/drop/rename columns
- **time_travel**: (Planned) Iceberg snapshots

## Configuration

All configuration is centralized in `config/test_config.env`:

```bash
# Database connection
export PGHOST=localhost
export PGPORT=5432
export PGDATABASE=postgres

# Service endpoints
export HIVE_HOST=lakehouse
export MINIO_HOST=lakehouse
export HDFS_NAMENODE=lakehouse

# Credentials
export MINIO_ACCESS_KEY=admin
export MINIO_SECRET_KEY=password

# Test settings
export TEST_TIMEOUT=300
export PERF_ITERATIONS=3
```

## Writing New Tests

### Smoke Test Template

```sql
-- Load common setup and server configuration
\i ../../../../lib/sql/common_setup.sql
\i ../../../../lib/sql/hive_server_setup.sql

DROP SCHEMA IF EXISTS my_test CASCADE;
CREATE SCHEMA my_test;

-- Your test logic here
SELECT test_log('Test description');
SELECT * FROM my_table;

-- Cleanup
DROP SCHEMA my_test CASCADE;
```

### Performance Test Template

```sql
\i ../../../../lib/sql/common_setup.sql
\i ../../../../lib/sql/hive_server_setup.sql
\i ../../../../lib/sql/performance_helpers.sql

-- Setup test data
CREATE SCHEMA perf_test;

-- Test with timing
SELECT test_log('Performance Test: Description');
SELECT perf_start_timer('test_name', 'operation');
-- Your query here
SELECT perf_end_timer('test_name', 'operation', row_count, 'notes');

-- Report results
SELECT * FROM perf_report('test_name');

-- Cleanup
DROP SCHEMA perf_test CASCADE;
```

## Running in Docker

Since the environment is in Docker container `hashdata-lightning-umbrella-hashdata-1`:

```bash
# Enter the container
docker exec -it hashdata-lightning-umbrella-hashdata-1 bash

# Navigate to automation directory
cd /workspace/contrib/datalake_fdw/automation

# Run tests
make smoke-test
```

## Makefile Targets

```bash
make help              # Show available targets
make check-services    # Check if services are running
make smoke-test        # Run smoke tests
make performance-test  # Run performance tests
make feature-test      # Run feature tests
make test              # Run default tests (smoke)
make clean             # Clean test artifacts
```

## Test Reports

Reports are generated in `reports/` directory with timestamps:

```
reports/
├── smoke/
│   └── 20260210_153045/
│       ├── summary.txt
│       ├── hive.log
│       └── iceberg.log
├── performance/
│   └── 20260210_153045/
│       ├── summary.txt
│       └── hive.log
└── feature/
    └── 20260210_153045/
        ├── summary.txt
        └── partitioning.log
```

## Troubleshooting

### Services not accessible
```bash
# Check service status
make check-services

# Ensure Docker services are running
docker-compose ps
```

### Test failures
```bash
# Check regression diffs
cat sqlrepo/smoke/hive/regression.diffs

# View test logs
cat reports/smoke/latest/hive.log
```

## Migration Notes

This framework replaces the previous test structure with:
- **Centralized configuration** instead of hardcoded values
- **Reusable SQL components** instead of duplicated setup code
- **Organized test categories** instead of mixed test files
- **Performance measurement** infrastructure
- **Comprehensive feature coverage**

All existing smoke tests have been migrated to use the new lib/sql components while maintaining backward compatibility.
