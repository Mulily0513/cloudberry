/*-------------------------------------------------------------------------
 *
 * cagg_refresh.c
 *    Continuous Aggregate REFRESH procedure.
 *
 *    Implements: CALL time_series.refresh_continuous_aggregate(name, start, end)
 *
 *    Two-transaction model via SPI_commit_and_chain:
 *      TX1 (short): advisory lock → L1→L2 migration → delete L1 → COMMIT
 *      TX2 (main):  advisory lock → gather L2 → interval merge →
 *                   DELETE+INSERT materialization → advance watermark →
 *                   trim L2
 *
 * Copyright (c) 2026 HashData Inc.
 * Licensed under Apache License 2.0
 *
 * IDENTIFICATION
 *    contrib/time_series/src/cagg_refresh.c
 *
 *-------------------------------------------------------------------------
 */
#include "../include/time_series.h"

#include "access/heapam.h"
#include "access/htup_details.h"
#include "access/skey.h"
#include "access/table.h"
#include "access/xact.h"
#include "storage/lmgr.h"			/* LockRelationOid */
#include "miscadmin.h"				/* GetUserId */
#include "utils/acl.h"				/* pg_class_ownercheck, aclcheck_error */
#include "catalog/namespace.h"
#include "datatype/timestamp.h"
#include "executor/spi.h"
#include "utils/builtins.h"
#include "utils/datum.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"
#include "tcop/pquery.h"			/* ActivePortal */
#include "utils/guc.h"			/* set_config_option */
#include "utils/rel.h"
#include "utils/snapmgr.h"
#include "utils/timestamp.h"

#include "nodes/parsenodes.h"

#include "cdb/cdbvars.h"

#ifdef FAULT_INJECTOR
#include "utils/faultinjector.h"
#endif

/* ================================================================
 * Internal structures
 * ================================================================ */

typedef struct CaggRefreshInfo
{
	int			cagg_id;
	Oid			source_table_oid;
	char	   *mat_table_schema;
	char	   *mat_table_name;
	char	   *partial_view_schema;
	char	   *partial_view_name;
	char	   *bucket_column;		/* source table time column (e.g. "time") */
	char	   *mat_bucket_col;		/* mat table bucket column (e.g. "bucket") */
	Interval   *bucket_width;
	Oid			time_type;
	/* Origin/offset/timezone from cagg_bucket_function (NULL if not set) */
	bool		has_origin;
	TimestampTz	bucket_origin;
	Interval   *bucket_offset;		/* NULL if not set */
	char	   *bucket_timezone;	/* NULL if not set */
} CaggRefreshInfo;

typedef struct DirtyInterval
{
	TimestampTz start;
	TimestampTz end;			/* exclusive */
} DirtyInterval;

/* ================================================================
 * Parse a possibly schema-qualified name "schema.name" or just "name".
 * If no schema, defaults to "public".
 * ================================================================ */

static void
cagg_parse_qualified_name(const char *input,
						  char *schema_out, char *name_out)
{
	const char *dot = strchr(input, '.');

	if (dot)
	{
		int slen = dot - input;

		if (slen >= NAMEDATALEN) slen = NAMEDATALEN - 1;
		memcpy(schema_out, input, slen);
		schema_out[slen] = '\0';
		strlcpy(name_out, dot + 1, NAMEDATALEN);
	}
	else
	{
		strlcpy(schema_out, "public", NAMEDATALEN);
		strlcpy(name_out, input, NAMEDATALEN);
	}
}

/* ================================================================
 * Look up CAGG metadata from catalog.
 * cagg_name can be "view_name" or "schema.view_name".
 * ================================================================ */

static void
cagg_lookup_metadata(const char *cagg_name, CaggRefreshInfo *info)
{
	int			ret;
	Oid			argtypes[2] = { TEXTOID, TEXTOID };
	Datum		args[2];
	MemoryContext caller_cxt = CurrentMemoryContext;
	char		schema_buf[NAMEDATALEN];
	char		name_buf[NAMEDATALEN];

	cagg_parse_qualified_name(cagg_name, schema_buf, name_buf);

	args[0] = CStringGetTextDatum(schema_buf);
	args[1] = CStringGetTextDatum(name_buf);

	/*
	 * Query CAGG metadata + mat table's first column name (the bucket alias).
	 * Match on BOTH user_view_schema AND user_view_name to avoid ambiguity
	 * when multiple schemas have same-named CAGGs.
	 */
	ret = SPI_execute_with_args(
		"SELECT c.cagg_id, c.source_table_oid, "
		"c.mat_table_schema, c.mat_table_name, "
		"c.partial_view_schema, c.partial_view_name, "
		"c.bucket_column, c.bucket_width, bf.time_type, "
		"a.attname AS mat_bucket_col, "
		"bf.bucket_origin, bf.bucket_offset, bf.bucket_timezone "
		"FROM time_series.continuous_agg c "
		"JOIN time_series.cagg_bucket_function bf ON c.cagg_id = bf.cagg_id "
		"JOIN pg_class pc ON pc.relname = c.mat_table_name "
		"  AND pc.relnamespace = (SELECT oid FROM pg_namespace WHERE nspname = c.mat_table_schema) "
		"JOIN pg_attribute a ON a.attrelid = pc.oid AND a.attnum = 1 AND NOT a.attisdropped "
		"WHERE c.user_view_schema = $1 AND c.user_view_name = $2",
		2, argtypes, args, NULL, true, 1);

	if (ret != SPI_OK_SELECT || SPI_processed == 0)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("continuous aggregate \"%s\" does not exist",
						cagg_name)));

	{
		HeapTuple	tup = SPI_tuptable->vals[0];
		TupleDesc	desc = SPI_tuptable->tupdesc;
		bool		isnull;
		MemoryContext oldcxt;

		info->cagg_id = DatumGetInt32(
			SPI_getbinval(tup, desc, 1, &isnull));
		info->source_table_oid = DatumGetObjectId(
			SPI_getbinval(tup, desc, 2, &isnull));

		/* Copy strings into caller's context so they survive SPI_finish */
		oldcxt = MemoryContextSwitchTo(caller_cxt);
		info->mat_table_schema = pstrdup(SPI_getvalue(tup, desc, 3));
		info->mat_table_name = pstrdup(SPI_getvalue(tup, desc, 4));
		info->partial_view_schema = pstrdup(SPI_getvalue(tup, desc, 5));
		info->partial_view_name = pstrdup(SPI_getvalue(tup, desc, 6));
		info->bucket_column = pstrdup(SPI_getvalue(tup, desc, 7));
		/* Interval is fixed-size (16 bytes), pass-by-reference */
		info->bucket_width = DatumGetIntervalP(datumCopy(
			SPI_getbinval(tup, desc, 8, &isnull), false, sizeof(Interval)));
		info->time_type = DatumGetObjectId(
			SPI_getbinval(tup, desc, 9, &isnull));
		{
			char *mbc = SPI_getvalue(tup, desc, 10);
			info->mat_bucket_col = mbc ? pstrdup(mbc) : pstrdup("bucket");
		}

		/* bucket_origin (col 11) — timestamptz, may be NULL */
		{
			Datum d = SPI_getbinval(tup, desc, 11, &isnull);
			if (!isnull)
			{
				info->has_origin = true;
				info->bucket_origin = DatumGetTimestampTz(d);
			}
			else
			{
				info->has_origin = false;
				info->bucket_origin = DT_NOBEGIN;
			}
		}
		/* bucket_offset (col 12) — interval, may be NULL */
		{
			Datum d = SPI_getbinval(tup, desc, 12, &isnull);
			info->bucket_offset = isnull ? NULL :
				DatumGetIntervalP(datumCopy(d, false, sizeof(Interval)));
		}
		/* bucket_timezone (col 13) — text, may be NULL */
		{
			char *tz = SPI_getvalue(tup, desc, 13);
			info->bucket_timezone = tz ? pstrdup(tz) : NULL;
		}

		MemoryContextSwitchTo(oldcxt);
	}
}

/* ================================================================
 * Acquire advisory lock for this CAGG (per-transaction scope)
 * ================================================================ */

