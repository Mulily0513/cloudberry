/*
 * cagg_refresh_policy.c
 *    CAGG automatic refresh policy procedure.
 *
 * This is the "policy procedure" registered in bgw_job.proc_name.  When a
 * BGW worker (or run_job caller) picks up a CAGG refresh job, it invokes
 *   CALL time_series.ts_policy_refresh_cagg(job_id, config)
 * via ExecuteCallStmt (see ts_bgw_job_execute_real).  This procedure
 * extracts the CAGG name and offsets from `config`, computes the refresh
 * window relative to the current (or mocked) timestamp, and dispatches
 *   CALL time_series.refresh_continuous_aggregate(name, start, end)
 * via a nested ExecuteCallStmt.
 *
 * Why ExecuteCallStmt and not SPI_execute("CALL ...")?
 *   The inner refresh_continuous_aggregate uses SPI_commit_and_chain to
 *   split into TX1 (L1 → L2 migration) and TX2 (materialization +
 *   watermark advance).  CBDB's SPI CALL path forces
 *   callcontext->atomic = true, which the procedure refuses.  Using
 *   ExecuteCallStmt directly mirrors what psql's top-level CALL does and
 *   gives us NONATOMIC context as needed.  The FuncExpr/Const nodes are
 *   allocated in TopMemoryContext so they survive across the inner
 *   StartTransactionCommand performed by SPI_commit_and_chain.
 *
 * JSONB config format:
 *   { "cagg_name": "view_name",
 *     "start_offset": "1 day",   -- optional; absent ⇒ NULL ⇒ open window
 *     "end_offset": "0" }
 *
 * Copyright (c) 2026 HashData Inc.
 * Licensed under Apache License 2.0
 */
#include <postgres.h>

#include <access/xact.h>
#include <catalog/pg_type.h>
#include <executor/spi.h>
#include <nodes/makefuncs.h>
#include <nodes/parsenodes.h>
#include <parser/parse_func.h>
#include <commands/defrem.h>
#include <tcop/dest.h>
#include <tcop/utility.h>
#include <utils/builtins.h>
#include <utils/jsonb.h>
#include <utils/lsyscache.h>
#include <utils/memutils.h>
#include <utils/snapmgr.h>
#include <utils/timestamp.h>

#include "cagg_refresh_policy.h"
#include "../bgw/timer.h"
#include "../include/time_series.h"		/* TS_EXTENSION_SCHEMA_NAME */

PG_FUNCTION_INFO_V1(ts_policy_refresh_cagg);

/*
 * Extract a text field from a JSONB object (palloc'd in caller context).
 * Returns NULL when the key is missing.
 */
static char *
jsonb_get_text(Jsonb *jb, const char *key)
{
	Datum keydat = CStringGetTextDatum(key);
	Datum val;

	val = DirectFunctionCall2(jsonb_object_field_text,
							  JsonbPGetDatum(jb),
							  keydat);
	if (DatumGetPointer(val) == NULL)
		return NULL;
	return text_to_cstring(DatumGetTextP(val));
}

/*
 * Policy procedure entry point: ts_policy_refresh_cagg(job_id int4, config jsonb)
 */
