/*
 * This file and its contents are licensed under the Apache License 2.0.
 * Please see the included NOTICE for copyright information and
 * LICENSE-APACHE for a copy of the license.
 */
#pragma once

#include <postgres.h>

#include <postmaster/bgworker.h>

/**
 * Parameters to background workers.
 *
 * Do not add data here that cannot be simply copied to the background worker
 * using memcpy(3). If it is necessary to add fields that cannot simply be
 * copied, we need to start using the send and recv functions for the types.
 *
 * The `bgw_main` is the function to execute when starting the job.
 *
 * @see ts_bgw_job_entrypoint
 */
typedef struct BgwParams
{
	/** User oid to run the job as. Used when initializing the database
	 * connection. */
	Oid user_oid;

	/** Job id to use for the worker when executing the job */
	int32 job_id;

	/** Job history information to use for the worker when recording the job execution */
	int64 job_history_id;
	TimestampTz job_history_execution_start;

	/** Name of function to call when starting the background worker. */
	char bgw_main[BGW_MAXLEN];
} BgwParams;

/**
 * Compile-time check to ensure that the size of BgwParams fit into the bgw_extra field
 * of BackgroundWorker.
 */
StaticAssertDecl(sizeof(BgwParams) <= sizeof(((BackgroundWorker *) 0)->bgw_extra),
				 "sizeof(BgwParams) exceeds sizeof(bgw_extra) field of BackgroundWorker");
