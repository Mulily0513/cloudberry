/*
 * This file and its contents are licensed under the Apache License 2.0.
 * Please see the included NOTICE for copyright information and
 * LICENSE-APACHE for a copy of the license.
 *
 * Portions Copyright (c) 2025-2026, HashData Technology Limited.
 *
 * Simplified worker slot management.  TSDB bridges between a loader .so
 * and the main extension via load_external_function + shared memory
 * counters.  We are a single .so, so a simple static counter suffices.
 *
 * The GUC time_series.bgw_max_workers controls the maximum.
 */
#include <postgres.h>

#include "launcher_interface.h"

extern int ts_guc_bgw_max_workers;		/* defined in scheduler.c */

static int num_reserved = 0;

bool
ts_bgw_worker_reserve(void)
{
	if (num_reserved >= ts_guc_bgw_max_workers)
		return false;
	num_reserved++;
	return true;
}

void
ts_bgw_worker_release(void)
{
	Assert(num_reserved > 0);
	num_reserved--;
}
