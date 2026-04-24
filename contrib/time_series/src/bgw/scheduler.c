/*
 * This file and its contents are licensed under the Apache License 2.0.
 * Please see the included NOTICE for copyright information and
 * LICENSE-APACHE for a copy of the license.
 *
 * Portions Copyright (c) 2025-2026, HashData Technology Limited.
 *
 * Adapted from TimescaleDB: removed TSDB-specific dependencies,
 * added CloudberryDB Gp_role support.
 */
/*
 * This is a scheduler that takes background jobs and schedules them appropriately
 *
 * Limitations: For now the jobs are only loaded when the scheduler starts and are not
 * updated if the jobs table changes
 *
 */
#include <postgres.h>

#include <access/xact.h>
#include <miscadmin.h>
#include <nodes/pg_list.h>
#include <pgstat.h>
#include <postmaster/bgworker.h>
#include <storage/ipc.h>
#include <storage/latch.h>
#include <storage/lwlock.h>
#include <storage/proc.h>
#include <storage/shmem.h>
#include <tcop/tcopprot.h>
#include <utils/acl.h>
#include <utils/builtins.h>
#include <utils/inval.h>
#include <utils/jsonb.h>
#include <utils/lsyscache.h>
#include <catalog/namespace.h>
#include <utils/memutils.h>
#include <utils/snapmgr.h>
#include <utils/timestamp.h>

#include "ts_compat.h"
#include "job.h"
#include "job_stat.h"
#include "launcher_interface.h"
#include "scheduler.h"
#include "timer.h"
#include "worker.h"
#include "../include/time_series.h"

/* ----------------------------------------------------------------
 * GUC variables
 *
 * These are registered via DefineCustomXxxVariable() in _PG_init().
 * Declared extern in ts_compat.h so other translation units can
 * reference them.
 * ----------------------------------------------------------------
 */
int ts_guc_bgw_log_level = WARNING;
bool ts_shutdown_bgw = false;
int ts_guc_bgw_max_workers = 4;
/*
 * CAGG policy defaults.  Mirror TimescaleDB upstream behavior
 * (max_retries=-1, max_runtime='0') by default — TSDB explicitly
 * comments "unlimited for now" — but expose them as GUCs so operators
 * who want safer defaults (e.g. cap retries at 20, kill runs over 30
 * minutes) can set them globally without patching SQL.
 */
int ts_guc_cagg_default_max_retries = -1;
char *ts_guc_cagg_default_max_runtime = "30 min";

/* BGW enable/database GUCs (PGC_POSTMASTER / PGC_SIGHUP) */
static bool ts_guc_bgw_enabled = false;
static char *ts_guc_bgw_db = NULL;

/*
 * ts_debug_bgw_scheduler_exit_status: exit code for proc_exit().
 *
 * Note: ts_guc_restoring is defined in time_series.c (registered as
 * the time_series.restoring bool GUC) and re-used here so the
 * scheduler exits immediately when a logical dump is being replayed.
 */
int ts_debug_bgw_scheduler_exit_status = 0;

#define START_RETRY_MS (1 * INT64CONST(1000)) /* 1 seconds */
#define ONE_SECOND_IN_MICROSECONDS 1000000

static TimestampTz
least_timestamp(TimestampTz left, TimestampTz right)
{
	return (left < right ? left : right);
}

PG_FUNCTION_INFO_V1(ts_bgw_scheduler_main);

/*
 * Global so the invalidate cache message can set. Don't need to protect
 * access with a lock because it's accessed only by the scheduler process.
 */
static bool jobs_list_needs_update;


/* has to be global to shutdown jobs on exit */
static List *scheduled_jobs = NIL;

static MemoryContext scheduler_mctx;
static MemoryContext scratch_mctx;

/* See the README for a state transition diagram */
typedef enum JobState
{
	/* terminal state for now. Later we may have path to JOB_STATE_SCHEDULED */
	JOB_STATE_DISABLED,

	/*
	 * This is the initial state. next states: JOB_STATE_STARTED,
	 * JOB_STATE_DISABLED. This job is not running and has been scheduled to
	 * be started at a later time.
	 */
	JOB_STATE_SCHEDULED,

	/*
	 * next states: JOB_STATE_TERMINATING, JOB_STATE_SCHEDULED. This job has
	 * been started by the scheduler and is either running or finished (and
	 * the finish has not yet been detected by the scheduler).
	 */
	JOB_STATE_STARTED,

	/*
	 * next states: JOB_STATE_SCHEDULED. The scheduler has explicitly sent a
	 * terminate to this job but has not yet detected that it has stopped.
	 */
	JOB_STATE_TERMINATING
} JobState;

typedef struct ScheduledBgwJob
{
	BgwJob job;
	TimestampTz next_start;
	TimestampTz timeout_at;
	JobState state;
	BackgroundWorkerHandle *handle;

	bool reserved_worker;

	/*
	 * We say "may" here since under normal circumstances the job itself will
	 * perform the mark_end
	 */
	bool may_need_mark_end;
	int32 consecutive_failed_launches;
} ScheduledBgwJob;

static void on_failure_to_start_job(ScheduledBgwJob *sjob);

static volatile sig_atomic_t got_SIGHUP = false;