static void
cagg_acquire_lock(int cagg_id)
{
	Oid		argtypes[1] = { INT4OID };
	Datum	args[1];

	args[0] = Int32GetDatum(cagg_id);
	SPI_execute_with_args(
		"SELECT pg_advisory_xact_lock($1::bigint)",
		1, argtypes, args, NULL, true, 0);
}

/* ================================================================
 * Segment-local L1 → L2 migration function.
 *
 * Called on EACH segment via:
 *   SELECT time_series._cagg_move_l1_to_l2($source_oid)
 *   FROM time_series.cagg_watermark WHERE cagg_id = $cagg_id;
 *
 * cagg_watermark is DISTRIBUTED RANDOMLY (one row per segment per CAGG),
 * so this SELECT naturally dispatches to every segment exactly once.
 *
 * On each segment, this function:
 *   1. Opens L1 (cagg_invalidation_log) and scans for source_table_oid
 *   2. Opens L2 (cagg_materialization_log)
 *   3. Reads all cagg_ids for this source from continuous_agg (REPLICATED)
 *   4. For each L1 entry: simple_heap_insert into L2 for every CAGG
 *   5. simple_heap_delete the L1 entry
 *
 * All operations are segment-local: zero network I/O, zero SPI overhead.
 * ================================================================ */

PG_FUNCTION_INFO_V1(cagg_segment_move_l1_to_l2);

Datum
cagg_segment_move_l1_to_l2(PG_FUNCTION_ARGS)
{
	Oid			source_oid = PG_GETARG_OID(0);
	Oid			ns_oid;
	Oid			l1_oid, l2_oid, ca_oid;
	Relation	l1_rel, l2_rel, ca_rel;
	TableScanDesc l1_scan, ca_scan;
	HeapTuple	l1_tup, ca_tup;
	List	   *cagg_ids = NIL;
	ListCell   *lc;

	ns_oid = ht_get_namespace_oid_cached();

	/* Open all three relations */
	l1_oid = get_relname_relid("cagg_invalidation_log", ns_oid);
	l2_oid = get_relname_relid("cagg_materialization_log", ns_oid);
	ca_oid = get_relname_relid("continuous_agg", ns_oid);

	if (!OidIsValid(l1_oid) || !OidIsValid(l2_oid) || !OidIsValid(ca_oid))
		elog(ERROR, "_cagg_move_l1_to_l2: catalog tables not found");

	/*
	 * Lock order: L1 (RowExclusive) → L2 (RowExclusive) → continuous_agg
	 * (AccessShare).
	 *
	 * The trigger insert path acquires the catalog tables in the
	 * opposite order: continuous_agg (AccessShare) → cagg_watermark
	 * (AccessShare) → L1 (RowExclusive).  The shared pair {L1,
	 * continuous_agg} is therefore acquired in opposite orders here
	 * and there.
	 *
	 * This is currently safe because the lock modes don't pairwise
	 * conflict: AccessShare on continuous_agg never blocks against
	 * itself, and both paths take RowExclusive on L1 (compatible
	 * with itself).  If either mode is ever tightened (e.g. taking
	 * Share or stronger on continuous_agg during DDL) this becomes
	 * a deadlock.  Update both call sites together if you change
	 * either mode.
	 */
	l1_rel = table_open(l1_oid, RowExclusiveLock);
	l2_rel = table_open(l2_oid, RowExclusiveLock);
	ca_rel = table_open(ca_oid, AccessShareLock);

	/*
	 * Step 1: Collect all cagg_ids for this source from continuous_agg.
	 * continuous_agg is REPLICATED, so every segment has a full copy.
	 * We scan it to find which CAGGs reference this source_oid.
	 *
	 * Use the active snapshot (MVCC) so visibility rules are honored;
	 * SnapshotSelf would expose uncommitted writes from this xact and
	 * ignore xmax (a fragile choice if the surrounding lock scheme is
	 * ever relaxed).  Mirrors TimescaleDB's invalidation log scans.
	 */
	ca_scan = heap_beginscan(ca_rel, GetActiveSnapshot(), 0, NULL, NULL, 0);
	while ((ca_tup = heap_getnext(ca_scan, ForwardScanDirection)) != NULL)
	{
		bool	isnull;
		Datum	d_source;
		Datum	d_cagg_id;

		/* continuous_agg column 6 = source_table_oid (oid) */
		d_source = heap_getattr(ca_tup, 6, RelationGetDescr(ca_rel), &isnull);
		if (isnull || DatumGetObjectId(d_source) != source_oid)
			continue;

		/* continuous_agg column 1 = cagg_id (int4) */
		d_cagg_id = heap_getattr(ca_tup, 1, RelationGetDescr(ca_rel), &isnull);
		if (!isnull)
			cagg_ids = lappend_int(cagg_ids, DatumGetInt32(d_cagg_id));
	}
	heap_endscan(ca_scan);
	table_close(ca_rel, AccessShareLock);

	if (cagg_ids == NIL)
	{
		/* No CAGGs for this source — just close and return */
		table_close(l2_rel, RowExclusiveLock);
		table_close(l1_rel, RowExclusiveLock);
		PG_RETURN_VOID();
	}

	/*
	 * Step 2: Scan L1 for entries matching source_oid.
	 * For each entry, insert into L2 for EVERY cagg_id, then delete from L1.
	 *
	 * L1 schema: (source_table_oid oid, lowest_modified timestamptz,
	 *             greatest_modified timestamptz)
	 * L2 schema: (cagg_id int, lowest_modified timestamptz,
	 *             greatest_modified timestamptz)
	 */
	/* MVCC for L1 scan — see comment on the continuous_agg scan above. */
	l1_scan = heap_beginscan(l1_rel, GetActiveSnapshot(), 0, NULL, NULL, 0);
	while ((l1_tup = heap_getnext(l1_scan, ForwardScanDirection)) != NULL)
	{
		bool		isnull;
		Datum		d_src, d_low, d_high;
		TimestampTz	lowest, greatest;

		/* Filter: only process entries for our source_oid */
		d_src = heap_getattr(l1_tup, 1, RelationGetDescr(l1_rel), &isnull);
		if (isnull || DatumGetObjectId(d_src) != source_oid)
			continue;

		d_low = heap_getattr(l1_tup, 2, RelationGetDescr(l1_rel), &isnull);
		lowest = DatumGetTimestampTz(d_low);
		d_high = heap_getattr(l1_tup, 3, RelationGetDescr(l1_rel), &isnull);
		greatest = DatumGetTimestampTz(d_high);

		/* Insert one L2 entry for each CAGG */
		foreach(lc, cagg_ids)
		{
			int			cagg_id = lfirst_int(lc);
			Datum		l2_vals[3];
			bool		l2_nulls[3] = { false, false, false };
			HeapTuple	l2_tup;

			l2_vals[0] = Int32GetDatum(cagg_id);
			l2_vals[1] = TimestampTzGetDatum(lowest);
			l2_vals[2] = TimestampTzGetDatum(greatest);

			l2_tup = heap_form_tuple(RelationGetDescr(l2_rel),
									 l2_vals, l2_nulls);
			simple_heap_insert(l2_rel, l2_tup);
			heap_freetuple(l2_tup);
		}

		/* Delete consumed L1 entry by its TID */
		simple_heap_delete(l1_rel, &l1_tup->t_self);
	}
	heap_endscan(l1_scan);

	table_close(l2_rel, RowExclusiveLock);
	table_close(l1_rel, RowExclusiveLock);

	PG_RETURN_VOID();
}

/* ================================================================
 * TX1: Dispatch segment-local L1 → L2 migration to all segments.
 *
 * Uses the cagg_watermark trick: cagg_watermark is DISTRIBUTED RANDOMLY
 * (one row per segment per CAGG), so SELECT ... FROM cagg_watermark
 * WHERE cagg_id = $1 naturally dispatches to every segment once.
 * ================================================================ */

static void
cagg_migrate_l1_to_l2(CaggRefreshInfo *info)
{
	Oid		argtypes[2] = { OIDOID, INT4OID };
	Datum	args[2];

	args[0] = ObjectIdGetDatum(info->source_table_oid);
	args[1] = Int32GetDatum(info->cagg_id);

	SPI_execute_with_args(
		"SELECT time_series._cagg_move_l1_to_l2($1) "
		"FROM time_series.cagg_watermark WHERE cagg_id = $2",
		2, argtypes, args, NULL, true, 0);
}

