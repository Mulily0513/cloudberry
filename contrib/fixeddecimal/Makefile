MODULE_big = fixeddecimal
OBJS = fixeddecimal.o fixeddecimal_optimizer.o

EXTENSION = fixeddecimal

DATA_built = fixeddecimal--1.1.0.sql

PG_CONFIG := $(shell command -v pg_config 2>/dev/null || which pg_config 2>/dev/null || echo "")
ifeq ($(PG_CONFIG),)
PG_CONFIG := ../../src/bin/pg_config/pg_config
endif

TESTS = $(wildcard test/sql/*.sql)


REGRESS_BRIN := $(shell $(PG_CONFIG) --version | grep -qE "XL( 9\.[5-9]| 10\.0| 11\.[0-9]| 12\.[0-9]| 13\.[0-9]| 14\.[0-9])" && echo brin-xl)
REGRESS_BRIN += $(shell $(PG_CONFIG) --version | grep -E "9\.[5-9]| 10\.0| 11\.[0-9]| 12\.[0-9]| 13\.[0-9]| 14\.[0-9]" | grep -qEv "XL" && echo brin)
REGRESS_VERSION_SPECIFIC := $(shell $(PG_CONFIG) --version | grep -qE "XL" && echo index-xl || echo index)
REGRESS = $(shell echo setup aggregate cast comparison overflow optimize_aggregate $(REGRESS_BRIN) $(REGRESS_VERSION_SPECIFIC))

REGRESS_OPTS = --inputdir=test --outputdir=test --init-file=init_file

ifdef USE_PGXS
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/fixeddecimal
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

AGGSTATESQL := $(shell $(PG_CONFIG) --version | grep -qE "XL" && echo fixeddecimalaggstate.sql)
AGGFUNCSSQL := $(shell $(PG_CONFIG) --version | grep -qE "XL" && echo fixeddecimal--xlaggs.sql)

AGGFUNCSSQL := $(shell $(PG_CONFIG) --version | grep -qE "9\.[6-9]| 10\.[0-9]| 11\.[0-9]| 12\.[0-9]| 13\.[0-9]| 14\.[0-9]" && echo fixeddecimal--parallelaggs.sql || echo fixeddecimal--aggs.sql)

#BRINSQL := $(shell $(PG_CONFIG) --version | grep -qE "9\.[5-9]| 10\.[0-9]| 11\.[0-9]| 12\.[0-9]| 13\.[0-9]| 14\.[0-9]" && echo fixeddecimal--brin.sql)

# 9.6 was the dawn of parallel query, so we'll use the parallel enabled .sql file from then on.
BASESQL := $(shell $(PG_CONFIG) --version | grep -qE "9\.[6-9]| 10\.[0-9]| 11\.[0-9]| 12\.[0-9]| 13\.[0-9]| 14\.[0-9]" && echo fixeddecimal--1.1.0_base_parallel.sql || echo fixeddecimal--1.1.0_base.sql)

OBJECTS := $(addprefix $(srcdir)/, $(AGGSTATESQL) $(BASESQL) $(AGGFUNCSSQL) $(BRINSQL))

fixeddecimal--1.1.0.sql: $(OBJECTS)
	cat $^ > $@

install-data: fixeddecimal--1.1.0.sql
	@if [ -d '$(DESTDIR)$(datadir)/cdb_init.d' ]; then \
		echo "install data"; \
		$(INSTALL_DATA) $^ '$(DESTDIR)$(datadir)/cdb_init.d/'; \
	fi

install: install-data