BackgroundWorkerHandle *
ts_bgw_start_worker(const char *name, const BgwParams *bgw_params)
{
	BackgroundWorker worker = {
		.bgw_flags = BGWORKER_SHMEM_ACCESS | BGWORKER_BACKEND_DATABASE_CONNECTION,
		.bgw_start_time = BgWorkerStart_RecoveryFinished,
		.bgw_restart_time = BGW_NEVER_RESTART,
		.bgw_notify_pid = MyProcPid,
		.bgw_main_arg = ObjectIdGetDatum(MyDatabaseId),
	};
	BackgroundWorkerHandle *handle = NULL;

	strlcpy(worker.bgw_name, name, BGW_MAXLEN);
	strlcpy(worker.bgw_library_name, TS_EXTENSION_NAME, BGW_MAXLEN);
	strlcpy(worker.bgw_function_name, bgw_params->bgw_main, BGW_MAXLEN);

	memcpy(worker.bgw_extra, bgw_params, sizeof(*bgw_params));

	/* handle needs to be allocated in long-lived memory context */
	MemoryContextSwitchTo(scheduler_mctx);
	if (!RegisterDynamicBackgroundWorker(&worker, &handle))
	{
		elog(NOTICE, "unable to register background worker");
		handle = NULL;
	}
	MemoryContextSwitchTo(scratch_mctx);

	return handle;
}

#ifdef USE_ASSERT_CHECKING
static void
assert_that_worker_has_stopped(ScheduledBgwJob *sjob)
{
	pid_t pid;
	BgwHandleStatus status;

	Assert(sjob->reserved_worker);
	status = GetBackgroundWorkerPid(sjob->handle, &pid);
	Assert(BGWH_STOPPED == status);
}
#endif

static void
mark_job_as_started(ScheduledBgwJob *sjob)
{
	Assert(!sjob->may_need_mark_end);
	sjob->consecutive_failed_launches = 0;
	ts_bgw_job_stat_mark_start(&sjob->job);
	sjob->may_need_mark_end = true;
}

static void
mark_job_as_ended(ScheduledBgwJob *sjob, JobResult res, Jsonb *edata)
{
	Assert(sjob->may_need_mark_end);
	ts_bgw_job_stat_mark_end(&sjob->job, res, edata);
	sjob->may_need_mark_end = false;
}

static ErrorData *
makeJobErrorData(ScheduledBgwJob *sjob, JobResult res)
{
	ErrorData *edata = (ErrorData *) palloc0(sizeof(ErrorData));
	edata->elevel = ERROR;
	edata->sqlerrcode = ERRCODE_INTERNAL_ERROR;
	edata->hint = NULL;

	Assert(res != JOB_SUCCESS);

	switch (res)
	{
		case JOB_FAILURE_TO_START:
			edata->message = "failed to start job";
			edata->detail = psprintf("Job %d (\"%s\") failed to start",
									 sjob->job.fd.id,
									 NameStr(sjob->job.fd.application_name));
			break;
		case JOB_FAILURE_IN_EXECUTION:
			edata->message = "failed to execute job";
			edata->detail = psprintf("Job %d (\"%s\") failed to execute.",
									 sjob->job.fd.id,
									 NameStr(sjob->job.fd.application_name));
			break;
		default:
			pg_unreachable();
			break;
	}

	return edata;
}

static void
worker_state_cleanup(ScheduledBgwJob *sjob)
{
	/*
	 * This function needs to be safe wrt failures occurring at any point in
	 * the job starting process.
	 */
	if (sjob->handle != NULL)
	{
#ifdef USE_ASSERT_CHECKING
		/* Sanity check: worker has stopped (if it was started) */
		assert_that_worker_has_stopped(sjob);
#endif
		pfree(sjob->handle);
		sjob->handle = NULL;
	}

	/*
	 * first cleanup reserved workers before accessing db. Want to minimize
	 * the possibility of errors before worker is released
	 */
	if (sjob->reserved_worker)
	{
		ts_bgw_worker_release();
		sjob->reserved_worker = false;
	}

	if (sjob->may_need_mark_end)
	{
		BgwJobStat *job_stat;

		if (!ts_bgw_job_get_share_lock(sjob->job.fd.id, CurrentMemoryContext))
		{
			elog(WARNING,
				 "scheduler detected that job %d was deleted after job quit",
				 sjob->job.fd.id);
			ts_bgw_job_cache_invalidate_callback();
			sjob->may_need_mark_end = false;
			return;
		}

		job_stat = ts_bgw_job_stat_find(sjob->job.fd.id);

		if (job_stat == NULL)
		{
			/*
			 * stat row gone — typically the user did
			 *   remove_continuous_aggregate_policy(...)
			 * which deletes bgw_job_stat first, then bgw_job, while
			 * we (the scheduler) were holding only the share lock on
			 * bgw_job and the worker had just exited (crash / SIGTERM /
			 * OOM kill) without running mark_end.  We got past the
			 * share-lock check above because bgw_job is REPLICATED and
			 * the user's DELETE on it is still blocked waiting for our
			 * lock — but bgw_job_stat is hash-distributed and not
			 * gated by the same advisory lock, so its DELETE went
			 * through.
			 *
			 * Nothing to mark — the job is being torn down anyway.
			 * Without this guard the next line dereferences NULL via
			 * ts_bgw_job_stat_end_was_marked → SIGSEGV in the
			 * scheduler process.  Mirrors the share-lock-failed branch
			 * a few lines up.
			 */
			elog(WARNING,
				 "scheduler detected that job %d stats were deleted "
				 "after worker exited",
				 sjob->job.fd.id);
			ts_bgw_job_cache_invalidate_callback();
			sjob->may_need_mark_end = false;
			return;
		}

		if (!ts_bgw_job_stat_end_was_marked(job_stat))
		{
			/*
			 * Usually the job process will mark the end, but if the job gets
			 * a signal (cancel or terminate), it won't be able to so we
			 * should.
			 */
			elog(LOG, "job %d failed", sjob->job.fd.id);
			ErrorData *edata = makeJobErrorData(sjob, JOB_FAILURE_IN_EXECUTION);
			mark_job_as_ended(sjob,
							  JOB_FAILURE_IN_EXECUTION,
							  ts_errdata_to_jsonb(edata,
												  &sjob->job.fd.proc_schema,
												  &sjob->job.fd.proc_name));
		}
		else
		{
			sjob->may_need_mark_end = false;
		}
	}
}

