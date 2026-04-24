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

#include "access/genam.h"
#include "access/htup_details.h"
#include "access/table.h"
#include "access/xact.h"
#include "catalog/indexing.h"
#include "catalog/namespace.h"
#include "catalog/pg_extension.h"
#include "commands/extension.h"
#include "miscadmin.h"
#include "postmaster/bgworker.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/guc.h"
#include "utils/inval.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"

#ifdef GP_VERSION_NUM
#include "cdb/cdbvars.h"
#endif

/* GUC: max individual materializations per REFRESH call */
int ts_guc_materializations_per_refresh_window = 10;

/*
 * GUC: time_series.restoring
 *
 * Mirrors TimescaleDB's `timescaledb.restoring`.  pg_dump / pg_restore
 * tooling sets this to 'on' so that hooks installed by the extension
 * suppress side-effects (BGW scheduling, invalidation logging,
 * TRUNCATE rewriting, etc.) while the dump is being replayed.
 *
 * Off by default; flipped via ALTER DATABASE / SET when restoring.
 */
bool ts_guc_restoring = false;

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
		ht_cached_namespace_oid = get_namespace_oid(
			TS_EXTENSION_SCHEMA_NAME, true);
		ht_namespace_oid_valid = true;
	}
	return ht_cached_namespace_oid;
}

/* ============================================================
 * Extension state machine
 *
 * Modelled after TimescaleDB's src/extension.c.  Tracks whether
 * the time_series extension is currently installed in this
 * database so that hooks installed by _PG_init (which survive
 * DROP EXTENSION because PG keeps preloaded .so files resident)
 * can short-circuit before touching extension-owned catalog
 * tables.
 *
 * The "proxy table" is time_series.continuous_agg — owned by
 * the extension, created at install time, and dropped early in
 * the DROP EXTENSION CASCADE sequence.  We register a relcache
 * callback so that drops of that relation invalidate the
 * cached state immediately.
 * ============================================================ */

/*
 * Local aliases for the shared identity macros (defined in
 * include/time_series.h) plus the proxy-table name used only here.
 */
#define TS_EXT_NAME           TS_EXTENSION_NAME
#define TS_EXT_PROXY_SCHEMA   TS_EXTENSION_SCHEMA_NAME
#define TS_EXT_PROXY_TABLE    "continuous_agg"

/*
 * The SO version baked into the shared library at compile time.
 * Compared against the SQL extension version on each CREATED
 * transition so that long-lived backends running with a stale
 * .so against an upgraded SQL schema are evicted with FATAL.
 *
 * Must be kept in sync with default_version in
 * time_series.control.
 */
#define TIME_SERIES_SO_VERSION "1.0"

enum TsExtensionState
{
	TS_EXT_STATE_UNKNOWN = 0,
	TS_EXT_STATE_TRANSITIONING,
	TS_EXT_STATE_CREATED,
	TS_EXT_STATE_NOT_INSTALLED,
};

static enum TsExtensionState ts_extstate = TS_EXT_STATE_UNKNOWN;
static Oid ts_extension_proxy_oid = InvalidOid;

/*
 * Look up the SQL-script version recorded in pg_extension.extversion
 * for the given extension name.  Returns a palloc'd string in the
 * current memory context, or NULL if the extension is not installed.
 *
 * Modelled after TimescaleDB's extension_version() in extension_utils.c.
 */
static char *
ts_extension_sql_version(const char *extname)
{
	Relation		rel;
	SysScanDesc		scandesc;
	HeapTuple		tuple;
	ScanKeyData		entry[1];
	char		   *sql_version = NULL;

	rel = table_open(ExtensionRelationId, AccessShareLock);

	ScanKeyInit(&entry[0],
				Anum_pg_extension_extname,
				BTEqualStrategyNumber,
				F_NAMEEQ,
				CStringGetDatum(extname));

	scandesc = systable_beginscan(rel, ExtensionNameIndexId, true, NULL,
								  1, entry);

	tuple = systable_getnext(scandesc);
	if (HeapTupleIsValid(tuple))
	{
		bool	is_null = true;
		Datum	result = heap_getattr(tuple,
									  Anum_pg_extension_extversion,
									  RelationGetDescr(rel),
									  &is_null);

		if (!is_null)
			sql_version = pstrdup(TextDatumGetCString(result));
	}

	systable_endscan(scandesc);
	table_close(rel, AccessShareLock);

	return sql_version;
}

