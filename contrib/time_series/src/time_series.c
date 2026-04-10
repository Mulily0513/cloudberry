/*
 * time_series.c - Module initialization for time_series extension
 *
 * Registers Custom Scan methods and planner hooks for GapFill.
 * Must be loaded via shared_preload_libraries so that Custom Scan
 * deserialization works on all segments.
 *
 * Copyright (c) 2026 HashData Inc.
 * Licensed under Apache License 2.0
 */
#include "include/time_series.h"

#include "catalog/namespace.h"
#include "utils/guc.h"
#include "utils/inval.h"
#include "utils/syscache.h"

/* GUC: max individual materializations per REFRESH call */
int ts_guc_materializations_per_refresh_window = 10;

PG_MODULE_MAGIC;

/* Cached time_series namespace OID */
static Oid ht_cached_namespace_oid = InvalidOid;
static bool ht_namespace_oid_valid = false;

static void
ht_namespace_invalidation_cb(Datum arg, int cacheid, uint32 hashvalue)
{
	ht_namespace_oid_valid = false;
}

/*
 * Get the time_series namespace OID with caching.
 * Returns InvalidOid if the extension is not installed.
 */
Oid
ht_get_namespace_oid_cached(void)
{
	if (!ht_namespace_oid_valid)
	{
		ht_cached_namespace_oid = get_namespace_oid("time_series", true);
		ht_namespace_oid_valid = true;
	}
	return ht_cached_namespace_oid;
}

void _PG_init(void);

void
_PG_init(void)
{
	/* 1. Register GapFill Custom Scan Methods (needed on both Coordinator
	 *    and Segments so that plan nodes can be deserialized after dispatch) */
	ht_gapfill_scan_init();

	/* 2. Install GapFill Planner Hook */
	ht_gapfill_planner_init();

	/* 3. Install Continuous Aggregate ProcessUtility Hook */
	ht_cagg_init();

	/* 4. Register syscache callback for namespace OID invalidation */
	CacheRegisterSyscacheCallback(NAMESPACENAME,
								  ht_namespace_invalidation_cb,
								  (Datum) 0);

	/* 5. Define GUCs */
	DefineCustomIntVariable("time_series.materializations_per_refresh_window",
							"Max number of individual refreshes per REFRESH call",
							"If more intervals need to be refreshed, they are "
							"merged into a single large refresh to avoid "
							"excessive fragmented I/O.",
							&ts_guc_materializations_per_refresh_window,
							10,		/* default */
							0,		/* min (0 = unlimited) */
							INT_MAX,
							PGC_USERSET,
							0,
							NULL, NULL, NULL);
}