/* Set the state of the job.
 * This function is responsible for setting all of the variables in ScheduledBgwJob
 * except for the job itself.
 */
static void
scheduled_bgw_job_transition_state_to(ScheduledBgwJob *sjob, JobState new_state)
{
#ifdef USE_ASSERT_CHECKING
	JobState prev_state = sjob->state;
#endif

	BgwJobStat *job_stat;

	switch (new_state)
	{
		case JOB_STATE_DISABLED:
			Assert(prev_state == JOB_STATE_STARTED || prev_state == JOB_STATE_TERMINATING);
			sjob->handle = NULL;
			break;
		case JOB_STATE_SCHEDULED:
			/* prev_state can be any value, including itself */

			worker_state_cleanup(sjob);

			/*
			 * SPI-based stat lookup needs a transaction context.
			 * TSDB's original scanner didn't need this.
			 *
			 * This code path is reached both from within a transaction
			 * (during ts_update_scheduled_jobs_list) and outside one
			 * (from check_for_stopped_and_timed_out_jobs), so we
			 * conditionally start/commit.
			 */
			{
				bool need_txn = !IsTransactionState();
				if (need_txn)
					StartTransactionCommand();

				job_stat = ts_bgw_job_stat_find(sjob->job.fd.id);

				Assert(!sjob->reserved_worker);
				sjob->next_start =
					ts_bgw_job_stat_next_start(job_stat, &sjob->job, sjob->consecutive_failed_launches);

				if (need_txn)
				{
					CommitTransactionCommand();
					MemoryContextSwitchTo(scratch_mctx);
				}
			}
			break;
		case JOB_STATE_STARTED:
			Assert(prev_state == JOB_STATE_SCHEDULED);
			Assert(sjob->handle == NULL);
			Assert(!sjob->reserved_worker);

			StartTransactionCommand();

			if (!ts_bgw_job_get_share_lock(sjob->job.fd.id, CurrentMemoryContext))
			{
				elog(WARNING,
					 "scheduler detected that job %d was deleted when starting job",
					 sjob->job.fd.id);
				ts_bgw_job_cache_invalidate_callback();
				CommitTransactionCommand();
				MemoryContextSwitchTo(scratch_mctx);
				return;
			}

			/* If we are unable to reserve a worker go back to the scheduled state */
			sjob->reserved_worker = ts_bgw_worker_reserve();
			if (!sjob->reserved_worker)
			{
				elog(WARNING,
					 "failed to launch job %d \"%s\": out of background workers",
					 sjob->job.fd.id,
					 NameStr(sjob->job.fd.application_name));
				sjob->consecutive_failed_launches++;
				scheduled_bgw_job_transition_state_to(sjob, JOB_STATE_SCHEDULED);
				CommitTransactionCommand();
				MemoryContextSwitchTo(scratch_mctx);
				return;
			}

			/*
			 * start the job before you can encounter any errors so that they
			 * are always registered
			 */
			mark_job_as_started(sjob);
			if (ts_bgw_job_has_timeout(&sjob->job))
				sjob->timeout_at =
					ts_bgw_job_timeout_at(&sjob->job, ts_timer_get_current_timestamp());
			else
				sjob->timeout_at = DT_NOEND;

			CommitTransactionCommand();
			MemoryContextSwitchTo(scratch_mctx);

			elog(DEBUG1,
				 "launching job %d \"%s\"",
				 sjob->job.fd.id,
				 NameStr(sjob->job.fd.application_name));

			sjob->handle = ts_bgw_job_start(&sjob->job, sjob->job.fd.owner);
			if (sjob->handle == NULL)
			{
				elog(WARNING,
					 "failed to launch job %d \"%s\": failed to start a background worker",
					 sjob->job.fd.id,
					 NameStr(sjob->job.fd.application_name));
				on_failure_to_start_job(sjob);
				return;
			}
			Assert(sjob->reserved_worker);
			break;
		case JOB_STATE_TERMINATING:
			Assert(prev_state == JOB_STATE_STARTED);
			Assert(sjob->handle != NULL);
			Assert(sjob->reserved_worker);
			TerminateBackgroundWorker(sjob->handle);
			break;
	}
	sjob->state = new_state;
}

static void
on_failure_to_start_job(ScheduledBgwJob *sjob)
{
	StartTransactionCommand();
	if (!ts_bgw_job_get_share_lock(sjob->job.fd.id, CurrentMemoryContext))
	{
		elog(WARNING,
			 "scheduler detected that job %d was deleted while failing to start",
			 sjob->job.fd.id);
		ts_bgw_job_cache_invalidate_callback();
	}
	else
	{
		/* restore the original next_start to maintain priority (it is unset during mark_start) */
		if (sjob->next_start != DT_NOBEGIN)
			ts_bgw_job_stat_set_next_start(sjob->job.fd.id, sjob->next_start);
		ErrorData *edata = makeJobErrorData(sjob, JOB_FAILURE_TO_START);
		mark_job_as_ended(sjob,
						  JOB_FAILURE_TO_START,
						  ts_errdata_to_jsonb(edata,
											  &sjob->job.fd.proc_schema,
											  &sjob->job.fd.proc_name));
	}
	scheduled_bgw_job_transition_state_to(sjob, JOB_STATE_SCHEDULED);
	CommitTransactionCommand();
	MemoryContextSwitchTo(scratch_mctx);
}