/* ================================================================
 * Align a timestamp to bucket boundary using time_bucket().
 *
 * If origin is provided (has_origin=true), it is passed to
 * time_bucket($1, $2, $3) so origin-shifted CAGGs align correctly.
 * This matches TimescaleDB's circumscribed alignment behavior.
 * ================================================================ */

/*
 * Build and execute the appropriate time_bucket() SPI call based on
 * which bucket function parameters are set (origin, offset, timezone).
 *
 * time_bucket variants:
 *   time_bucket(interval, timestamptz)                         — plain
 *   time_bucket(interval, timestamptz, timestamptz)            — origin
 *   time_bucket(interval, timestamptz, interval)               — offset
 *   time_bucket(interval, timestamptz, text)                   — timezone
 *   time_bucket(interval, timestamptz, text, timestamptz)      — timezone + origin
 *
 * If suffix is non-NULL, it's appended to the SELECT (e.g. " + $1" for bucket end).
 */
static TimestampTz
cagg_align_bucket_internal(TimestampTz ts, CaggRefreshInfo *info,
						   const char *suffix)
{
	bool	isnull;
	int		ret;
	StringInfoData sql;

	if (TIMESTAMP_NOT_FINITE(ts))
		return ts;

	initStringInfo(&sql);

	if (info->bucket_timezone != NULL)
	{
		/* timezone variant: time_bucket($1, $2, $3) or time_bucket($1, $2, $3, $4) */
		if (info->has_origin)
		{
			Oid		argtypes[4] = { INTERVALOID, TIMESTAMPTZOID, TEXTOID, TIMESTAMPTZOID };
			Datum	args[4];

			args[0] = IntervalPGetDatum(info->bucket_width);
			args[1] = TimestampTzGetDatum(ts);
			args[2] = CStringGetTextDatum(info->bucket_timezone);
			args[3] = TimestampTzGetDatum(info->bucket_origin);

			appendStringInfo(&sql, "SELECT time_series.time_bucket($1, $2, $3, $4)%s",
							 suffix ? suffix : "");
			ret = SPI_execute_with_args(sql.data, 4, argtypes, args, NULL, true, 1);
		}
		else
		{
			Oid		argtypes[3] = { INTERVALOID, TIMESTAMPTZOID, TEXTOID };
			Datum	args[3];

			args[0] = IntervalPGetDatum(info->bucket_width);
			args[1] = TimestampTzGetDatum(ts);
			args[2] = CStringGetTextDatum(info->bucket_timezone);

			appendStringInfo(&sql, "SELECT time_series.time_bucket($1, $2, $3)%s",
							 suffix ? suffix : "");
			ret = SPI_execute_with_args(sql.data, 3, argtypes, args, NULL, true, 1);
		}
	}
	else if (info->bucket_offset != NULL)
	{
		/* offset variant: time_bucket($1, $2, $3::interval) */
		Oid		argtypes[3] = { INTERVALOID, TIMESTAMPTZOID, INTERVALOID };
		Datum	args[3];

		args[0] = IntervalPGetDatum(info->bucket_width);
		args[1] = TimestampTzGetDatum(ts);
		args[2] = IntervalPGetDatum(info->bucket_offset);

		appendStringInfo(&sql, "SELECT time_series.time_bucket($1, $2, $3)%s",
						 suffix ? suffix : "");
		ret = SPI_execute_with_args(sql.data, 3, argtypes, args, NULL, true, 1);
	}
	else if (info->has_origin)
	{
		/* origin variant: time_bucket($1, $2, $3::timestamptz) */
		Oid		argtypes[3] = { INTERVALOID, TIMESTAMPTZOID, TIMESTAMPTZOID };
		Datum	args[3];

		args[0] = IntervalPGetDatum(info->bucket_width);
		args[1] = TimestampTzGetDatum(ts);
		args[2] = TimestampTzGetDatum(info->bucket_origin);

		appendStringInfo(&sql, "SELECT time_series.time_bucket($1, $2, $3)%s",
						 suffix ? suffix : "");
		ret = SPI_execute_with_args(sql.data, 3, argtypes, args, NULL, true, 1);
	}
	else
	{
		/* plain variant: time_bucket($1, $2) */
		Oid		argtypes[2] = { INTERVALOID, TIMESTAMPTZOID };
		Datum	args[2];

		args[0] = IntervalPGetDatum(info->bucket_width);
		args[1] = TimestampTzGetDatum(ts);

		appendStringInfo(&sql, "SELECT time_series.time_bucket($1, $2)%s",
						 suffix ? suffix : "");
		ret = SPI_execute_with_args(sql.data, 2, argtypes, args, NULL, true, 1);
	}

	pfree(sql.data);

	if (ret != SPI_OK_SELECT || SPI_processed == 0)
		elog(ERROR, "cagg_refresh: could not align timestamp to bucket");

	return DatumGetTimestampTz(
		SPI_getbinval(SPI_tuptable->vals[0],
					  SPI_tuptable->tupdesc, 1, &isnull));
}

static TimestampTz
cagg_align_to_bucket_start(CaggRefreshInfo *info, TimestampTz ts)
{
	return cagg_align_bucket_internal(ts, info, NULL);
}

static TimestampTz
cagg_align_to_bucket_end(CaggRefreshInfo *info, TimestampTz ts)
{
	return cagg_align_bucket_internal(ts, info, " + $1");
}

/* ================================================================
 * Gather L2 entries, align to bucket boundaries, merge overlapping
 * intervals (LeetCode 56 algorithm), and intersect with the
 * user-supplied refresh window.
 *
 * Returns the number of dirty intervals.  *intervals_out is palloc'd
 * in the caller's memory context.
 * ================================================================ */

static int
cagg_gather_dirty_intervals(CaggRefreshInfo *info,
							TimestampTz window_start,
							TimestampTz window_end,
							DirtyInterval **intervals_out)
{
	Oid		argtypes[1] = { INT4OID };
	Datum	args[1];
	int		ret;
	int		raw_count;
	DirtyInterval *raw = NULL;
	DirtyInterval *merged = NULL;
	int		num_merged = 0;
	int		i;

	args[0] = Int32GetDatum(info->cagg_id);

	ret = SPI_execute_with_args(
		"SELECT lowest_modified, greatest_modified "
		"FROM time_series.cagg_materialization_log "
		"WHERE cagg_id = $1 "
		"ORDER BY lowest_modified",
		1, argtypes, args, NULL, true, 0);

	if (ret != SPI_OK_SELECT || SPI_processed == 0)
	{
		*intervals_out = NULL;
		return 0;
	}

	raw_count = SPI_processed;
	raw = palloc(sizeof(DirtyInterval) * raw_count);

	/*
	 * Phase 1: Extract ALL values from SPI_tuptable FIRST.
	 * We must NOT call any SPI functions (like cagg_align_to_bucket_start)
	 * during this loop, because nested SPI calls invalidate SPI_tuptable.
	 */
	for (i = 0; i < raw_count; i++)
	{
		bool	null1, null2;

		raw[i].start = DatumGetTimestampTz(
			SPI_getbinval(SPI_tuptable->vals[i],
						  SPI_tuptable->tupdesc, 1, &null1));
		raw[i].end = DatumGetTimestampTz(
			SPI_getbinval(SPI_tuptable->vals[i],
						  SPI_tuptable->tupdesc, 2, &null2));

		if (null1 || null2)
			ereport(ERROR,
					(errcode(ERRCODE_DATA_CORRUPTED),
					 errmsg("NULL value in materialization invalidation log"),
					 errhint("The cagg_materialization_log catalog may be corrupted.")));
	}

	/*
	 * Phase 2: Align to bucket boundaries AFTER all SPI results extracted.
	 * cagg_align_to_bucket_start/end use SPI internally.
	 */
	for (i = 0; i < raw_count; i++)
	{
		raw[i].start = cagg_align_to_bucket_start(info, raw[i].start);
		raw[i].end = cagg_align_to_bucket_end(info, raw[i].end);
	}

	/*
	 * Merge overlapping intervals (already sorted by start from ORDER BY).
	 * Classic LeetCode 56 algorithm.
	 */
	merged = palloc(sizeof(DirtyInterval) * raw_count);
	num_merged = 0;

	for (i = 0; i < raw_count; i++)
	{
		if (num_merged > 0 && raw[i].start <= merged[num_merged - 1].end)
		{
			/* Overlapping or adjacent — extend the current merged interval */
			if (raw[i].end > merged[num_merged - 1].end)
				merged[num_merged - 1].end = raw[i].end;
		}
		else
		{
			/* New disjoint interval */
			merged[num_merged].start = raw[i].start;
			merged[num_merged].end = raw[i].end;
			num_merged++;
		}
	}

	pfree(raw);

	/*
	 * Intersect each merged interval with the user's refresh window.
	 * Skip intervals entirely outside the window; clamp those that
	 * partially overlap.
	 */
	{
		int		final_count = 0;

		for (i = 0; i < num_merged; i++)
		{
			TimestampTz s = merged[i].start;
			TimestampTz e = merged[i].end;

			/* Clamp to window */
			if (!TIMESTAMP_IS_NOBEGIN(window_start) && s < window_start)
				s = window_start;
			if (!TIMESTAMP_IS_NOEND(window_end) && e > window_end)
				e = window_end;

			/* Skip if no overlap */
			if (s >= e)
				continue;

			merged[final_count].start = s;
			merged[final_count].end = e;
			final_count++;
		}
		num_merged = final_count;
	}

	*intervals_out = merged;
	return num_merged;
}