Datum
ts_policy_refresh_cagg(PG_FUNCTION_ARGS)
{
	int32 job_id = PG_GETARG_INT32(0);
	Jsonb *config = PG_ARGISNULL(1) ? NULL : PG_GETARG_JSONB_P(1);
	char *cagg_name;
	char *start_offset_str;
	char *end_offset_str;
	TimestampTz now_ts;
	TimestampTz window_start_ts = 0;
	TimestampTz window_end_ts = 0;
	bool ws_isnull = true;
	bool we_isnull = true;
	Oid procoid;
	Const *arg0, *arg1, *arg2, *arg3;
	FuncExpr *funcexpr;
	CallStmt *call;
	DestReceiver *dest;
	ParamListInfo params;
	MemoryContext oldctx;
	MemoryContext policy_ctx;

	if (config == NULL)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("job %d has NULL config", job_id)));

	/* Extract parameters from JSONB config */
	cagg_name = jsonb_get_text(config, "cagg_name");
	if (cagg_name == NULL)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("job %d config missing \"cagg_name\"", job_id)));

	start_offset_str = jsonb_get_text(config, "start_offset");
	end_offset_str = jsonb_get_text(config, "end_offset");

	/*
	 * Compute refresh window relative to current (mockable) time.
	 * NULL offset → open boundary on that side.
	 */
	now_ts = ts_timer_get_current_timestamp();
	if (start_offset_str != NULL)
	{
		Interval *si = DatumGetIntervalP(
			DirectFunctionCall3(interval_in,
								CStringGetDatum(start_offset_str),
								ObjectIdGetDatum(InvalidOid),
								Int32GetDatum(-1)));
		window_start_ts = DatumGetTimestampTz(
			DirectFunctionCall2(timestamptz_mi_interval,
								TimestampTzGetDatum(now_ts),
								IntervalPGetDatum(si)));
		ws_isnull = false;
	}
	if (end_offset_str != NULL)
	{
		Interval *ei = DatumGetIntervalP(
			DirectFunctionCall3(interval_in,
								CStringGetDatum(end_offset_str),
								ObjectIdGetDatum(InvalidOid),
								Int32GetDatum(-1)));
		window_end_ts = DatumGetTimestampTz(
			DirectFunctionCall2(timestamptz_mi_interval,
								TimestampTzGetDatum(now_ts),
								IntervalPGetDatum(ei)));
		we_isnull = false;
	}

	elog(DEBUG1, "running CAGG refresh policy: job_id=%d cagg=\"%s\" "
		 "window=[%s, %s) (offsets: start=%s end=%s)",
		 job_id, cagg_name,
		 ws_isnull ? "-infinity"
		 : DatumGetCString(DirectFunctionCall1(timestamptz_out,
					 TimestampTzGetDatum(window_start_ts))),
		 we_isnull ? "+infinity"
		 : DatumGetCString(DirectFunctionCall1(timestamptz_out,
					 TimestampTzGetDatum(window_end_ts))),
		 start_offset_str ? start_offset_str : "NULL",
		 end_offset_str ? end_offset_str : "NULL");

	/*
	 * Build inner CallStmt for refresh_continuous_aggregate(text, ts, ts).
	 *
	 * Allocate the CallStmt and its FuncExpr/Const argument nodes in a
	 * dedicated sub-context of TopMemoryContext so they (a) survive the
	 * inner cagg_refresh's StartTransactionCommand / SPI_commit_and_chain
	 * (which destroy and recreate CurTransactionContext mid-call), and
	 * (b) are reclaimed in one MemoryContextDelete after ExecuteCallStmt
	 * returns — without this sub-context, the makeNode/list_make/makeConst
	 * allocations would leak into TopMemoryContext on every invocation.
	 *
	 * TSDB uses the saved parent_ctx pattern (see tsl/src/bgw_policy/job.c
	 * ::job_execute), which is also what src/bgw/job.c::ts_bgw_job_execute_real
	 * does.  Here we cannot use the caller's parent_ctx because the
	 * NONATOMIC inner CALL flushes that context across SPI_commit_and_chain.
	 */
	policy_ctx = AllocSetContextCreate(TopMemoryContext,
									   "ts_policy_refresh_cagg",
									   ALLOCSET_SMALL_SIZES);
	oldctx = MemoryContextSwitchTo(policy_ctx);

	{
		ObjectWithArgs *obj = makeNode(ObjectWithArgs);
		obj->objname = list_make2(makeString(TS_EXTENSION_SCHEMA_NAME),
								  makeString("refresh_continuous_aggregate"));
		obj->objargs = list_make4(SystemTypeName("text"),
								  SystemTypeName("timestamptz"),
								  SystemTypeName("timestamptz"),
								  SystemTypeName("bool"));
		procoid = LookupFuncWithArgs(OBJECT_ROUTINE, obj, false);
	}

	arg0 = makeConst(TEXTOID, -1, InvalidOid, -1,
					 CStringGetTextDatum(cagg_name), false, false);
	arg1 = ws_isnull
		? (Const *) makeNullConst(TIMESTAMPTZOID, -1, InvalidOid)
		: makeConst(TIMESTAMPTZOID, -1, InvalidOid, 8,
					TimestampTzGetDatum(window_start_ts), false, true);
	arg2 = we_isnull
		? (Const *) makeNullConst(TIMESTAMPTZOID, -1, InvalidOid)
		: makeConst(TIMESTAMPTZOID, -1, InvalidOid, 8,
					TimestampTzGetDatum(window_end_ts), false, true);
	/*
	 * Policy-driven refresh always runs with force=FALSE.  Force is the
	 * recovery escape hatch for manual data-drift repair, not a
	 * scheduled-job behavior.
	 */
	arg3 = makeConst(BOOLOID, -1, InvalidOid, 1,
					 BoolGetDatum(false), false, true);

	funcexpr = makeFuncExpr(procoid, VOIDOID,
							list_make4(arg0, arg1, arg2, arg3),
							InvalidOid, InvalidOid, COERCE_EXPLICIT_CALL);
	call = makeNode(CallStmt);
	call->funcexpr = funcexpr;
	dest = CreateDestReceiver(DestNone);
	params = makeParamList(0);

	MemoryContextSwitchTo(oldctx);

	/*
	 * Reclaim policy_ctx in one shot whether ExecuteCallStmt succeeds or
	 * raises ERROR.  Without PG_FINALLY the cleanup is skipped on the
	 * error path and policy_ctx leaks into TopMemoryContext until the
	 * worker process exits.
	 */
	PG_TRY();
	{
		ExecuteCallStmt(call, params, false /* atomic */, dest);
	}
	PG_FINALLY();
	{
		MemoryContextDelete(policy_ctx);
	}
	PG_END_TRY();

	pfree(cagg_name);
	if (start_offset_str) pfree(start_offset_str);
	if (end_offset_str) pfree(end_offset_str);

	PG_RETURN_VOID();
}