static inline void
bgw_scheduler_on_postmaster_death(void)
{
	/*
	 * Don't call exit hooks cause we want to bail out quickly. We don't care
	 * about cleaning up shared memory in this case anyway since it's
	 * potentially corrupt.
	 */
	on_exit_reset();
	ereport(FATAL,
			(errcode(ERRCODE_ADMIN_SHUTDOWN),
			 errmsg("postmaster exited while time_series scheduler was working")));
}

/*
 * This function starts a job.
 * To correctly count crashes we need to mark the start of a job in a separate
 * txn before we kick off the actual job. Thus this function cannot be run
 * from within a transaction.
 */
static void
scheduled_ts_bgw_job_start(ScheduledBgwJob *sjob,
						   register_background_worker_callback_type bgw_register)
{
	pid_t pid;
	BgwHandleStatus status;

	scheduled_bgw_job_transition_state_to(sjob, JOB_STATE_STARTED);

	if (sjob->state != JOB_STATE_STARTED)
		return;

	Assert(sjob->handle != NULL);
	if (bgw_register != NULL)
		bgw_register(sjob->handle, scheduler_mctx);

	status = WaitForBackgroundWorkerStartup(sjob->handle, &pid);
	switch (status)
	{
		case BGWH_POSTMASTER_DIED:
			bgw_scheduler_on_postmaster_death();
			break;
		case BGWH_STARTED:
			/* all good */
			break;
		case BGWH_STOPPED:
			StartTransactionCommand();
			scheduled_bgw_job_transition_state_to(sjob, JOB_STATE_SCHEDULED);
			CommitTransactionCommand();
			MemoryContextSwitchTo(scratch_mctx);
			break;
		case BGWH_NOT_YET_STARTED:
			/* should not be possible */
			elog(ERROR, "unexpected bgworker state %d", status);
			break;
	}
}

static void
terminate_and_cleanup_job(ScheduledBgwJob *sjob)
{
	if (sjob->handle != NULL)
	{
		TerminateBackgroundWorker(sjob->handle);
		WaitForBackgroundWorkerShutdown(sjob->handle);
	}
	sjob->may_need_mark_end = false;
	worker_state_cleanup(sjob);
}

/*
 *  Update the given job list with whatever is in the bgw_job table. For overlapping jobs,
 *  copy over any existing scheduler info from the given jobs list.
 *  Assume that both lists are ordered by job ID.
 *  Note that this function call will destroy cur_jobs_list and return a new list.
 */
List *
ts_update_scheduled_jobs_list(List *cur_jobs_list, MemoryContext mctx)
{
	List *new_jobs = ts_bgw_job_get_scheduled(sizeof(ScheduledBgwJob), mctx);
	ListCell *new_ptr = list_head(new_jobs);
	ListCell *cur_ptr = list_head(cur_jobs_list);

	elog(DEBUG2, "updating scheduled jobs list");

	while (cur_ptr != NULL && new_ptr != NULL)
	{
		ScheduledBgwJob *new_sjob = lfirst(new_ptr);
		ScheduledBgwJob *cur_sjob = lfirst(cur_ptr);

		if (cur_sjob->job.fd.id < new_sjob->job.fd.id)
		{
			/*
			 * We don't need cur_sjob anymore. Make sure to clean up the job
			 * state. Then keep advancing cur pointer until we catch up.
			 */
			terminate_and_cleanup_job(cur_sjob);

			cur_ptr = lnext(cur_jobs_list, cur_ptr);
			continue;
		}
		if (cur_sjob->job.fd.id == new_sjob->job.fd.id)
		{
			/*
			 * Then this job already exists. Copy over any state and advance
			 * both pointers.
			 */
			cur_sjob->job = new_sjob->job;
			*new_sjob = *cur_sjob;

			/* reload the scheduling information from the job_stats */
			if (cur_sjob->state == JOB_STATE_SCHEDULED)
				scheduled_bgw_job_transition_state_to(new_sjob, JOB_STATE_SCHEDULED);

			cur_ptr = lnext(cur_jobs_list, cur_ptr);
			new_ptr = lnext(new_jobs, new_ptr);
		}
		else if (cur_sjob->job.fd.id > new_sjob->job.fd.id)
		{
			scheduled_bgw_job_transition_state_to(new_sjob, JOB_STATE_SCHEDULED);
			elog(DEBUG1,
				 "sjob %d was new, its fixed_schedule is %d",
				 new_sjob->job.fd.id,
				 new_sjob->job.fd.fixed_schedule);

			/* Advance the new_job list until we catch up to cur_list */
			new_ptr = lnext(new_jobs, new_ptr);
		}
	}

	/* If there's more stuff in cur_list, clean it all up */
	if (cur_ptr != NULL)
	{
		ListCell *ptr;

		for_each_cell (ptr, cur_jobs_list, cur_ptr)
			terminate_and_cleanup_job(lfirst(ptr));
	}

	if (new_ptr != NULL)
	{
		/* Then there are more new jobs. Initialize all of them. */
		ListCell *ptr;

		for_each_cell (ptr, new_jobs, new_ptr)
			scheduled_bgw_job_transition_state_to(lfirst(ptr), JOB_STATE_SCHEDULED);
	}

	/* Free the old list */
	list_free_deep(cur_jobs_list);
	return new_jobs;
}