/* ================================================================
 * Refresh one dirty interval: DELETE old rows, INSERT new from
 * partial view.
 * ================================================================ */

static void
cagg_refresh_one_interval(CaggRefreshInfo *info,
						  TimestampTz start, TimestampTz end)
{
	StringInfoData sql;
	Oid		argtypes[2] = { TIMESTAMPTZOID, TIMESTAMPTZOID };
	Datum	args[2];
	uint64	deleted_rows;
	uint64	inserted_rows;

	args[0] = TimestampTzGetDatum(start);
	args[1] = TimestampTzGetDatum(end);

	initStringInfo(&sql);

	/*
	 * DELETE old materialized rows in this range.
	 * Use mat_bucket_col (the mat table's bucket column alias, e.g. "bucket"),
	 * NOT bucket_column (the source table's time column, e.g. "time").
	 */
	appendStringInfo(&sql,
		"DELETE FROM %s.%s WHERE %s >= $1 AND %s < $2",
		quote_identifier(info->mat_table_schema),
		quote_identifier(info->mat_table_name),
		quote_identifier(info->mat_bucket_col),
		quote_identifier(info->mat_bucket_col));

	SPI_execute_with_args(sql.data, 2, argtypes, args, NULL, false, 0);
	deleted_rows = SPI_processed;

	/*
	 * INSERT new rows from partial view for this range.
	 * The partial view's output column name matches mat_bucket_col.
	 */
	resetStringInfo(&sql);
	appendStringInfo(&sql,
		"INSERT INTO %s.%s "
		"SELECT * FROM %s.%s WHERE %s >= $1 AND %s < $2",
		quote_identifier(info->mat_table_schema),
		quote_identifier(info->mat_table_name),
		quote_identifier(info->partial_view_schema),
		quote_identifier(info->partial_view_name),
		quote_identifier(info->mat_bucket_col),
		quote_identifier(info->mat_bucket_col));

	SPI_execute_with_args(sql.data, 2, argtypes, args, NULL, false, 0);
	inserted_rows = SPI_processed;

	pfree(sql.data);

	/*
	 * Server-log audit of rows touched per interval.  Mirrors TSDB
	 * materialize.c:597,607 "deleted/inserted N row(s)" at LOG level —
	 * default-visible diagnostic for CAGG drift in long-stability
	 * environments.
	 *
	 * The cagg_bgw_mock test framework's emit_log_hook captures every
	 * LOG message (server-side severity LOG > WARNING) into bgw_log.
	 * The mock_time_monotonic assertion is now PARTITION BY
	 * application_name, so cross-process LOG-level interleaving from
	 * multiple BGW workers no longer breaks msg_no ordering.
	 */
	elog(LOG,
		 "deleted " UINT64_FORMAT " row(s) from materialization table \"%s.%s\"",
		 deleted_rows, info->mat_table_schema, info->mat_table_name);
	elog(LOG,
		 "inserted " UINT64_FORMAT " row(s) into materialization table \"%s.%s\"",
		 inserted_rows, info->mat_table_schema, info->mat_table_name);
}

/* ================================================================
 * Advance watermark: watermark = GREATEST(watermark, new_value)
 * ================================================================ */

static void
cagg_advance_watermark(CaggRefreshInfo *info, TimestampTz new_wm)
{
	Oid		argtypes[2] = { TIMESTAMPTZOID, INT4OID };
	Datum	args[2];

	args[0] = TimestampTzGetDatum(new_wm);
	args[1] = Int32GetDatum(info->cagg_id);

	SPI_execute_with_args(
		"UPDATE time_series.cagg_watermark "
		"SET watermark = GREATEST(watermark, $1) "
		"WHERE cagg_id = $2",
		2, argtypes, args, NULL, false, 0);
}

/* ================================================================
 * Trim L2 entries after REFRESH.
 *
 * Three cases for each L2 entry vs the refreshed window [start, end):
 *
 *   Case A: Fully inside window → DELETE
 *           [lowest >= start AND greatest < end]
 *
 *   Case B: Spans left boundary → UPDATE shrink to [lowest, start)
 *           [lowest < start AND greatest >= start AND greatest < end]
 *
 *   Case C: Spans right boundary → UPDATE shrink to [end, greatest]
 *           [lowest >= start AND lowest < end AND greatest >= end]
 *
 *   Case D: Fully contains window → split into two:
 *           [lowest, start) and [end, greatest]
 *           Implemented as UPDATE to [lowest, start) + INSERT [end, greatest]
 *
 *   Case E: Fully outside window → no action
 * ================================================================ */

static void
cagg_trim_l2(CaggRefreshInfo *info, TimestampTz start, TimestampTz end)
{
	Oid		argtypes[3] = { INT4OID, TIMESTAMPTZOID, TIMESTAMPTZOID };
	Datum	args[3];

	args[0] = Int32GetDatum(info->cagg_id);
	args[1] = TimestampTzGetDatum(start);
	args[2] = TimestampTzGetDatum(end);

	/* Case A: fully inside → DELETE */
	SPI_execute_with_args(
		"DELETE FROM time_series.cagg_materialization_log "
		"WHERE cagg_id = $1 "
		"AND lowest_modified >= $2 AND greatest_modified < $3",
		3, argtypes, args, NULL, false, 0);

	/* Case B: spans left boundary → shrink to [lowest, start) */
	SPI_execute_with_args(
		"UPDATE time_series.cagg_materialization_log "
		"SET greatest_modified = $2 "
		"WHERE cagg_id = $1 "
		"AND lowest_modified < $2 "
		"AND greatest_modified >= $2 AND greatest_modified < $3",
		3, argtypes, args, NULL, false, 0);

	/* Case C: spans right boundary → shrink to [end, greatest] */
	SPI_execute_with_args(
		"UPDATE time_series.cagg_materialization_log "
		"SET lowest_modified = $3 "
		"WHERE cagg_id = $1 "
		"AND lowest_modified >= $2 AND lowest_modified < $3 "
		"AND greatest_modified >= $3",
		3, argtypes, args, NULL, false, 0);

	/*
	 * Case D: fully contains window → split.
	 * First INSERT the right remainder [end, greatest], then UPDATE
	 * the original to [lowest, start).  Order matters: INSERT first
	 * so the UPDATE WHERE clause still matches the original row.
	 */
	SPI_execute_with_args(
		"INSERT INTO time_series.cagg_materialization_log "
		"(cagg_id, lowest_modified, greatest_modified) "
		"SELECT $1, $3, greatest_modified "
		"FROM time_series.cagg_materialization_log "
		"WHERE cagg_id = $1 "
		"AND lowest_modified < $2 AND greatest_modified >= $3",
		3, argtypes, args, NULL, false, 0);

	SPI_execute_with_args(
		"UPDATE time_series.cagg_materialization_log "
		"SET greatest_modified = $2 "
		"WHERE cagg_id = $1 "
		"AND lowest_modified < $2 AND greatest_modified >= $3",
		3, argtypes, args, NULL, false, 0);
}

