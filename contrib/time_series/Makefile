# contrib/time_series/Makefile
#
# Top-level build: compiles src/ C files (UDF only: time_bucket + gapfill)

MODULE_big = time_series

OBJS = \
	src/time_series.o \
	src/time_bucket.o \
	src/gapfill.o \
	src/gapfill_plan.o \
	src/gapfill_exec.o \
	src/cagg_create.o \
	src/cagg_insert.o \
	src/cagg_refresh.o

EXTENSION = time_series
DATA = time_series--1.0.sql
PGFILEDESC = "time_series - time bucket and gapfill functions"

# Regression tests are now under test/regress/ and test/isolation2/
# (no REGRESS here — each subdir has its own Makefile)

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
# Override after include: skip gp_inject_fault (not installed in this environment)
override FAULTINJECTOR_OPTS =
else
subdir = contrib/time_series
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
# Override after include: skip gp_inject_fault for time_series tests
override FAULTINJECTOR_OPTS =
endif

# Ensure shared_preload_libraries includes time_series before running tests.
# time_series requires preloading for Custom Scan deserialization on segments.
# This prerequisite makes "make installcheck" work in any environment (ICW, CI,
# manual) by auto-configuring the cluster when needed.
installcheck: _ts_ensure_preload _ts_regress

# Regression tests (delegated to test/regress)
.PHONY: _ts_regress
_ts_regress:
	$(MAKE) -C test/regress installcheck

# Isolation2 tests (run separately)
.PHONY: installcheck-isolation2
installcheck-isolation2: _ts_ensure_preload
	$(MAKE) -C test/isolation2 installcheck

.PHONY: _ts_ensure_preload
_ts_ensure_preload: install
	@current=$$(psql -X -t -A -c "SHOW shared_preload_libraries" template1 2>/dev/null) || \
		{ echo "WARNING: cannot check shared_preload_libraries (is the cluster running?)"; exit 1; }; \
	if echo "$$current" | grep -qw 'time_series'; then \
		echo "time_series already in shared_preload_libraries"; \
	elif [ -z "$$current" ]; then \
		gpconfig -c shared_preload_libraries -v 'time_series' || exit 1; \
		gpstop -ra || exit 1; \
	else \
		gpconfig -c shared_preload_libraries -v "$$current,time_series" || exit 1; \
		gpstop -ra || exit 1; \
	fi

# CI convenience target (kept for backward compatibility with GitHub Actions).
installcheck-ci:
	$(MAKE) installcheck