/*
 * Verify that the SQL-script version (pg_extension.extversion) and
 * the SO version compiled into this shared library agree.  Mismatch
 * means a long-lived backend is running stale C code against an
 * upgraded catalog schema; raise FATAL so the client reconnects and
 * picks up the new .so.
 *
 * Mirrors TimescaleDB's ts_extension_check_version().
 */
static void
ts_extension_check_version(const char *so_version)
{
	char	   *sql_version;

	if (!IsNormalProcessingMode() || !IsTransactionState())
		return;

	sql_version = ts_extension_sql_version(TS_EXT_NAME);
	if (sql_version == NULL)
		return;					/* extension not installed */

	if (strcmp(sql_version, so_version) != 0)
	{
		ereport(FATAL,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("extension \"%s\" version mismatch: "
						"shared library version %s; SQL version %s",
						TS_EXT_NAME, so_version, sql_version),
				 errhint("Reconnect so the new shared library is loaded.")));
	}

	pfree(sql_version);
}

static bool
ts_extension_is_transitioning(void)
{
	/*
	 * creating_extension is set by ProcessUtility while a CREATE
	 * EXTENSION or ALTER EXTENSION ... UPDATE script is running
	 * (the name is a misnomer; it covers upgrades too).
	 */
	if (creating_extension)
		return get_extension_oid(TS_EXT_NAME, true) == CurrentExtensionObject;
	return false;
}

static enum TsExtensionState
ts_extension_current_state(void)
{
	Oid			ns_oid;
	Oid			rel_oid;

	/*
	 * Avoid touching catalog before RelationCacheInitializePhase3
	 * has run; otherwise we may recurse through the cache machinery.
	 */
	if (!IsNormalProcessingMode() || !IsTransactionState() ||
		!OidIsValid(MyDatabaseId))
		return TS_EXT_STATE_UNKNOWN;

	if (ts_extension_is_transitioning())
		return TS_EXT_STATE_TRANSITIONING;

	ns_oid = get_namespace_oid(TS_EXT_PROXY_SCHEMA, true);
	if (!OidIsValid(ns_oid))
		return TS_EXT_STATE_NOT_INSTALLED;

	rel_oid = get_relname_relid(TS_EXT_PROXY_TABLE, ns_oid);
	if (OidIsValid(rel_oid))
		return TS_EXT_STATE_CREATED;

	return TS_EXT_STATE_NOT_INSTALLED;
}

static void
ts_extension_update_state(void)
{
	enum TsExtensionState new_state = ts_extension_current_state();

	/*
	 * Never settle on NOT_INSTALLED: if the extension is dropped and
	 * re-created in another backend, the proxy table relid changes
	 * and we cannot reliably detect it through invalidation events.
	 * Falling back to UNKNOWN forces a fresh catalog lookup on the
	 * next state query.
	 */
	if (new_state == TS_EXT_STATE_NOT_INSTALLED)
		new_state = TS_EXT_STATE_UNKNOWN;

	if (new_state == TS_EXT_STATE_CREATED)
	{
		Oid			ns = get_namespace_oid(TS_EXT_PROXY_SCHEMA, true);

		/*
		 * Verify the SQL-script version matches the SO version on
		 * every transition into CREATED.  Mismatch raises FATAL,
		 * forcing the client to reconnect and reload the .so.
		 *
		 * Skip during TRANSITIONING (the script is mid-flight, the
		 * extversion column may not yet reflect the new value).
		 */
		if (ts_extstate != TS_EXT_STATE_TRANSITIONING)
			ts_extension_check_version(TIME_SERIES_SO_VERSION);

		ts_extension_proxy_oid = OidIsValid(ns)
			? get_relname_relid(TS_EXT_PROXY_TABLE, ns)
			: InvalidOid;
	}
	else
	{
		ts_extension_proxy_oid = InvalidOid;
	}

	ts_extstate = new_state;
}

