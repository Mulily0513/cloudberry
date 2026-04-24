/*
 * This file and its contents are licensed under the Apache License 2.0.
 * Please see the included NOTICE for copyright information and
 * LICENSE-APACHE for a copy of the license.
 *
 * Portions Copyright (c) 2025-2026, HashData Technology Limited.
 */
#ifndef BGW_JOB_STAT_H
#define BGW_JOB_STAT_H

#include "job.h"

#define LAST_CRASH_REPORTED 1

typedef struct BgwJobStat
{
	FormData_bgw_job_stat fd;
} BgwJobStat;

extern BgwJobStat *ts_bgw_job_stat_find(int job_id);
extern void ts_bgw_job_stat_mark_start(BgwJob *job);
extern void ts_bgw_job_stat_mark_end(BgwJob *job, JobResult result, Jsonb *edata);
extern bool ts_bgw_job_stat_end_was_marked(BgwJobStat *jobstat);

extern void ts_bgw_job_stat_set_next_start(int32 job_id, TimestampTz next_start);
extern bool ts_bgw_job_stat_update_next_start(int32 job_id, TimestampTz next_start,
											  bool allow_unset);

extern TimestampTz ts_bgw_job_stat_next_start(BgwJobStat *jobstat, BgwJob *job,
											  int32 consecutive_failed_launches);
extern void ts_bgw_job_stat_mark_crash_reported(BgwJob *job, JobResult result);

extern TimestampTz ts_get_next_scheduled_execution_slot(BgwJob *job,
														TimestampTz finish_time);

#endif /* BGW_JOB_STAT_H */