/* ================================================================
 * Get MIN(watermark) across all segments for this CAGG.
 * This is the most conservative materialization boundary — data
 * above this hasn't been materialized on at least one segment.
 * ================================================================ */

static TimestampTz
cagg_get_min_watermark(CaggRefreshInfo *info)
{
	Oid		argtypes[1] = { INT4OID };
	Datum	args[1];
	int		ret;
	bool	isnull;

	args[0] = Int32GetDatum(info->cagg_id);
	ret = SPI_execute_with_args(
		"SELECT MIN(watermark) FROM time_series.cagg_watermark "
		"WHERE cagg_id = $1",
		1, argtypes, args, NULL, true, 1);

	if (ret == SPI_OK_SELECT && SPI_processed > 0)
	{
		TimestampTz wm = DatumGetTimestampTz(
			SPI_getbinval(SPI_tuptable->vals[0],
						  SPI_tuptable->tupdesc, 1, &isnull));
		if (!isnull)
			return wm;
	}

	/* No watermark rows found — catalog is corrupted */
	ereport(ERROR,
			(errcode(ERRCODE_INTERNAL_ERROR),
			 errmsg("watermark not found for continuous aggregate %d",
					info->cagg_id),
			 errhint("The watermark catalog may be corrupted. "
					 "Re-create the watermark rows using "
					 "_cagg_init_segment_watermark().")));
	return DT_NOBEGIN;		/* unreachable, keeps compiler happy */
}

/* ================================================================
 * Main REFRESH procedure entry point.
 *
 * CALL time_series.refresh_continuous_aggregate(
 *     cagg_name text,
 *     window_start timestamptz DEFAULT NULL,
 *     window_end   timestamptz DEFAULT NULL
 * )
 * ================================================================ */

PG_FUNCTION_INFO_V1(cagg_refresh);

