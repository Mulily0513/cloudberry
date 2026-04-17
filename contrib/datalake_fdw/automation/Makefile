# Automation test Makefile for datalake_fdw
#
# Usage:
#   make test                     # run all smoke tests (skips unavailable services)
#   make smoke-test               # same as above
#   make smoke-test CATEGORIES="iceberg/builtin guc negative"  # specific categories
#   make check-services           # show service availability
#   make list-categories          # show all categories and required services
#   make clean                    # clean test artifacts

.PHONY: all test clean smoke-test performance-test feature-test stability-test \
        chaos-test scale-test compat-test check-services list-categories install-tools help

# Default target
all: test

# Help target
help:
	@echo "Datalake FDW Automation Test Makefile"
	@echo "======================================"
	@echo "Available targets:"
	@echo "  make test                    - Run smoke tests (auto-skips unavailable services)"
	@echo "  make smoke-test              - Same as 'make test'"
	@echo "  make smoke-test CATEGORIES='guc negative iceberg/builtin'"
	@echo "                               - Run specific categories only"
	@echo "  make check-services          - Show which services are available"
	@echo "  make list-categories         - Show all categories and their service requirements"
	@echo "  make install-tools           - Download and install CLI tools (mc, hdfs, beeline, spark)"
	@echo "  make performance-test        - Run performance tests"
	@echo "  make feature-test            - Run feature tests"
	@echo "  make stability-test          - Run stability tests (concurrency, etc.)"
	@echo "  make stability-test CATEGORIES='concurrency' - run subset"
	@echo "  make chaos-test              - Run chaos tests (crash, fault, network, disk)"
	@echo "  make scale-test              - Run scale tests (volume, many_files, wide_table, etc.)"
	@echo "  make compat-test             - Run compat tests (spark<->hd, requires spark-sql)"
	@echo ""
	@echo "Profiles (via RUN_PROFILE env var):"
	@echo "  RUN_PROFILE=ci      make ...  - CI (default, ~15min)"
	@echo "  RUN_PROFILE=nightly make ...  - Nightly (2-4h)"
	@echo "  RUN_PROFILE=weekly  make ...  - Weekly (8-24h, chaos+scale+compat)"
	@echo "  make clean                   - Clean test artifacts"
	@echo ""
	@echo "Environment variables:"
	@echo "  CATEGORIES              - Space-separated list of test categories"
	@echo "  SKIP_SERVICE_CHECK=1    - Skip service availability check"
	@echo "  INSTALL_TOOLS=1         - Download CLI tools (mc, hdfs, beeline, spark) during check"
	@echo "  ENABLE_SMOKE_TESTS=true/false"
	@echo "  ENABLE_PERFORMANCE_TESTS=true/false"
	@echo "  ENABLE_FEATURE_TESTS=true/false"

# Check service availability (informational, does not block)
check-services:
	@bash scripts/setup/check_services.sh

# Download and install CLI tools (mc, hdfs, beeline, spark-sql)
install-tools:
	@INSTALL_TOOLS=1 bash scripts/setup/check_services.sh

# List all test categories
list-categories:
	@bash scripts/test/run_smoke_tests.sh --list

# Run smoke tests — auto-detects services and skips unavailable categories
smoke-test:
	@echo "Running smoke tests..."
ifdef CATEGORIES
	@SMOKE_CATEGORIES="$(CATEGORIES)" bash scripts/test/run_smoke_tests.sh
else
	@bash scripts/test/run_smoke_tests.sh
endif

# Run performance tests
performance-test:
	@echo "Running performance tests..."
ifdef CATEGORIES
	@PERF_CATEGORIES="$(CATEGORIES)" bash scripts/test/run_performance_tests.sh
else
	@bash scripts/test/run_performance_tests.sh
endif

# Run feature tests
feature-test:
	@echo "Running feature tests..."
	@if [ -f scripts/test/run_feature_tests.sh ]; then \
		bash scripts/test/run_feature_tests.sh; \
	else \
		echo "Feature tests not yet implemented"; \
	fi

# Run stability tests (concurrency / soak / repeat / schema_evolution / ...)
stability-test:
	@echo "Running stability tests..."
ifdef CATEGORIES
	@STABILITY_CATEGORIES="$(CATEGORIES)" bash scripts/test/run_stability_tests.sh
else
	@bash scripts/test/run_stability_tests.sh
endif

# Run chaos tests (crash, fault, network, disk) — weekly profile only by default
chaos-test:
	@echo "Running chaos tests..."
ifdef CATEGORIES
	@CHAOS_CATEGORIES="$(CATEGORIES)" bash scripts/test/run_chaos_tests.sh
else
	@bash scripts/test/run_chaos_tests.sh
endif

# Run scale tests (volume, concurrency_load, many_files, wide_table, nested_types)
scale-test:
	@echo "Running scale tests..."
	@if [ -f scripts/test/run_scale_tests.sh ]; then \
		bash scripts/test/run_scale_tests.sh; \
	else \
		echo "Scale tests: running via stability runner pattern"; \
		STABILITY_CATEGORIES="$$(ls sqlrepo/scale/ 2>/dev/null | tr '\n' ' ')" \
		SQLREPO_DIR="$$(pwd)/sqlrepo" \
		bash scripts/test/run_stability_tests.sh 2>&1 | sed 's/stability/scale/gi'; \
	fi

# Run compat tests (spark <-> hashdata, requires spark-sql)
compat-test:
	@echo "Running compat tests..."
	@if [ -f scripts/test/run_compat_tests.sh ]; then \
		bash scripts/test/run_compat_tests.sh; \
	else \
		echo "Compat tests: running categories directly"; \
		for d in sqlrepo/compat/*/; do \
			[ -f "$$d/Makefile" ] && (cd "$$d" && make installcheck) || true; \
		done; \
	fi

# Default test target
test: smoke-test

# Clean all test artifacts
clean:
	@echo "Cleaning test artifacts..."
	@find sqlrepo -name "results" -type d -exec rm -rf {} + 2>/dev/null || true
	@find sqlrepo -name "regression.diffs" -delete 2>/dev/null || true
	@find sqlrepo -name "regression.out" -delete 2>/dev/null || true
	@find sqlrepo -name "*.tmp" -delete 2>/dev/null || true
	@echo "Cleaned test artifacts"

# Clean reports (use with caution)
clean-reports:
	@echo "WARNING: This will delete all test reports!"
	@read -p "Are you sure? [y/N] " confirm && [ "$$confirm" = "y" ] || exit 1
	@rm -rf reports/logs/* reports/smoke/* reports/performance/* reports/feature/*
	@echo "Cleaned reports"