static int
cmp_next_start(const ListCell *left_cell, const ListCell *right_cell)
{
	ScheduledBgwJob *left_sjob = lfirst(left_cell);
	ScheduledBgwJob *right_sjob = lfirst(right_cell);

	if (left_sjob->next_start < right_sjob->next_start)
		return -1;

	if (left_sjob->next_start > right_sjob->next_start)
		return 1;

	return 0;
}

static void
start_scheduled_jobs(register_background_worker_callback_type bgw_register)
{
	List *ordered_scheduled_jobs;
	ListCell *lc;
	Assert(CurrentMemoryContext == scratch_mctx);

	/* Order jobs by increasing next_start */
	/* list_sort does in-place sort - so make a copy and sort that */
	ordered_scheduled_jobs = list_copy(scheduled_jobs);
	list_sort(ordered_scheduled_jobs, cmp_next_start);

	foreach (lc, ordered_scheduled_jobs)
	{
		ScheduledBgwJob *sjob = lfirst(lc);

		int64 job_start_diff = sjob->next_start - ts_timer_get_current_timestamp();

		if (sjob->state == JOB_STATE_SCHEDULED &&
			(job_start_diff <= 0 || sjob->next_start == DT_NOBEGIN))
		{
			elog(DEBUG2, "starting scheduled job %d", sjob->job.fd.id);
			scheduled_ts_bgw_job_start(sjob, bgw_register);
		}
		else
		{
			elog(DEBUG5,
				 "starting scheduled job %d in " INT64_FORMAT " seconds",
				 sjob->job.fd.id,
				 job_start_diff / ONE_SECOND_IN_MICROSECONDS);
		}
	}

	list_free(ordered_scheduled_jobs);
}

/* Returns the earliest time the scheduler should start a job that is waiting to be started */
static TimestampTz
earliest_wakeup_to_start_next_job()
{
	ListCell *lc;
	TimestampTz earliest = DT_NOEND;
	TimestampTz now = ts_timer_get_current_timestamp();

	foreach (lc, scheduled_jobs)
	{
		ScheduledBgwJob *sjob = lfirst(lc);

		if (sjob->state == JOB_STATE_SCHEDULED)
		{
			TimestampTz start = sjob->next_start;
			/* if the start is less than now, this means we tried and failed to start it already, so
			 * use the retry period */
			if (start < now)
				start = TimestampTzPlusMilliseconds(now, START_RETRY_MS);
			earliest = least_timestamp(earliest, start);
		}
	}
	return earliest;
}

/* Returns the earliest time the scheduler needs to kill a job according to its timeout  */
static TimestampTz
earliest_job_timeout()
{
	ListCell *lc;
	TimestampTz earliest = DT_NOEND;

	foreach (lc, scheduled_jobs)
	{
		ScheduledBgwJob *sjob = lfirst(lc);

		if (sjob->state == JOB_STATE_STARTED)
			earliest = least_timestamp(earliest, sjob->timeout_at);
	}
	return earliest;
}

/* Special exit function only used in shmem_exit_callback.
 * Do not call the normal cleanup function (worker_state_cleanup), because
 * 1) we do not wait for the BGW to terminate,
 * 2) we cannot access the database at this time, so we should not be
 *    trying to update the bgw_stat table.
 */
static void
terminate_all_jobs_and_release_workers()
{
	ListCell *lc;

	foreach (lc, scheduled_jobs)
	{
		ScheduledBgwJob *sjob = lfirst(lc);

		/*
		 * Clean up the background workers. Don't worry about state of the
		 * sjobs, because this callback might have interrupted a state
		 * transition.
		 */
		if (sjob->handle != NULL)
			TerminateBackgroundWorker(sjob->handle);

		if (sjob->reserved_worker)
		{
			ts_bgw_worker_release();
			sjob->reserved_worker = false;
		}
	}
}

static void
wait_for_all_jobs_to_shutdown()
{
	ListCell *lc;

	foreach (lc, scheduled_jobs)
	{
		ScheduledBgwJob *sjob = lfirst(lc);

		if (sjob->state == JOB_STATE_STARTED || sjob->state == JOB_STATE_TERMINATING)
			WaitForBackgroundWorkerShutdown(sjob->handle);
	}
}

static void
check_for_stopped_and_timed_out_jobs()
{
	ListCell *lc;

	foreach (lc, scheduled_jobs)
	{
		BgwHandleStatus status;
		pid_t pid;
		ScheduledBgwJob *sjob = lfirst(lc);
		TimestampTz now = ts_timer_get_current_timestamp();

		if (sjob->state != JOB_STATE_STARTED && sjob->state != JOB_STATE_TERMINATING)
			continue;

		status = GetBackgroundWorkerPid(sjob->handle, &pid);

		switch (status)
		{
			case BGWH_POSTMASTER_DIED:
				bgw_scheduler_on_postmaster_death();
				break;
			case BGWH_NOT_YET_STARTED:
				elog(ERROR, "unexpected bgworker state %d", status);
				break;
			case BGWH_STARTED:
				/* still running */
				if (sjob->state == JOB_STATE_STARTED && now >= sjob->timeout_at)
				{
					elog(WARNING,
						 "terminating background worker \"%s\" due to timeout",
						 NameStr(sjob->job.fd.application_name));
					scheduled_bgw_job_transition_state_to(sjob, JOB_STATE_TERMINATING);
					Assert(sjob->state != JOB_STATE_STARTED);
				}
				break;
			case BGWH_STOPPED:
				StartTransactionCommand();
				scheduled_bgw_job_transition_state_to(sjob, JOB_STATE_SCHEDULED);
				CommitTransactionCommand();
				MemoryContextSwitchTo(scratch_mctx);
				Assert(sjob->state != JOB_STATE_STARTED);
				break;
		}
	}
}

