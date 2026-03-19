# contrib/check_expiration/Makefile

MODULE_big = check_expiration
OBJS = \
	$(WIN32RES) \
	check_expiration.o

EXTENSION = check_expiration
DATA = check_expiration--1.0.sql
PGFILEDESC = "check_expiration - request postmaster to check expiration date"
REGRESS = check_expiration

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/check_expiration
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