Datum
cagg_refresh(PG_FUNCTION_ARGS)
{
	char			cagg_name_buf[NAMEDATALEN];
	TimestampTz		window_start = DT_NOBEGIN;
	TimestampTz		window_end = DT_NOEND;
	bool			force = false;
	CaggRefreshInfo	info;
	DirtyInterval  *intervals = NULL;
	int				n_intervals;
	TimestampTz		max_end = DT_NOBEGIN;
	TimestampTz		current_watermark;
	int				i;

	/* Extract arguments.
	 * Copy cagg_name to a stack buffer so it survives SPI_commit_and_chain
	 * which may reset per-transaction memory contexts in CBDB. */
	if (PG_ARGISNULL(0))
		ereport(ERROR,
				(errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
				 errmsg("continuous aggregate name cannot be NULL")));

	strlcpy(cagg_name_buf, text_to_cstring(PG_GETARG_TEXT_PP(0)),
			NAMEDATALEN);

	if (!PG_ARGISNULL(1))
		window_start = PG_GETARG_TIMESTAMPTZ(1);
	if (!PG_ARGISNULL(2))
		window_end = PG_GETARG_TIMESTAMPTZ(2);

	/*
	 * Optional 4th arg: force.  When true, treat the entire refresh
	 * window as one big invalidation regardless of L2 contents.  Used
	 * to recover from data drift (e.g. trigger missed an INSERT, or
	 * mat table got out of sync via direct manipulation).  Mirrors
	 * TSDB tsl/src/continuous_aggs/refresh.c:648.
	 */
	if (PG_NARGS() >= 4 && !PG_ARGISNULL(3))
		force = PG_GETARG_BOOL(3);

	/* Validate window */
	if (!TIMESTAMP_IS_NOBEGIN(window_start) &&
		!TIMESTAMP_IS_NOEND(window_end) &&
		window_start >= window_end)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("refresh window start must be before end")));

	/*
	 * Ownership check: only the CAGG owner (or a superuser, which
	 * pg_class_ownercheck handles internally) may invoke
	 * refresh_continuous_aggregate.  Without this, any role with
	 * EXECUTE on the procedure (PUBLIC by default for CREATE
	 * PROCEDURE) could refresh another tenant's CAGG, advance its
	 * watermark, and consume scheduler resources.
	 *
	 * Mirrors TimescaleDB's tsl/src/continuous_aggs/refresh.c which
	 * gates refresh on the user view's owner.  TSDB on PG16+ uses
	 * object_ownercheck; on PG14 (our base) the equivalent is
	 * pg_class_ownercheck.
	 *
	 * The check is by name lookup (not via the metadata SPI) so the
	 * "does not exist" path is reached *before* any state read; users
	 * who lack visibility get the same NOTICE as if the cagg was
	 * absent.
	 */
	{
		char		schema_buf[NAMEDATALEN];
		char		name_buf[NAMEDATALEN];
		Oid			ns_oid;
		Oid			view_oid;

		cagg_parse_qualified_name(cagg_name_buf, schema_buf, name_buf);
		ns_oid = get_namespace_oid(schema_buf, true);
		view_oid = OidIsValid(ns_oid)
			? get_relname_relid(name_buf, ns_oid)
			: InvalidOid;

		if (OidIsValid(view_oid) &&
			!pg_class_ownercheck(view_oid, GetUserId()))
			aclcheck_error(ACLCHECK_NOT_OWNER, OBJECT_MATVIEW,
						   cagg_name_buf);
	}

	/* Must run on coordinator */
	if (Gp_role == GP_ROLE_EXECUTE)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("refresh_continuous_aggregate must be called on coordinator")));

	memset(&info, 0, sizeof(info));

	/*
	 * Check that we are in a non-atomic context.  SPI_commit_and_chain()
	 * requires the ability to commit and restart a transaction, which is
	 * only possible when called from a top-level CALL or a DO block.
	 *
	 * Calling from a FUNCTION, TRIGGER, or EXCEPTION block would crash
	 * (SIGSEGV) because those contexts run in atomic/subtransaction mode.
	 * Detect this early and raise a friendly error instead.
	 */
	{
		CallContext *callcontext = (CallContext *) fcinfo->context;

		if (callcontext == NULL ||
			!IsA(callcontext, CallContext) ||
			callcontext->atomic)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("refresh_continuous_aggregate cannot run in an atomic context"),
					 errhint("Call it from a top-level CALL statement or a DO block, "
							 "not from a function, trigger, or exception handler.")));
	}


	/*
	 * Use SPI_OPT_NONATOMIC so we can do SPI_commit_and_chain() to split
	 * the work into two transactions.
	 */
	SPI_connect_ext(SPI_OPT_NONATOMIC);


	/*
	 * In the normal psql path, ExecuteCallStmt's caller (PortalRunUtility)
	 * has an ActiveSnapshot tied to the user's Portal, so SPI's downstream
	 * SQL execution (pquery.c:EnsurePortalSnapshotExists) finds it and
	 * proceeds.
	 *
	 * In the BGW path, however, there is no user Portal and no ActiveSnapshot:
	 * job.c starts a transaction and calls SPI_execute_extended() directly.
	 * Without an active snapshot, our SPI_execute_with_args() calls below
	 * would fail with "cannot execute SQL without an outer snapshot or
	 * portal" (pquery.c:2075).
	 *
	 * Push a transaction snapshot only if one isn't already active.  We
	 * also Pop+Push around SPI_commit_and_chain() below, since that path
	 * tears down the active snapshot stack as part of commit.
	 */
	/*
	 * All callers (psql CALL, run_job, BGW worker) now arrive here with
	 * a valid ActivePortal — the BGW worker creates a transient Portal
	 * before invoking us, mirroring TSDB's tsl/src/bgw_policy/job.c
	 * pattern.  This means snapshot management is delegated to the
	 * Portal infrastructure: PortalRun for psql, our manual setup +
	 * EnsurePortalSnapshotExists for BGW.  No path-specific snapshot
	 * stack management is needed below.
	 */


	/*
	 * Disable ORCA for the duration of this refresh.
	 *
	 * cagg_refresh issues many UPDATE statements against DISTRIBUTED
	 * REPLICATED catalog tables (cagg_invalidation_threshold etc.).
	 * ORCA emits "Operator Update on replicated tables not supported"
	 * and falls back to the Postgres planner, but that fallback path
	 * SIGSEGVs in BGW workers — see signal-11 crash logs and the project
	 * convention in CLAUDE.md ("Always SET optimizer = off for CBDB").
	 *
	 * Forcing the Postgres planner avoids the entire ORCA-fallback path.
	 *
	 * We assign the global directly (instead of set_config_option) because
	 * SET LOCAL via SPI showed no effect under BGW NONATOMIC SPI: the next
	 * SPI_execute_with_args still triggered ORCA NOTICE and the same crash.
	 *
	 * Save/restore via PG_TRY/PG_FINALLY: cagg_refresh is reachable from
	 * three call paths — BGW worker (process exits, no leak), CALL
	 * run_job (stays in user session), direct CALL refresh_continuous_aggregate
	 * (stays in user session).  Without restore, the latter two would
	 * silently flip the caller's optimizer GUC to off for the rest of the
	 * session — a hard-to-diagnose performance regression for any user
	 * still relying on ORCA.  PG_FINALLY ensures restore even on ERROR.
	 */
	{
		extern bool optimizer;
		bool saved_optimizer = optimizer;

		optimizer = false;
		PG_TRY();
		{

	/* ---- Transaction 1: L1 → L2 migration ---- */

	cagg_lookup_metadata(cagg_name_buf, &info);


	cagg_acquire_lock(info.cagg_id);


	/*
	 * Acquire source-level advisory lock to serialize L1→L2 migration
	 * across all CAGGs on the same source table.
	 */
	{
		Oid		lock_argtypes[1] = { INT8OID };
		Datum	lock_args[1];

		lock_args[0] = Int64GetDatum(-(int64) info.source_table_oid);
		SPI_execute_with_args(
			"SELECT pg_advisory_xact_lock($1)",
			1, lock_argtypes, lock_args, NULL, true, 0);
	}

	/*
	 * Acquire a real relation-level lock on cagg_invalidation_threshold
	 * to serialize against concurrent DDL or out-of-band UPDATEs to
	 * the threshold row (e.g. an admin manually rewriting state).
	 *
	 * Mirrors TimescaleDB's invalidation_threshold.c — they take
	 * ShareUpdateExclusiveLock on the relation and RowExclusiveLock
	 * implicitly during the UPDATE.  Without this, advisory locks
	 * alone do not block PG-level DDL or non-cooperating DML.
	 */
	{
		Oid		ns_oid = ht_get_namespace_oid_cached();
		Oid		th_oid = OidIsValid(ns_oid)
			? get_relname_relid("cagg_invalidation_threshold", ns_oid)
			: InvalidOid;

		if (OidIsValid(th_oid))
			LockRelationOid(th_oid, ShareUpdateExclusiveLock);
	}

	/*
	 * Match TSDB tsl/src/continuous_aggs/refresh.c:759 — just the
	 * CAGG name, no window timestamps.  Window detail is logged at
	 * DEBUG1 below (multiple sites with start/end values), so the
	 * NOTICE here stays deterministic for regression tests where
	 * window = now() - offset embeds a real-time timestamp.
	 */
	ereport(NOTICE,
			(errmsg("refreshing continuous aggregate \"%s\"",
					cagg_name_buf)));

	cagg_migrate_l1_to_l2(&info);
	elog(DEBUG1, "cagg \"%s\": L1->L2 migration complete (TX1)", cagg_name_buf);


	/*
	 * Advance invalidation threshold early (before commit) so that
	 * concurrent triggers see the new threshold as soon as TX1 commits.
	 * This matches TimescaleDB's behavior of setting threshold in TX1.
	 *
	 * Estimated threshold:
	 *   - finite window_end  → window_end
	 *   - +infinity (NULL)   → max bucket boundary from source data
	 *
	 * Uses GREATEST to ensure threshold only moves forward.
	 * TX2 will reconcile with the definitive MAX(watermark) later.
	 */
	{
		TimestampTz est_threshold = DT_NOBEGIN;

		if (!TIMESTAMP_IS_NOEND(window_end))
		{
			est_threshold = window_end;
		}
		else
		{
			/*
			 * Query source table for max(time_bucket(width, time)).
			 * Use bucket START (not end) to exclude the "hot bucket"
			 * — the last bucket still receiving data.  This ensures
			 * the hot bucket stays in the live branch of the
			 * UNION ALL view, preventing stale partial aggregates.
			 */
			Oid			max_argtypes[1] = { INTERVALOID };
			Datum		max_args[1];
			int			max_ret;
			bool		max_isnull;
			StringInfoData max_sql;

			max_args[0] = IntervalPGetDatum(info.bucket_width);

			initStringInfo(&max_sql);
			appendStringInfo(&max_sql,
				"SELECT time_series.time_bucket($1, %s) "
				"FROM %s ORDER BY 1 DESC NULLS LAST LIMIT 1",
				quote_identifier(info.bucket_column),
				quote_qualified_identifier(
					get_namespace_name(
						get_rel_namespace(info.source_table_oid)),
					get_rel_name(info.source_table_oid)));

			max_ret = SPI_execute_with_args(max_sql.data,
				1, max_argtypes, max_args, NULL, true, 1);

			if (max_ret == SPI_OK_SELECT && SPI_processed > 0)
			{
				est_threshold = DatumGetTimestampTz(
					SPI_getbinval(SPI_tuptable->vals[0],
								  SPI_tuptable->tupdesc, 1,
								  &max_isnull));
				if (max_isnull)
					est_threshold = DT_NOBEGIN;
			}

			pfree(max_sql.data);
		}

		if (!TIMESTAMP_IS_NOBEGIN(est_threshold))
		{
			Oid		th_argtypes[2] = {TIMESTAMPTZOID, OIDOID};
			Datum	th_args[2];

			th_args[0] = TimestampTzGetDatum(est_threshold);
			th_args[1] = ObjectIdGetDatum(info.source_table_oid);

			SPI_execute_with_args(
				"UPDATE time_series.cagg_invalidation_threshold "
				"SET threshold = GREATEST(threshold, $1) "
				"WHERE source_table_oid = $2",
				2, th_argtypes, th_args, NULL, false, 0);
		}
	}

	SIMPLE_FAULT_INJECTOR("cagg_refresh_before_commit_and_chain");


	/*
	 * Hand off to TX2 via SPI_commit_and_chain.  With a valid
	 * ActivePortal, ForgetPortalSnapshots / push-new-snapshot are
	 * orchestrated by Portal + SPI machinery; we don't manipulate
	 * the snapshot stack manually here.
	 */
	SPI_commit_and_chain();

	SIMPLE_FAULT_INJECTOR("cagg_refresh_after_commit_and_chain");

	/* ---- Transaction 2: Materialize dirty intervals ---- */


	cagg_acquire_lock(info.cagg_id);


	/*
	 * Re-lookup metadata in the new transaction.  The previous transaction's
	 * data is no longer accessible.
	 */
	memset(&info, 0, sizeof(info));
	cagg_lookup_metadata(cagg_name_buf, &info);


	/*
	 * Unified refresh path (aligned with TimescaleDB behavior):
	 *
	 * 1. Gather dirty intervals from L2 (backfill/update/delete)
	 * 2. Add the "unmaterialized range" [MIN(watermark), window_end)
	 *    as an additional dirty interval — this covers new data that
	 *    hasn't been materialized yet
	 * 3. Merge all intervals
	 * 4. Refresh only the merged dirty ranges
	 *
	 * This means NULL, NULL doesn't do a brute-force full rebuild;
	 * instead it processes only what's actually dirty or new.
	 * First refresh (watermark=-infinity) naturally becomes a full
	 * materialization because the unmaterialized range = everything.
	 */

	/*
	 * Inscribed bucket alignment (matches TimescaleDB behavior):
	 * Only refresh COMPLETE buckets that fall entirely within the
	 * user's window.  Partial buckets at the edges are excluded.
	 *
	 *   start → align UP to next bucket boundary (ceiling)
	 *   end   → align DOWN to previous bucket boundary (floor)
	 *
	 * Example: window [00:30, 02:30), 1-hour buckets
	 *   start: 00:30 → ceil to 01:00
	 *   end:   02:30 → floor to 02:00
	 *   refreshes: [01:00, 02:00) — only bucket 01:00
	 */
	if (!TIMESTAMP_IS_NOBEGIN(window_start))
	{
		TimestampTz aligned = cagg_align_to_bucket_start(&info, window_start);
		/* If start is not on a boundary, round UP to next boundary */
		if (aligned < window_start)
			window_start = cagg_align_to_bucket_end(&info, window_start);
		else
			window_start = aligned;  /* already on boundary */
	}

	if (!TIMESTAMP_IS_NOEND(window_end))
	{
		/* Align end DOWN to bucket boundary (floor) */
		window_end = cagg_align_to_bucket_start(&info, window_end);
	}

	/* After alignment, start >= end means no complete buckets in window */
	if (!TIMESTAMP_IS_NOBEGIN(window_start) &&
		!TIMESTAMP_IS_NOEND(window_end) &&
		window_start >= window_end)
	{
		SPI_finish();
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("refresh window too small"),
				 errdetail("The refresh window must cover at least one bucket of data."),
				 errhint("Align the refresh window with the bucket boundaries or use at least two buckets.")));
	}

	/* Get the current lowest watermark across all segments */
	current_watermark = cagg_get_min_watermark(&info);

	/* Gather dirty intervals from L2 */
	n_intervals = cagg_gather_dirty_intervals(&info,
											  window_start, window_end,
											  &intervals);

	/*
	 * force=TRUE: treat the entire refresh window as one big dirty
	 * interval, regardless of what L2 contains.  Mirrors TSDB
	 * invalidation.c:896-908 which pre-seeds an "always-merged"
	 * invalidation entry covering the whole window before merging
	 * with the regular L2 entries.
	 *
	 * The unmat-range append below will merge this entry with any
	 * adjacent L2 entries; downstream cagg_refresh_one_interval
	 * unconditionally DELETEs+INSERTs the buckets, which is the
	 * recovery escape hatch for data drift after the trigger missed
	 * an INSERT or the mat table was manipulated directly.
	 */
	if (force)
	{
		TimestampTz forced_start =
			TIMESTAMP_IS_NOBEGIN(window_start) ? current_watermark : window_start;
		TimestampTz forced_end = window_end;

		/* If start is still -infinity, fall back to actual data boundary
		 * later (max_end clamp); for the synthetic interval here use
		 * the window_start we have. */
		if (!TIMESTAMP_IS_NOEND(forced_end) && forced_start < forced_end)
		{
			elog(LOG, "cagg \"%s\": force refresh window [%s, %s)",
				 cagg_name_buf,
				 TIMESTAMP_IS_NOBEGIN(forced_start) ? "-infinity"
				 : DatumGetCString(DirectFunctionCall1(timestamptz_out,
							 TimestampTzGetDatum(forced_start))),
				 DatumGetCString(DirectFunctionCall1(timestamptz_out,
							 TimestampTzGetDatum(forced_end))));

			intervals = (intervals == NULL)
				? palloc(sizeof(DirtyInterval))
				: repalloc(intervals,
						   sizeof(DirtyInterval) * (n_intervals + 1));
			intervals[n_intervals].start = forced_start;
			intervals[n_intervals].end = forced_end;
			n_intervals++;
		}
	}

	/*
	 * Add the unmaterialized range [watermark, window_end) as an extra
	 * dirty interval.  This ensures new data beyond the watermark gets
	 * materialized even if there are no L2 entries for it.
	 *
	 * Clamp to the user's window: the unmaterialized range might extend
	 * beyond the requested window.
	 */
	{
		TimestampTz unmat_start = current_watermark;
		TimestampTz unmat_end = window_end;

		/* Clamp unmaterialized range to window */
		if (!TIMESTAMP_IS_NOBEGIN(window_start) && unmat_start < window_start)
			unmat_start = window_start;


		/* Only add if the range is non-empty */
		if (TIMESTAMP_IS_NOBEGIN(unmat_start) ||
			TIMESTAMP_IS_NOEND(unmat_end) ||
			unmat_start < unmat_end)
		{
			/* Align to bucket boundaries */
			if (!TIMESTAMP_IS_NOBEGIN(unmat_start))
				unmat_start = cagg_align_to_bucket_start(&info, unmat_start);

			/* Append to intervals array */
			if (intervals == NULL)
				intervals = palloc(sizeof(DirtyInterval));
			else
				intervals = repalloc(intervals,
									sizeof(DirtyInterval) * (n_intervals + 1));

			intervals[n_intervals].start = unmat_start;
			intervals[n_intervals].end = unmat_end;
			n_intervals++;

			/*
			 * Re-sort and re-merge since we added an interval that might
			 * overlap with existing ones.  Simple approach: just sort by
			 * start and merge in-place.
			 */
			{
				DirtyInterval *merged;
				int			num_merged = 0;
				int			j;

				/* Simple bubble sort (n_intervals is typically small) */
				for (i = 0; i < n_intervals - 1; i++)
					for (j = i + 1; j < n_intervals; j++)
						if (intervals[j].start < intervals[i].start)
						{
							DirtyInterval tmp = intervals[i];
							intervals[i] = intervals[j];
							intervals[j] = tmp;
						}

				/* Merge */
				merged = palloc(sizeof(DirtyInterval) * n_intervals);
				for (i = 0; i < n_intervals; i++)
				{
					if (num_merged > 0 &&
						intervals[i].start <= merged[num_merged - 1].end)
					{
						if (intervals[i].end > merged[num_merged - 1].end)
							merged[num_merged - 1].end = intervals[i].end;
					}
					else
					{
						merged[num_merged].start = intervals[i].start;
						merged[num_merged].end = intervals[i].end;
						num_merged++;
					}
				}

				pfree(intervals);
				intervals = merged;
				n_intervals = num_merged;
			}

		}
	}

	/*
	 * If the number of disjoint intervals exceeds the GUC limit,
	 * merge them all into one range [min_start, max_end] to avoid
	 * excessive fragmented refreshes.  0 means unlimited.
	 */
	if (n_intervals > 1 &&
		ts_guc_materializations_per_refresh_window > 0 &&
		n_intervals > ts_guc_materializations_per_refresh_window)
	{
		TimestampTz merged_start = intervals[0].start;
		TimestampTz merged_end = intervals[n_intervals - 1].end;

		/* Find actual max end (intervals are sorted by start, not end) */
		for (i = 0; i < n_intervals; i++)
		{
			if (intervals[i].end > merged_end)
				merged_end = intervals[i].end;
		}

		intervals[0].start = merged_start;
		intervals[0].end = merged_end;
		n_intervals = 1;
	}

	if (n_intervals == 0)
	{
		ereport(NOTICE,
				(errmsg("continuous aggregate \"%s\" is already up-to-date",
						cagg_name_buf)));
	}

	if (n_intervals > 0)
	{
		elog(DEBUG1, "cagg \"%s\": refreshing %d dirty interval%s",
			 cagg_name_buf, n_intervals, n_intervals == 1 ? "" : "s");

		for (i = 0; i < n_intervals; i++)
		{
			elog(DEBUG1, "cagg \"%s\": interval %d/%d [%s, %s)",
				 cagg_name_buf, i + 1, n_intervals,
				 TIMESTAMP_IS_NOBEGIN(intervals[i].start) ? "-infinity"
				 : DatumGetCString(DirectFunctionCall1(timestamptz_out,
							 TimestampTzGetDatum(intervals[i].start))),
				 TIMESTAMP_IS_NOEND(intervals[i].end) ? "+infinity"
				 : DatumGetCString(DirectFunctionCall1(timestamptz_out,
							 TimestampTzGetDatum(intervals[i].end))));

			cagg_refresh_one_interval(&info,
									  intervals[i].start,
									  intervals[i].end);

			if (intervals[i].end > max_end)
				max_end = intervals[i].end;
		}

		/*
		 * Clamp watermark to actual data boundary.
		 *
		 * The refresh window's max_end may be far beyond actual data:
		 *   - NULL,NULL → +infinity
		 *   - Explicit future date → e.g. '2027-01-01'
		 *
		 * Pushing watermark beyond data creates a "blind zone" where
		 * the real-time view's live branch doesn't cover new INSERTs
		 * (both mat and live branches miss them).
		 *
		 * Fix: always query source table for actual max bucket START,
		 * then watermark = MIN(max_end, actual_boundary).
		 *
		 * Use bucket START (not end) to exclude the "hot bucket" —
		 * the last bucket that may still be receiving data.  This
		 * prevents stale partial aggregates from being served by
		 * the mat branch while the live branch is cut away.
		 */
		{
			Oid		max_argtypes[1] = { INTERVALOID };
			Datum	max_args[1];
			int		max_ret;
			bool	max_isnull;
			StringInfoData max_sql;
			TimestampTz actual_boundary = DT_NOBEGIN;

			max_args[0] = IntervalPGetDatum(info.bucket_width);

			initStringInfo(&max_sql);
			appendStringInfo(&max_sql,
				"SELECT time_series.time_bucket($1, %s) "
				"FROM %s ORDER BY 1 DESC NULLS LAST LIMIT 1",
				quote_identifier(info.bucket_column),
				quote_qualified_identifier(
					get_namespace_name(get_rel_namespace(info.source_table_oid)),
					get_rel_name(info.source_table_oid)));

			max_ret = SPI_execute_with_args(max_sql.data,
				1, max_argtypes, max_args, NULL, true, 1);

			if (max_ret == SPI_OK_SELECT && SPI_processed > 0)
			{
				TimestampTz max_data = DatumGetTimestampTz(
					SPI_getbinval(SPI_tuptable->vals[0],
								  SPI_tuptable->tupdesc, 1, &max_isnull));
				if (!max_isnull)
					actual_boundary = max_data;
			}

			pfree(max_sql.data);

			/* Clamp: watermark = MIN(max_end, actual_boundary) */
			if (TIMESTAMP_IS_NOBEGIN(actual_boundary))
				max_end = DT_NOBEGIN;	/* empty source → don't advance */
			else if (TIMESTAMP_IS_NOEND(max_end) || max_end > actual_boundary)
				max_end = actual_boundary;
			/* else: max_end <= actual_boundary → keep max_end as-is */
		}


		SIMPLE_FAULT_INJECTOR("cagg_refresh_before_watermark_advance");

		/*
		 * Decide whether to advance the watermark.
		 *
		 * The mat branch of the real-time UNION ALL view returns rows
		 * with bucket < watermark.  Advancing watermark past source
		 * buckets that are not in the mat table would hide them (mat
		 * branch claims coverage; live branch covers only >= watermark).
		 *
		 * Three cases:
		 *  1. window_start <= current_watermark — window is contiguous
		 *     with what's already materialized; safe to advance.
		 *  2. window_start > current_watermark, but every source bucket
		 *     in the gap [current_watermark, window_start) is present
		 *     in the mat table — advancing hides nothing.  Safe.
		 *  3. window_start > current_watermark and source has buckets
		 *     in the gap not covered by mat — advancing would hide
		 *     them.  Refuse.
		 *
		 * Why query mat directly: L2 (cagg_materialization_log) only
		 * tracks invalidations of buckets that *were* materialized.
		 * Buckets that were never materialized (e.g., on a fresh CAGG
		 * before any prior refresh covered them) leave no L2 trace.
		 * A LEFT JOIN of source-buckets-in-gap against mat catches
		 * both cases.
		 *
		 * For NULL/NULL refresh, window_start is DT_NOBEGIN, which is
		 * always <= any watermark, so case 1 applies and we advance.
		 */
		bool		advance_ok = (TIMESTAMP_IS_NOBEGIN(window_start) ||
								  window_start <= current_watermark);

		if (!advance_ok)
		{
			Oid			gap_argtypes[3] = { INTERVALOID, TIMESTAMPTZOID, TIMESTAMPTZOID };
			Datum		gap_args[3];
			StringInfoData gap_sql;
			int			gap_ret;

			gap_args[0] = IntervalPGetDatum(info.bucket_width);
			gap_args[1] = TimestampTzGetDatum(current_watermark);
			gap_args[2] = TimestampTzGetDatum(window_start);

			initStringInfo(&gap_sql);
			appendStringInfo(&gap_sql,
				"SELECT 1 FROM ("
				"  SELECT DISTINCT time_series.time_bucket($1, %s) AS b "
				"  FROM %s "
				"  WHERE %s >= $2 AND %s < $3"
				") src "
				"WHERE NOT EXISTS ("
				"  SELECT 1 FROM %s.%s mat WHERE mat.%s = src.b"
				") "
				"LIMIT 1",
				quote_identifier(info.bucket_column),
				quote_qualified_identifier(
					get_namespace_name(get_rel_namespace(info.source_table_oid)),
					get_rel_name(info.source_table_oid)),
				quote_identifier(info.bucket_column),
				quote_identifier(info.bucket_column),
				quote_identifier(info.mat_table_schema),
				quote_identifier(info.mat_table_name),
				quote_identifier(info.mat_bucket_col));

			gap_ret = SPI_execute_with_args(gap_sql.data, 3,
											gap_argtypes, gap_args,
											NULL, true, 1);
			pfree(gap_sql.data);

			/* All source buckets in gap are in mat → safe */
			if (gap_ret == SPI_OK_SELECT && SPI_processed == 0)
				advance_ok = true;
		}

		if (advance_ok)
		{
			cagg_advance_watermark(&info, max_end);
			elog(DEBUG1, "cagg \"%s\": watermark advanced to %s",
				 cagg_name_buf,
				 TIMESTAMP_IS_NOBEGIN(max_end) ? "-infinity"
				 : DatumGetCString(DirectFunctionCall1(timestamptz_out,
							 TimestampTzGetDatum(max_end))));
		}
		else
		{
			elog(DEBUG1, "cagg \"%s\": watermark NOT advanced "
				 "(unmaterialized buckets in [%s, %s))",
				 cagg_name_buf,
				 DatumGetCString(DirectFunctionCall1(timestamptz_out,
							 TimestampTzGetDatum(current_watermark))),
				 DatumGetCString(DirectFunctionCall1(timestamptz_out,
							 TimestampTzGetDatum(window_start))));
		}

		/* Trim L2 entries within the refresh window */
		cagg_trim_l2(&info, window_start, window_end);
	}


	if (intervals)
		pfree(intervals);


	/*
	 * Reconcile invalidation threshold with actual MAX(watermark).
	 *
	 * TX1 already advanced threshold to an estimated value (window_end
	 * or max-data boundary) so concurrent triggers benefit immediately.
	 * Now that watermark has been definitively advanced, set threshold
	 * to the authoritative MAX(watermark) across all CAGGs on this
	 * source table.
	 */
	{
		Oid		argtypes[1] = { OIDOID };
		Datum	args[1];

		args[0] = ObjectIdGetDatum(info.source_table_oid);
		SPI_execute_with_args(
			"UPDATE time_series.cagg_invalidation_threshold "
			"SET threshold = COALESCE(("
			"  SELECT MAX(w.watermark) "
			"  FROM time_series.cagg_watermark w "
			"  JOIN time_series.continuous_agg c ON w.cagg_id = c.cagg_id "
			"  WHERE c.source_table_oid = $1"
			"), '-infinity'::timestamptz) "
			"WHERE source_table_oid = $1",
			1, argtypes, args, NULL, false, 0);
	}


	/*
	 * Unified cleanup.  All callers (psql, run_job, BGW worker) have a
	 * valid ActivePortal coordinating snapshot/SPI lifecycle, so a
	 * single SPI_finish suffices.
	 */
	SPI_finish();

		}
		PG_FINALLY();
		{
			optimizer = saved_optimizer;
		}
		PG_END_TRY();
	}

	PG_RETURN_VOID();
}