/* This is the guts of the scheduler which runs the main loop.
 * The parameter ttl_ms gives a maximum time to run the loop (after which
 * the loop will exit). This functionality is used to ease testing.
 * In production, ttl_ms should be < 0 to signal that the loop should
 * run forever (or until the process gets a signal).
 *
 * The scheduler uses 2 memory contexts for its operation: scheduler_mctx
 * for long-lived objects and scratch_mctx for short-lived objects.
 * After every iteration of the scheduling main loop scratch_mctx gets
 * reset. Special care needs to be taken in regards to memory contexts
 * since StartTransactionCommand creates and switches to a transaction
 * memory context which gets deleted on CommitTransactionCommand which
 * switches CurrentMemoryContext back to TopMemoryContext. So operations
 * wrapped in Start/CommitTransactionCommit will not happen in scratch_mctx
 * but will get freed on CommitTransactionCommand.
 */
void
ts_bgw_scheduler_process(int32 run_for_interval_ms,
						 register_background_worker_callback_type bgw_register)
{
	TimestampTz start = ts_timer_get_current_timestamp();
	TimestampTz quit_time = DT_NOEND;

	log_min_messages = ts_guc_bgw_log_level;

	pgstat_report_activity(STATE_RUNNING, NULL);

	/* If we are restoring or upgrading, don't schedule anything. Just
	 * exit. */
	if (ts_guc_restoring || IsBinaryUpgrade)
	{
		ereport(LOG,
				errmsg("scheduler for database %u exiting with exit status %d",
					   MyDatabaseId,
					   ts_debug_bgw_scheduler_exit_status),
				errdetail("the database is restoring or upgrading"));
		terminate_all_jobs_and_release_workers();
		goto scheduler_exit;
	}

	/* txn to read the list of jobs from the DB.  Tolerate failure (e.g.,
	 * extension not yet installed in this database, or the bgw_job table
	 * missing during DROP EXTENSION + CREATE EXTENSION cycles in tests):
	 * leave scheduled_jobs at NIL and retry on next iteration.  Without
	 * this PG_TRY the scheduler would crash and require postmaster
	 * restart, which has a long backoff. */
	StartTransactionCommand();
	PG_TRY();
	{
		scheduled_jobs = ts_update_scheduled_jobs_list(scheduled_jobs, scheduler_mctx);
		CommitTransactionCommand();
	}
	PG_CATCH();
	{
		EmitErrorReport();
		FlushErrorState();
		AbortCurrentTransaction();
		scheduled_jobs = NIL;
		jobs_list_needs_update = true;	/* retry next iteration */
	}
	PG_END_TRY();
	MemoryContextSwitchTo(scratch_mctx);

	jobs_list_needs_update = false;

	if (run_for_interval_ms > 0)
		quit_time = TimestampTzPlusMilliseconds(start, run_for_interval_ms);

	elog(DEBUG1, "database scheduler for database %u starting", MyDatabaseId);
	elog(LOG, "time_series scheduler: loaded %d jobs for database %u",
		 list_length(scheduled_jobs), MyDatabaseId);

	/*
	 * on SIGTERM the process will usually die from the CHECK_FOR_INTERRUPTS
	 * in the die() called from the sigterm handler. Child reaping is then
	 * handled in the before_shmem_exit,
	 * bgw_scheduler_before_shmem_exit_callback.
	 */
	while (quit_time > ts_timer_get_current_timestamp() && !ProcDiePending && !ts_shutdown_bgw)
	{
		TimestampTz next_wakeup = quit_time;
		Assert(CurrentMemoryContext == scratch_mctx);

		start_scheduled_jobs(bgw_register);
		next_wakeup = least_timestamp(next_wakeup, earliest_wakeup_to_start_next_job());
		next_wakeup = least_timestamp(next_wakeup, earliest_job_timeout());

		pgstat_report_activity(STATE_IDLE, NULL);
		ts_timer_wait(next_wakeup);
		pgstat_report_activity(STATE_RUNNING, NULL);

		CHECK_FOR_INTERRUPTS();

		if (got_SIGHUP)
		{
			got_SIGHUP = false;
			ProcessConfigFile(PGC_SIGHUP);
			log_min_messages = ts_guc_bgw_log_level;
		}

		/*
		 * Process any cache invalidation message that indicates we need to
		 * update the jobs list.  This delivers any pending invalidations
		 * to bgw_job_relcache_callback (registered in
		 * ts_bgw_scheduler_setup_callbacks), which sets
		 * jobs_list_needs_update if relevant.
		 */
		AcceptInvalidationMessages();

		/*
		 * jobs_list_needs_update is set by bgw_job_relcache_callback when
		 * any relcache invalidation message arrives.  Add/remove/alter
		 * policy paths explicitly broadcast such an invalidation via
		 * time_series.bgw_invalidate_cache() (CacheInvalidateRelcacheByRelid
		 * on bgw_job), so a freshly-inserted policy is picked up on the
		 * next scheduler wakeup without the heavy belt-and-suspenders
		 * "reload every iteration" that the V1 prototype used.
		 */
		if (jobs_list_needs_update)
		{
			StartTransactionCommand();
			Assert(CurrentMemoryContext == CurTransactionContext);
			PG_TRY();
			{
				scheduled_jobs = ts_update_scheduled_jobs_list(scheduled_jobs, scheduler_mctx);
				CommitTransactionCommand();
			}
			PG_CATCH();
			{
				/* See comment on the same pattern at startup above. */
				EmitErrorReport();
				FlushErrorState();
				AbortCurrentTransaction();
				scheduled_jobs = NIL;
			}
			PG_END_TRY();
			MemoryContextSwitchTo(scratch_mctx);
			jobs_list_needs_update = false;
		}

		check_for_stopped_and_timed_out_jobs();

		MemoryContextReset(scratch_mctx);
	}

	elog(DEBUG1,
		 "scheduler for database %u exiting with exit status %d",
		 MyDatabaseId,
		 ts_debug_bgw_scheduler_exit_status);

#ifdef TS_DEBUG
	if (ts_shutdown_bgw)
		elog(WARNING, "bgw scheduler stopped due to shutdown_bgw guc");
#endif

scheduler_exit:
	CHECK_FOR_INTERRUPTS();

	wait_for_all_jobs_to_shutdown();
	check_for_stopped_and_timed_out_jobs();
	scheduled_jobs = NIL;
	proc_exit(ts_debug_bgw_scheduler_exit_status);
}