void
ts_extension_invalidate(void)
{
	ts_extstate = TS_EXT_STATE_UNKNOWN;
	ts_extension_proxy_oid = InvalidOid;
}

bool
ts_extension_is_loaded(void)
{
	if (ts_extstate == TS_EXT_STATE_UNKNOWN ||
		ts_extstate == TS_EXT_STATE_TRANSITIONING)
		ts_extension_update_state();

	return ts_extstate == TS_EXT_STATE_CREATED;
}

bool
ts_extension_is_loaded_and_not_upgrading(void)
{
	/*
	 * During pg_upgrade --binary-upgrade or while replaying a logical
	 * dump (time_series.restoring = on), hooks must yield so that
	 * pg_dump / pg_restore can recreate state without our triggers
	 * and BGW scheduler firing on every replayed statement.
	 *
	 * Mirrors TimescaleDB's check:
	 *     if (ts_guc_restoring || IsBinaryUpgrade) return false;
	 */
	if (ts_guc_restoring || IsBinaryUpgrade)
		return false;

	return ts_extension_is_loaded();
}

/*
 * Relcache invalidation callback.
 *
 * Per PG conventions, this MUST NOT call functions that themselves
 * touch the relcache or syscache (risk of recursion / inconsistent
 * state).  We just flip the state back to UNKNOWN; the next call
 * to ts_extension_is_loaded() will do the real lookup.
 */
static void
ts_extension_relcache_callback(Datum arg, Oid relid)
{
	if (!OidIsValid(relid))
	{
		/* Whole-cache invalidation. */
		ts_extension_invalidate();
		return;
	}

	if (OidIsValid(ts_extension_proxy_oid) &&
		relid == ts_extension_proxy_oid)
		ts_extension_invalidate();
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

	/* 4b. Register relcache callback for extension state invalidation.
	 *     Fires whenever any relation's relcache entry changes; we
	 *     filter for the proxy table (time_series.continuous_agg)
	 *     inside the callback. */
	CacheRegisterRelcacheCallback(ts_extension_relcache_callback,
								  (Datum) 0);

	/* 5. Define GUCs */

	/*
	 * time_series.restoring — set to 'on' by pg_dump / pg_restore
	 * tooling (and via ALTER DATABASE) so that hooks installed by
	 * this extension yield while a logical dump is being replayed.
	 * Mirrors TimescaleDB's `timescaledb.restoring`.
	 */
	DefineCustomBoolVariable("time_series.restoring",
							 "Suppress extension hooks during pg_restore",
							 "When on, ProcessUtility / planner / "
							 "invalidation-trigger hooks installed by "
							 "the time_series extension yield to the "
							 "previous handler immediately, so a "
							 "logical dump can be replayed without "
							 "side-effects.",
							 &ts_guc_restoring,
							 false,
							 PGC_USERSET,
							 0,
							 NULL, NULL, NULL);

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

	/* 6. Define BGW GUCs (bgw_enabled, bgw_db, bgw_max_workers) */
	ts_bgw_define_gucs();

	/* 7. Register BGW scheduler (coordinator only, if enabled) */
	ts_bgw_register_scheduler();

	elog(LOG, "time_series _PG_init: hooks installed (gapfill scan, "
		 "gapfill planner, CAGG ProcessUtility, namespace cache, "
		 "extension relcache); GUCs defined; BGW scheduler registered");
}
