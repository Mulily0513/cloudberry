/*-------------------------------------------------------------------------
 *
 * ts_compat.h
 *    Compatibility layer replacing TimescaleDB internal headers.
 *
 *    Provides FormData structs, macros, and declarations that the copied
 *    TSDB BGW code depends on, adapted for the time_series extension
 *    on CloudberryDB.
 *
 * Portions Copyright (c) 2025-2026, HashData Technology Limited.
 * Portions Copyright (c) 2018-2025, Timescale, Inc.
 *
 * Licensed under the Apache License, Version 2.0.
 *-------------------------------------------------------------------------
 */
#ifndef TS_COMPAT_H
#define TS_COMPAT_H

#include <postgres.h>
#include <utils/timestamp.h>
#include <utils/jsonb.h>

/*
 * Replace TS_DEBUG — enable in debug builds only.
 */
#ifdef USE_ASSERT_CHECKING
#define TS_DEBUG 1
#endif

/* ----------------------------------------------------------------
 * Schema / table name constants
 *
 * TSDB uses _timescaledb_config / _timescaledb_internal.
 * We use time_series schema for everything.
 * ----------------------------------------------------------------
 */
#define BGW_SCHEMA_NAME		"time_series"
#define BGW_JOB_TABLE		"bgw_job"
#define BGW_JOB_STAT_TABLE	"bgw_job_stat"
#define BGW_JOB_STAT_HISTORY_TABLE "bgw_job_stat_history"

/* Fully qualified table names for SPI */
#define BGW_JOB_TABLE_FQ		BGW_SCHEMA_NAME "." BGW_JOB_TABLE
#define BGW_JOB_STAT_TABLE_FQ	BGW_SCHEMA_NAME "." BGW_JOB_STAT_TABLE
#define BGW_JOB_STAT_HISTORY_TABLE_FQ BGW_SCHEMA_NAME "." BGW_JOB_STAT_HISTORY_TABLE

/* ----------------------------------------------------------------
 * FormData structs — match the SQL table definitions
 *
 * These replace TSDB's auto-generated FormData types from
 * ts_catalog/catalog.h.
 * ----------------------------------------------------------------
 */
typedef struct FormData_bgw_job
{
	int32		id;
	NameData	application_name;
	Interval	schedule_interval;
	Interval	max_runtime;
	int32		max_retries;
	Interval	retry_period;
	NameData	proc_schema;
	NameData	proc_name;
	Oid			owner;
	bool		scheduled;
	bool		fixed_schedule;
	TimestampTz initial_start;
	int32		hypertable_id;		/* maps to cagg_id in our context */
	Jsonb	   *config;
	NameData	check_schema;
	NameData	check_name;
	text	   *timezone;
} FormData_bgw_job;

typedef struct FormData_bgw_job_stat
{
	int32		id;					/* job_id */
	TimestampTz last_start;
	TimestampTz last_finish;
	TimestampTz next_start;
	TimestampTz last_successful_finish;
	bool		last_run_success;
	int64		total_runs;
	Interval	total_duration;
	Interval	total_duration_failures;
	int64		total_successes;
	int64		total_failures;
	int64		total_crashes;
	int32		consecutive_failures;
	int32		consecutive_crashes;
	int32		flags;
} FormData_bgw_job_stat;

/* ----------------------------------------------------------------
 * Advisory lock helpers
 *
 * Replace TSDB's TS_SET_LOCKTAG_ADVISORY macro.
 * ----------------------------------------------------------------
 */
#define TS_SET_LOCKTAG_ADVISORY(tag, id1, id2) \
	SET_LOCKTAG_ADVISORY((tag), MyDatabaseId, (uint32)(id1), (uint32)(id2), 0)

/* ----------------------------------------------------------------
 * Utility macros / stubs for removed TSDB features
 * ----------------------------------------------------------------
 */

/*
 * Capture an ErrorData into a JSONB object for storage in
 * bgw_job_stat_history.data.  Mirrors TimescaleDB's ts_errdata_to_jsonb
 * (utils.c).  Captured fields: sqlerrcode, message, detail, hint, filename,
 * lineno, funcname, domain, context_domain, context, schema_name,
 * table_name, column_name, datatype_name, constraint_name, internalquery,
 * detail_log, plus proc_schema / proc_name from the caller (so analytics
 * views don't need to re-join bgw_job).
 *
 * Returns NULL if edata is NULL — callers (build_history_data) treat that
 * as "no error_data for this row" and skip the JSONB key entirely.
 *
 * Implementation lives in src/bgw/errdata.c.
 */
#include <utils/elog.h>		/* ErrorData */
extern Jsonb *ts_errdata_to_jsonb(ErrorData *edata,
								  Name proc_schema, Name proc_name);

/* Replace TSDB GUC variables we reference */
extern int ts_guc_bgw_log_level;

/* Replace ts_shutdown_bgw (debug flag) */
extern bool ts_shutdown_bgw;

/* ----------------------------------------------------------------
 * CBDB-specific: pull in Gp_role / GP_ROLE_* used throughout BGW code
 * ----------------------------------------------------------------
 */
#ifdef GP_VERSION_NUM
#include "cdb/cdbvars.h"
#endif

/* ----------------------------------------------------------------
 * SPI wrapper with snapshot management
 *
 * CBDB background workers require an active snapshot for SPI
 * execution. Unlike vanilla PG where SPI_connect handles snapshots
 * automatically, CBDB's executor checks for an outer snapshot.
 *
 * Use these wrappers around SPI_connect/SPI_finish in BGW code.
 * ----------------------------------------------------------------
 */
#include <executor/spi.h>
#include <utils/snapmgr.h>

static inline int
ts_spi_connect(void)
{
	int ret = SPI_connect();
	if (ret == SPI_OK_CONNECT)
		PushActiveSnapshot(GetTransactionSnapshot());
	return ret;
}

static inline int
ts_spi_finish(void)
{
	PopActiveSnapshot();
	return SPI_finish();
}

#endif /* TS_COMPAT_H */
