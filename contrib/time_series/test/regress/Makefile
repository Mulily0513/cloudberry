#
# Makefile for time_series regression tests
#

subdir = test/regress
top_builddir = ../../../../
include $(top_builddir)/src/Makefile.global

PG_REGRESS = $(top_builddir)/src/test/regress/pg_regress

REGRESS = time_bucket gapfill gapfill_mpp \
          cagg_create cagg_invalidation cagg_refresh \
          cagg_union_view cagg_permissions cagg_smoke \
          cagg_fault_tolerance

installcheck:
	$(PG_REGRESS) \
		--inputdir=./ \
		--bindir='$(bindir)' \
		--dbname=contrib_regression \
		$(REGRESS)

clean:
	rm -rf results regression.diffs regression.out
