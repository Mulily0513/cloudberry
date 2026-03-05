/*
 * time_series.h - Internal header for time_series PG extension
 *
 * Copyright (c) 2026 HashData Inc.
 * Licensed under Apache License 2.0
 */
#ifndef TIME_SERIES_H
#define TIME_SERIES_H

#include "postgres.h"
#include "fmgr.h"
#include "nodes/extensible.h"
#include "nodes/pathnodes.h"
#include "optimizer/pathnode.h"
#include "optimizer/planner.h"
#include "utils/relcache.h"

/* Cached namespace OID (time_series.c) */
extern Oid ht_get_namespace_oid_cached(void);

/* ---- time_bucket functions (time_bucket.c) ---- */

extern Datum ts_int16_bucket(PG_FUNCTION_ARGS);
extern Datum ts_int32_bucket(PG_FUNCTION_ARGS);
extern Datum ts_int64_bucket(PG_FUNCTION_ARGS);
extern Datum ts_timestamp_bucket(PG_FUNCTION_ARGS);
extern Datum ts_timestamp_offset_bucket(PG_FUNCTION_ARGS);
extern Datum ts_timestamptz_bucket(PG_FUNCTION_ARGS);
extern Datum ts_timestamptz_offset_bucket(PG_FUNCTION_ARGS);
extern Datum ts_timestamptz_timezone_bucket(PG_FUNCTION_ARGS);
extern Datum ts_date_bucket(PG_FUNCTION_ARGS);
extern Datum ts_date_offset_bucket(PG_FUNCTION_ARGS);

/* ---- GapFill (gapfill.c / gapfill_plan.c / gapfill_exec.c) ---- */

/* Marker function: passthrough, used by locf() and interpolate() */
extern Datum ht_gapfill_marker(PG_FUNCTION_ARGS);

/* GapFill bucket functions (wrappers that call ts_*_bucket internally) */
extern Datum ht_gapfill_timestamp_bucket(PG_FUNCTION_ARGS);
extern Datum ht_gapfill_timestamptz_bucket(PG_FUNCTION_ARGS);
extern Datum ht_gapfill_int16_bucket(PG_FUNCTION_ARGS);
extern Datum ht_gapfill_int32_bucket(PG_FUNCTION_ARGS);
extern Datum ht_gapfill_int64_bucket(PG_FUNCTION_ARGS);
extern Datum ht_gapfill_date_bucket(PG_FUNCTION_ARGS);
extern Datum ht_gapfill_timestamptz_timezone_bucket(PG_FUNCTION_ARGS);

/* GapFill Custom Scan registration (called from _PG_init) */
extern void ht_gapfill_scan_init(void);

/* GapFill CreateCustomScanState callback (gapfill_exec.c) */
extern Node *ht_gapfill_create_state(CustomScan *cscan);

/* GapFill Planner hook registration (called from _PG_init) */
extern void ht_gapfill_planner_init(void);

/* GapFill Custom Scan Methods (shared between plan and exec) */
extern const CustomScanMethods ht_gapfill_scan_methods;

#endif /* TIME_SERIES_H */