static void
bgw_scheduler_before_shmem_exit_callback(int code, Datum arg)
{
	terminate_all_jobs_and_release_workers();
}

/*
 * Relcache invalidation callback: fires in this backend (the scheduler
 * process) whenever any other backend commits a change that invalidates
 * a relcache entry.  We mark the job list dirty unconditionally — the
 * scheduler is a low-frequency process and reloading its short job list
 * on unrelated invalidations is far cheaper than the alternative
 * (resolving and caching the bgw_job OID, which fights with extension
 * drop/recreate).  The actual SELECT only happens on the next iteration
 * if jobs_list_needs_update is still true.
 */
static void
bgw_job_relcache_callback(Datum arg, Oid relid)
{
	jobs_list_needs_update = true;
}

void
ts_bgw_scheduler_setup_callbacks()
{
	before_shmem_exit(bgw_scheduler_before_shmem_exit_callback, PointerGetDatum(NULL));

	/*
	 * Subscribe to relcache invalidations so the scheduler reloads its
	 * job list immediately after any other backend INSERT/UPDATE/DELETEs
	 * on time_series.bgw_job — without this, a freshly-added policy
	 * would only be picked up on the scheduler's next long sleep wakeup.
	 */
	CacheRegisterRelcacheCallback(bgw_job_relcache_callback, (Datum) 0);
}

/* some of the scheduler mock code calls functions from this file without going through
 * the main loop so we need a way to setup the memory contexts
 */
void
ts_bgw_scheduler_setup_mctx()
{
	scheduler_mctx = AllocSetContextCreate(TopMemoryContext, "Scheduler", ALLOCSET_DEFAULT_SIZES);
	scratch_mctx =
		AllocSetContextCreate(scheduler_mctx, "SchedulerScratch", ALLOCSET_DEFAULT_SIZES);
	MemoryContextSwitchTo(scratch_mctx);
}

static void
handle_sighup(SIGNAL_ARGS)
{
	/* based on av_sighup_handler */
	int save_errno = errno;

	got_SIGHUP = true;
	SetLatch(MyLatch);

	errno = save_errno;
}

/*
 * Register SIGTERM and SIGHUP handlers for bgw_scheduler.
 * This function _must_ be called with signals blocked, i.e., after calling
 * BackgroundWorkerBlockSignals
 */
void
ts_bgw_scheduler_register_signal_handlers(void)
{
	/*
	 * do not use the default `bgworker_die` sigterm handler because it does
	 * not respect critical sections
	 */
	pqsignal(SIGTERM, die);
	pqsignal(SIGHUP, handle_sighup);

	/* Some SIGHUPS may already have been dropped, so we must load the file here */
	got_SIGHUP = false;
	ProcessConfigFile(PGC_SIGHUP);
	log_min_messages = ts_guc_bgw_log_level;
}

Datum
ts_bgw_scheduler_main(PG_FUNCTION_ARGS)
{
	BackgroundWorkerBlockSignals();
	/* Setup any signal handlers here */
	ts_bgw_scheduler_register_signal_handlers();
	BackgroundWorkerUnblockSignals();

	ts_bgw_scheduler_setup_callbacks();

	/*
	 * Connect to the configured database. Without this, SPI cannot be used.
	 * TSDB's original code had the launcher do this, but we merged
	 * launcher + scheduler into one process.
	 */
	if (ts_guc_bgw_db == NULL || ts_guc_bgw_db[0] == '\0')
	{
		elog(WARNING, "time_series.bgw_db is not configured, scheduler exiting");
		PG_RETURN_VOID();
	}
	BackgroundWorkerInitializeConnection(ts_guc_bgw_db, NULL, 0);

	pgstat_report_appname(SCHEDULER_APPNAME);

#ifdef GP_VERSION_NUM
	Gp_role = GP_ROLE_DISPATCH;
#endif

	ts_bgw_scheduler_setup_mctx();

	ts_bgw_scheduler_process(-1, NULL);

	Assert(scheduled_jobs == NIL);
	MemoryContextSwitchTo(TopMemoryContext);
	MemoryContextDelete(scheduler_mctx);

	PG_RETURN_VOID();
};

void
ts_bgw_job_cache_invalidate_callback()
{
	jobs_list_needs_update = true;
}

