/*
 * This file and its contents are licensed under the Apache License 2.0.
 * Please see the included NOTICE for copyright information and
 * LICENSE-APACHE for a copy of the license.
 *
 * Adapted from TimescaleDB test/src/bgw/timer_mock.c.
 *
 * Changes vs upstream:
 *   - Drop annotations.h (replace TS_FALLTHROUGH with __attribute__).
 *   - Drop bgw/launcher_interface.h (we don't use launcher).
 *   - Drop scanner.h, ts_catalog/catalog.h (no catalog scans here).
 *   - Path adjustments for include "params.h" / "timer_mock.h".
 */
#include <postgres.h>
#include <postmaster/bgworker.h>
#include <storage/latch.h>
#include <storage/proc.h>
#include <utils/memutils.h>

#include "params.h"
#include "timer_mock.h"
#include "../../src/bgw/timer.h"

#ifndef pg_attribute_fallthrough
#define pg_attribute_fallthrough() __attribute__((fallthrough))
#endif

static List *bgw_handles = NIL;

static bool mock_wait(TimestampTz until);
static TimestampTz mock_current_time(void);

const Timer ts_mock_timer = {
	.get_current_timestamp = mock_current_time,
	.wait = mock_wait,
};

void
ts_timer_mock_register_bgw_handle(BackgroundWorkerHandle *handle,
								  MemoryContext scheduler_mctx)
{
	elog(WARNING, "[TESTING] Registered new background worker");
	MemoryContext old_context = MemoryContextSwitchTo(scheduler_mctx);
	bgw_handles = lappend(bgw_handles, handle);
	MemoryContextSwitchTo(old_context);
}

/*
 * WARNING: mock_wait must _only_ be called from the bgw_scheduler.  Calling
 * it from a worker will clobber the timer state.
 */
static bool
mock_wait(TimestampTz until)
{
	elog(WARNING,
		 "[TESTING] Wait until " INT64_FORMAT ", started at " INT64_FORMAT,
		 (int64) until,
		 (int64) ts_params_get()->current_time);

	ListCell *lc;

	switch (ts_params_get()->mock_wait_type)
	{
		case WAIT_ON_JOB:
			foreach (lc, bgw_handles)
			{
				BackgroundWorkerHandle *bgw_handle = lfirst(lc);
				WaitForBackgroundWorkerShutdown(bgw_handle);
			}
			bgw_handles = NIL;
			pg_attribute_fallthrough();
		case IMMEDIATELY_SET_UNTIL:
			ts_params_set_time(until, false);
			return true;
		case WAIT_FOR_OTHER_TO_ADVANCE:
			/* Wait for another process to set "next time" */
			ts_reset_and_wait_timer_latch();
			return true;
		case WAIT_FOR_STANDARD_WAITLATCH:
			ts_get_standard_timer()->wait(until);
			return true;
		default:
			return false;
	}
}

static TimestampTz
mock_current_time(void)
{
	return (TimestampTz) ts_params_get()->current_time;
}
