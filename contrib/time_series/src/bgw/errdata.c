/*-------------------------------------------------------------------------
 *
 * errdata.c
 *    Capture ErrorData fields into a JSONB object for the BGW job audit
 *    log (bgw_job_stat_history.data).  Without this, every failed job
 *    leaves only "see server logs" — DBA cannot diagnose policies from
 *    SQL alone.
 *
 *    Mirrors TimescaleDB's ts_errdata_to_jsonb in src/utils.c.  Captures
 *    17 ErrorData fields plus proc_schema / proc_name so the
 *    time_series.job_history view can render full failure context.
 *
 * Copyright (c) 2026 HashData Inc.
 * Licensed under Apache License 2.0
 *
 * IDENTIFICATION
 *    contrib/time_series/src/bgw/errdata.c
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>

#include <utils/builtins.h>		/* unpack_sql_state */
#include <utils/elog.h>
#include <utils/jsonb.h>

#include "ts_compat.h"

/* ----------------------------------------------------------------
 * Push a single string-typed key/value pair into the JSONB builder.
 * No-op if val is NULL (skips the key entirely), so callers can pass
 * optional ErrorData fields without conditional logic at every site.
 * ----------------------------------------------------------------
 */
static void
push_str(JsonbParseState **state, const char *key, const char *val)
{
	JsonbValue	jbkey;
	JsonbValue	jbval;

	if (val == NULL)
		return;

	jbkey.type = jbvString;
	jbkey.val.string.val = (char *) key;
	jbkey.val.string.len = strlen(key);
	pushJsonbValue(state, WJB_KEY, &jbkey);

	jbval.type = jbvString;
	jbval.val.string.val = (char *) val;
	jbval.val.string.len = strlen(val);
	pushJsonbValue(state, WJB_VALUE, &jbval);
}

/* ----------------------------------------------------------------
 * Push an int4 key/value pair.  Skips if val == 0 (mirrors TSDB which
 * skips edata->lineno when it is 0; line 0 is never a real source line).
 * ----------------------------------------------------------------
 */
static void
push_int(JsonbParseState **state, const char *key, int32 val)
{
	JsonbValue	jbkey;
	JsonbValue	jbval;
	Numeric		numval;

	if (val == 0)
		return;

	jbkey.type = jbvString;
	jbkey.val.string.val = (char *) key;
	jbkey.val.string.len = strlen(key);
	pushJsonbValue(state, WJB_KEY, &jbkey);

	numval = DatumGetNumeric(DirectFunctionCall1(int4_numeric,
												 Int32GetDatum(val)));
	jbval.type = jbvNumeric;
	jbval.val.numeric = numval;
	pushJsonbValue(state, WJB_VALUE, &jbval);
}

/* ----------------------------------------------------------------
 * Build a JSONB object from an ErrorData captured via CopyErrorData.
 * Returns NULL if edata is NULL (no error to record).
 *
 * Memory context: allocates result via JsonbValueToJsonb in the
 * current context.  Caller is typically inside SPI / a per-tx context;
 * the resulting Jsonb is consumed immediately by the SPI INSERT into
 * bgw_job_stat_history.
 * ----------------------------------------------------------------
 */
Jsonb *
ts_errdata_to_jsonb(ErrorData *edata, Name proc_schema, Name proc_name)
{
	JsonbParseState *parse_state = NULL;
	JsonbValue *result;

	if (edata == NULL)
		return NULL;

	pushJsonbValue(&parse_state, WJB_BEGIN_OBJECT, NULL);

	if (edata->sqlerrcode)
		push_str(&parse_state, "sqlerrcode",
				 unpack_sql_state(edata->sqlerrcode));
	push_str(&parse_state, "message", edata->message);
	push_str(&parse_state, "detail", edata->detail);
	push_str(&parse_state, "hint", edata->hint);
	push_str(&parse_state, "filename", edata->filename);
	push_int(&parse_state, "lineno", edata->lineno);
	push_str(&parse_state, "funcname", edata->funcname);
	push_str(&parse_state, "domain", edata->domain);
	push_str(&parse_state, "context_domain", edata->context_domain);
	push_str(&parse_state, "context", edata->context);
	push_str(&parse_state, "schema_name", edata->schema_name);
	push_str(&parse_state, "table_name", edata->table_name);
	push_str(&parse_state, "column_name", edata->column_name);
	push_str(&parse_state, "datatype_name", edata->datatype_name);
	push_str(&parse_state, "constraint_name", edata->constraint_name);
	push_str(&parse_state, "internalquery", edata->internalquery);
	push_str(&parse_state, "detail_log", edata->detail_log);

	if (proc_schema != NULL && strlen(NameStr(*proc_schema)) > 0)
		push_str(&parse_state, "proc_schema", NameStr(*proc_schema));
	if (proc_name != NULL && strlen(NameStr(*proc_name)) > 0)
		push_str(&parse_state, "proc_name", NameStr(*proc_name));

	result = pushJsonbValue(&parse_state, WJB_END_OBJECT, NULL);
	return JsonbValueToJsonb(result);
}