/*
 * ts_bgw_invalidate_cache -- SQL-callable: broadcast a relcache
 * invalidation message for time_series.bgw_job, so any running scheduler
 * will reload its job list on its next iteration.  Called from
 * add_continuous_aggregate_policy / remove_continuous_aggregate_policy /
 * alter_job after they INSERT/UPDATE/DELETE rows in bgw_job.  Plain DML
 * does not trigger relcache invalidations on its own (PG fires those on
 * structural DDL only), so this is the explicit signal.  Mirrors TSDB's
 * proxy-table invalidation pattern (see timescaledb/src/cache_invalidate.c).
 */
PG_FUNCTION_INFO_V1(ts_bgw_invalidate_cache);
Datum
ts_bgw_invalidate_cache(PG_FUNCTION_ARGS)
{
	Oid nsp_oid;
	Oid bgw_job_oid;

	nsp_oid = get_namespace_oid(TS_EXTENSION_SCHEMA_NAME, true);
	if (!OidIsValid(nsp_oid))
		PG_RETURN_VOID();
	bgw_job_oid = get_relname_relid("bgw_job", nsp_oid);
	if (OidIsValid(bgw_job_oid))
		CacheInvalidateRelcacheByRelid(bgw_job_oid);
	PG_RETURN_VOID();
}

/* ================================================================
 * GUC registration and BGW scheduler registration
 * Called from _PG_init() in time_series.c
 * ================================================================
 */

void
ts_bgw_define_gucs(void)
{
	DefineCustomBoolVariable("time_series.bgw_enabled",
							 "Enable the background worker scheduler",
							 "When true, a scheduler background worker is started "
							 "on the coordinator to automatically run scheduled jobs. "
							 "Requires restart.",
							 &ts_guc_bgw_enabled,
							 false,		/* default: disabled */
							 PGC_POSTMASTER,
							 0,
							 NULL, NULL, NULL);

	DefineCustomStringVariable("time_series.bgw_db",
							   "Database for the background worker scheduler",
							   "The scheduler connects to this database to read job "
							   "definitions and execute scheduled jobs.",
							   &ts_guc_bgw_db,
							   "",		/* default: empty = not configured */
							   PGC_SIGHUP,
							   0,
							   NULL, NULL, NULL);

	DefineCustomIntVariable("time_series.bgw_max_workers",
							"Maximum number of parallel background job workers",
							NULL,
							&ts_guc_bgw_max_workers,
							4,		/* default */
							1,		/* min */
							16,		/* max */
							PGC_POSTMASTER,
							0,
							NULL, NULL, NULL);

	/*
	 * Default max_retries for newly-created CAGG refresh policies.
	 * -1 = unlimited (matches TimescaleDB upstream behavior).  Operators
	 * who want failing policies to auto-pause can SET this to a positive
	 * value (e.g. 20) so consecutive_failures >= max_retries unschedules
	 * the policy.  Existing policies are not retroactively changed —
	 * use alter_job(jid, max_retries => N) per policy if needed.
	 */
	DefineCustomIntVariable("time_series.cagg_default_max_retries",
							"Default max_retries for new CAGG refresh policies",
							"-1 means unlimited retries (TSDB-compatible default). "
							"Set to a positive integer to auto-pause policies after "
							"that many consecutive failures.",
							&ts_guc_cagg_default_max_retries,
							-1,		/* default */
							-1,		/* min */
							INT_MAX,/* max */
							PGC_USERSET,
							0,
							NULL, NULL, NULL);

	/*
	 * Default max_runtime for newly-created CAGG refresh policies.
	 * '0' = no timeout (matches TimescaleDB upstream behavior).  Set to
	 * an interval like '30 minutes' to have the scheduler SIGTERM
	 * workers that exceed it.  Stored as a string GUC; the SQL function
	 * casts via ::interval at INSERT time.
	 */
	DefineCustomStringVariable("time_series.cagg_default_max_runtime",
							   "Default max_runtime for new CAGG refresh policies",
							   "Default '30 min' bounds a single refresh run; "
							   "set to '0' to disable the timeout entirely. "
							   "Mirrors TimescaleDB's user-facing default.",
							   &ts_guc_cagg_default_max_runtime,
							   "30 min",	/* default — matches TSDB SQL */
							   PGC_USERSET,
							   0,
							   NULL, NULL, NULL);
}

void
ts_bgw_register_scheduler(void)
{
	BackgroundWorker worker;

	if (!ts_guc_bgw_enabled)
		return;

#ifdef GP_VERSION_NUM
	if (Gp_role != GP_ROLE_DISPATCH)
		return;		/* only register on coordinator */
#endif

	MemSet(&worker, 0, sizeof(BackgroundWorker));
	worker.bgw_flags = BGWORKER_SHMEM_ACCESS |
					   BGWORKER_BACKEND_DATABASE_CONNECTION;
	worker.bgw_start_time = BgWorkerStart_RecoveryFinished;
	/*
	 * Restart promptly after the scheduler exits or crashes.  The scheduler
	 * is the only consumer of bgw_job, so when the database is dropped
	 * and recreated (a common pattern in regress tests via pg_regress),
	 * we want it back online within a few seconds — not stuck waiting
	 * for the default 60 s back-off.
	 */
	worker.bgw_restart_time = 1;
	snprintf(worker.bgw_library_name, BGW_MAXLEN, "%s",
			 TS_EXTENSION_NAME);
	snprintf(worker.bgw_function_name, BGW_MAXLEN, "ts_bgw_scheduler_main");
	snprintf(worker.bgw_name, BGW_MAXLEN, "time_series scheduler");
	snprintf(worker.bgw_type, BGW_MAXLEN, "time_series scheduler");
	worker.bgw_notify_pid = 0;

	RegisterBackgroundWorker(&worker);
}
