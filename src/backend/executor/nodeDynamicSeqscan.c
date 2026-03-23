/*-------------------------------------------------------------------------
 *
 * nodeDynamicSeqscan.c
 *	  Support routines for scanning one or more relations that are
 *	  determined at run time. The relations could be Heap, AppendOnly Row,
 *	  AppendOnly Columnar.
 *
 * DynamicSeqScan node scans each relation one after the other. For each
 * relation, it opens the table, scans the tuple, and returns relevant tuples.
 *
 * This has a smaller plan size than using an append with many partitions.
 * Instead of determining the column mapping for each partition during planning,
 * this mapping is determined during execution. When there are many partitions
 * with many columns, the plan size improvement becomes very large, on the order of
 * 100+ MB in some cases.
 *
 * Portions Copyright (c) 2012 - present, EMC/Greenplum
 * Portions Copyright (c) 2012-Present VMware, Inc. or its affiliates.
 *
 *
 * IDENTIFICATION
 *	    src/backend/executor/nodeDynamicSeqscan.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "executor/executor.h"
#include "executor/instrument.h"
#include "nodes/execnodes.h"
#include "executor/execPartition.h"
#include "executor/nodeDynamicSeqscan.h"
#include "executor/nodeSeqscan.h"
#include "storage/lwlock.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "access/table.h"
#include "access/tableam.h"

/*
 * Shared state for parallel-aware DynamicSeqScan.
 *
 * Workers atomically claim partitions via pdss_next_partition under the
 * pdss_lock.  The partition OID array is populated at DSM init time and
 * may be filtered in-place when dynamic pruning fires.
 */
typedef struct ParallelDynamicSeqScanDesc
{
	LWLock		pdss_lock;				/* mutual exclusion for partition claiming */
	int			pdss_npartitions;		/* partition count (updated after pruning) */
	int			pdss_next_partition;	/* next partition index to claim */
	bool		pdss_pruning_done;		/* dynamic pruning performed? */
	Oid			pdss_partoids[FLEXIBLE_ARRAY_MEMBER]; /* partition OID array */
} ParallelDynamicSeqScanDesc;

static void CleanupOnePartition(DynamicSeqScanState *node);

DynamicSeqScanState *
ExecInitDynamicSeqScan(DynamicSeqScan *node, EState *estate, int eflags)
{
	DynamicSeqScanState *state;
	Oid			reloid;
	ListCell *lc;
	int i;

	Assert((eflags & (EXEC_FLAG_BACKWARD | EXEC_FLAG_MARK)) == 0);

	state = makeNode(DynamicSeqScanState);
	state->eflags = eflags;
	state->ss.ps.plan = (Plan *) node;
	state->ss.ps.state = estate;
	state->ss.ps.ExecProcNode = ExecDynamicSeqScan;
	state->did_pruning = false;
	state->scan_state = SCAN_INIT;

	/* Initialize child expressions. This is needed to find subplans. */
	state->ss.ps.qual =
		ExecInitQual(node->seqscan.plan.qual, (PlanState *) state);

	Relation scanRel = ExecOpenScanRelation(estate, node->seqscan.scanrelid, eflags);
	ExecInitScanTupleSlot(estate, &state->ss, RelationGetDescr(scanRel), table_slot_callbacks(scanRel));

	/* Dynamic table/index/bitmap scan can't tell the ops of tupleslot */
	state->ss.ps.scanopsfixed = false;
	state->ss.ps.scanopsset = true;

	/* Initialize result tuple type. */
	ExecInitResultTypeTL(&state->ss.ps);
	ExecAssignScanProjectionInfo(&state->ss);

	state->ss.ps.resultopsfixed = false;
	state->ss.ps.resultopsset = true;

	state->nOids = list_length(node->partOids);
	state->partOids = palloc(sizeof(Oid) * state->nOids);
	foreach_with_count(lc, node->partOids, i)
		state->partOids[i] = lfirst_oid(lc);
	state->whichPart = -1;

	reloid = exec_rt_fetch(node->seqscan.scanrelid, estate)->relid;
	Assert(OidIsValid(reloid));

	/* lastRelOid is used to remap varattno for heterogeneous partitions */
	state->lastRelOid = reloid;

	state->scanrelid = node->seqscan.scanrelid;

	state->as_prune_state = NULL;
	state->pdss = NULL;

	/*
	 * This context will be reset per-partition to free up per-partition
	 * qual and targetlist allocations
	 */
	state->partitionMemoryContext = AllocSetContextCreate(CurrentMemoryContext,
									 "DynamicSeqScanPerPartition",
									 ALLOCSET_DEFAULT_MINSIZE,
									 ALLOCSET_DEFAULT_INITSIZE,
									 ALLOCSET_DEFAULT_MAXSIZE);
	return state;
}

/*
 * initNextTableToScan
 *   Find the next table to scan and initiate the scan if the previous table
 * is finished.
 *
 * If scanning on the current table is not finished, or a new table is found,
 * this function returns true.
 * If no more table is found, this function returns false.
 */
static bool
initNextTableToScan(DynamicSeqScanState *node)
{
	ScanState  *scanState = (ScanState *) node;
	DynamicSeqScan *plan = (DynamicSeqScan *) scanState->ps.plan;
	EState	   *estate = scanState->ps.state;
	Relation	lastScannedRel;
	TupleDesc	partTupDesc;
	TupleDesc	lastTupDesc;
	AttrMap *attMap;
	Oid		   *pid;
	Relation	currentRelation;

	if (node->pdss != NULL)
	{
		/* Parallel mode: atomically claim next partition under lock */
		int		idx;

		LWLockAcquire(&node->pdss->pdss_lock, LW_EXCLUSIVE);
		idx = node->pdss->pdss_next_partition++;
		LWLockRelease(&node->pdss->pdss_lock);

		if (idx >= node->nOids)
			return false;
		node->whichPart = idx;
	}
	else
	{
		/* Serial mode: simple increment */
		if (++node->whichPart >= node->nOids)
			return false;
	}

	pid = &node->partOids[node->whichPart];

	/* Collect number of partitions scanned in EXPLAIN ANALYZE */
	if (NULL != scanState->ps.instrument)
	{
		Instrumentation *instr = scanState->ps.instrument;
		instr->numPartScanned++;
	}

	currentRelation = scanState->ss_currentRelation =
		table_open(node->partOids[node->whichPart], AccessShareLock);

	if (currentRelation->rd_rel->relkind != RELKIND_RELATION)
	{
		/* shouldn't happen */
		elog(ERROR, "unexpected relkind in Dynamic Scan: %c", currentRelation->rd_rel->relkind);
	}
	lastScannedRel = table_open(node->lastRelOid, AccessShareLock);
	lastTupDesc = RelationGetDescr(lastScannedRel);
	partTupDesc = RelationGetDescr(scanState->ss_currentRelation);
	/*
	 * FIXME: should we use execute_attr_map_tuple instead? Seems like a
	 * higher level abstraction that fits the bill
	 */
	attMap = build_attrmap_by_name_if_req(partTupDesc, lastTupDesc);
	table_close(lastScannedRel, AccessShareLock);

	/* If attribute remapping is not necessary, then do not change the varattno */
	if (attMap)
	{
		change_varattnos_of_a_varno((Node*)scanState->ps.plan->qual, attMap->attnums, node->scanrelid);
		change_varattnos_of_a_varno((Node*)scanState->ps.plan->targetlist, attMap->attnums, node->scanrelid);

		/*
		 * Now that the varattno mapping has been changed, change the relation that
		 * the new varnos correspond to
		 */
		node->lastRelOid = *pid;
		free_attrmap(attMap);
	}

	node->seqScanState = ExecInitSeqScanForPartition(&plan->seqscan, estate,
													 currentRelation);
	return true;
}

/*
 * doParallelPruning
 *		Coordinate dynamic pruning across parallel workers.  The first
 *		worker to arrive performs the pruning and writes the filtered
 *		partition list into shared state; others wait and then sync.
 */
static void
doParallelPruning(DynamicSeqScanState *node)
{
	ParallelDynamicSeqScanDesc *pdss = node->pdss;

	/*
	 * Hold EXCLUSIVE lock so only the first worker performs pruning.
	 * All workers sync local state before releasing.
	 */
	LWLockAcquire(&pdss->pdss_lock, LW_EXCLUSIVE);
	if (!pdss->pdss_pruning_done)
	{
		DynamicSeqScan *plan = (DynamicSeqScan *) node->ss.ps.plan;
		int			i = 0;
		int			partOidIdx = -1;
		int			newCount = 0;

		node->as_valid_subplans =
			ExecFindMatchingSubPlans(node->as_prune_state,
									 node->ss.ps.state,
									 list_length(plan->partOids),
									 plan->join_prune_paramids);

		for (i = 0; i < bms_num_members(node->as_valid_subplans); i++)
		{
			partOidIdx = bms_next_member(node->as_valid_subplans, partOidIdx);
			pdss->pdss_partoids[newCount++] = pdss->pdss_partoids[partOidIdx];
		}
		pdss->pdss_npartitions = newCount;
		pdss->pdss_pruning_done = true;
	}

	/* Sync local state from the shared descriptor while holding the lock */
	node->nOids = pdss->pdss_npartitions;
	memcpy(node->partOids, pdss->pdss_partoids,
		   sizeof(Oid) * node->nOids);
	LWLockRelease(&pdss->pdss_lock);
}

TupleTableSlot *
ExecDynamicSeqScan(PlanState *pstate)
{
	DynamicSeqScanState *node = castNode(DynamicSeqScanState, pstate);
	TupleTableSlot *slot = NULL;

	DynamicSeqScan	   *plan = (DynamicSeqScan *) node->ss.ps.plan;
	node->as_valid_subplans = NULL;
	if (NULL != plan->join_prune_paramids && !node->did_pruning)
	{
		if (node->pdss != NULL)
		{
			doParallelPruning(node);
		}
		else
		{
			int			i = 0;
			int			partOidIdx = -1;
			List	   *newPartOids = NIL;
			ListCell   *lc;

			node->as_valid_subplans =
				ExecFindMatchingSubPlans(node->as_prune_state,
										 node->ss.ps.state,
										 list_length(plan->partOids),
										 plan->join_prune_paramids);

			for (i = 0; i < bms_num_members(node->as_valid_subplans); i++)
			{
				partOidIdx = bms_next_member(node->as_valid_subplans, partOidIdx);
				newPartOids = lappend_oid(newPartOids, node->partOids[partOidIdx]);
			}

			node->partOids = palloc(sizeof(Oid) * list_length(newPartOids));
			foreach_with_count(lc, newPartOids, i)
				node->partOids[i] = lfirst_oid(lc);
			node->nOids = list_length(newPartOids);
		}

		node->did_pruning = true;
	}

	/*
	 * Scan the table to find next tuple to return. If the current table
	 * is finished, close it and open the next table for scan.
	 */
	for (;;)
	{
		if (!node->seqScanState)
		{
			/* No partition open. Open the next one, if any. */
			if (!initNextTableToScan(node))
				break;
		}

		slot = ExecProcNode(&node->seqScanState->ss.ps);

		if (!TupIsNull(slot))
		{
			if (gp_enable_runtime_filter_pushdown
				&& !pstate->state->useMppParallelMode
				&& node->filters)
			{
				if (!PassByBloomFilter(&node->ss.ps, node->filters, slot))
					continue;
			}
			break;
		}

		/* No more tuples from this partition. Move to next one. */
		CleanupOnePartition(node);
	}

	return slot;
}

/*
 * CleanupOnePartition
 *		Cleans up a partition's relation and releases all locks.
 */
static void
CleanupOnePartition(DynamicSeqScanState *scanState)
{
	Assert(NULL != scanState);

	if (scanState->seqScanState)
	{
		ExecEndSeqScan(scanState->seqScanState);
		scanState->seqScanState = NULL;
		Assert(scanState->ss.ss_currentRelation != NULL);
		table_close(scanState->ss.ss_currentRelation, NoLock);
		scanState->ss.ss_currentRelation = NULL;
	}
}

/*
 * DynamicSeqScanEndCurrentScan
 *		Cleans up any ongoing scan.
 */
static void
DynamicSeqScanEndCurrentScan(DynamicSeqScanState *node)
{
	CleanupOnePartition(node);
}

/*
 * ExecEndDynamicSeqScan
 *		Ends the scanning of this DynamicSeqScanNode and frees
 *		up all the resources.
 */
void
ExecEndDynamicSeqScan(DynamicSeqScanState *node)
{
	DynamicSeqScanEndCurrentScan(node);

	if (node->ss.ps.ps_ResultTupleSlot)
		ExecClearTuple(node->ss.ps.ps_ResultTupleSlot);
}

/*
 * ExecReScanDynamicSeqScan
 *		Prepares the internal states for a rescan.
 */
void
ExecReScanDynamicSeqScan(DynamicSeqScanState *node)
{
	DynamicSeqScanEndCurrentScan(node);

	/*
	 * If any PARAM_EXEC Params used in pruning expressions have changed, then
	 * we'd better unset the valid subplans so that they are reselected for
	 * the new parameter values.
	 */
	if (node->as_prune_state &&
		bms_overlap(node->ss.ps.chgParam,
					node->as_prune_state->execparamids))
	{
		bms_free(node->as_valid_subplans);
		node->as_valid_subplans = NULL;
	}

	/*
	 * Parallel mode: reset shared state so workers start from partition 0.
	 * If pruning params changed, we must also restore the original partition
	 * list (both local and shared) because pruning compacts the arrays
	 * in-place — re-pruning needs the full original indices.
	 */
	if (node->pdss != NULL)
	{
		LWLockAcquire(&node->pdss->pdss_lock, LW_EXCLUSIVE);
		node->pdss->pdss_next_partition = 0;

		if (node->as_valid_subplans == NULL && node->did_pruning)
		{
			DynamicSeqScan *plan = (DynamicSeqScan *) node->ss.ps.plan;
			ListCell   *lc;
			int			i;
			int			origCount = list_length(plan->partOids);

			node->pdss->pdss_npartitions = origCount;
			node->pdss->pdss_pruning_done = false;
			foreach_with_count(lc, plan->partOids, i)
				node->pdss->pdss_partoids[i] = lfirst_oid(lc);

			/* Restore leader's local state */
			node->nOids = origCount;
			pfree(node->partOids);
			node->partOids = palloc(sizeof(Oid) * origCount);
			memcpy(node->partOids, node->pdss->pdss_partoids,
				   sizeof(Oid) * origCount);
			node->did_pruning = false;
		}

		LWLockRelease(&node->pdss->pdss_lock);
	}
	node->whichPart = -1;
}

/* ----------------------------------------------------------------
 *					Parallel Dynamic Seq Scan Support
 * ----------------------------------------------------------------
 */

/* ----------------------------------------------------------------
 *		ExecDynamicSeqScanEstimate
 *
 *		Compute the amount of space we'll need in the parallel
 *		query DSM, and inform pcxt->estimator about our needs.
 * ----------------------------------------------------------------
 */
void
ExecDynamicSeqScanEstimate(DynamicSeqScanState *node,
						   ParallelContext *pcxt)
{
	node->pdss_len =
		add_size(offsetof(ParallelDynamicSeqScanDesc, pdss_partoids),
				 sizeof(Oid) * node->nOids);

	shm_toc_estimate_chunk(&pcxt->estimator, node->pdss_len);
	shm_toc_estimate_keys(&pcxt->estimator, 1);
}

/* ----------------------------------------------------------------
 *		ExecDynamicSeqScanInitializeDSM
 *
 *		Set up shared state for Parallel Dynamic Seq Scan.
 * ----------------------------------------------------------------
 */
void
ExecDynamicSeqScanInitializeDSM(DynamicSeqScanState *node,
								ParallelContext *pcxt)
{
	ParallelDynamicSeqScanDesc *pdss;

	pdss = shm_toc_allocate(pcxt->toc, node->pdss_len);
	memset(pdss, 0, node->pdss_len);
	LWLockInitialize(&pdss->pdss_lock, LWTRANCHE_PARALLEL_DYNAMIC_SEQSCAN);
	pdss->pdss_npartitions = node->nOids;
	pdss->pdss_next_partition = 0;
	pdss->pdss_pruning_done = false;
	memcpy(pdss->pdss_partoids, node->partOids, sizeof(Oid) * node->nOids);
	shm_toc_insert(pcxt->toc, node->ss.ps.plan->plan_node_id, pdss);

	node->pdss = pdss;
}

/* ----------------------------------------------------------------
 *		ExecDynamicSeqScanReInitializeDSM
 *
 *		Reset shared state before beginning a fresh scan.
 * ----------------------------------------------------------------
 */
void
ExecDynamicSeqScanReInitializeDSM(DynamicSeqScanState *node,
								  ParallelContext *pcxt)
{
	DynamicSeqScan *plan = (DynamicSeqScan *) node->ss.ps.plan;
	ParallelDynamicSeqScanDesc *pdss = node->pdss;
	ListCell   *lc;
	int			origCount = list_length(plan->partOids);
	int			i;

	/* Allocate local buffer before taking lock to minimize lock hold time */
	node->nOids = origCount;
	pfree(node->partOids);
	node->partOids = palloc(sizeof(Oid) * origCount);

	LWLockAcquire(&pdss->pdss_lock, LW_EXCLUSIVE);
	pdss->pdss_next_partition = 0;

	/*
	 * Restore the full original partition list so that any subsequent
	 * dynamic pruning pass can use the original indices correctly.
	 */
	pdss->pdss_npartitions = origCount;
	pdss->pdss_pruning_done = false;
	foreach_with_count(lc, plan->partOids, i)
		pdss->pdss_partoids[i] = lfirst_oid(lc);

	/* Sync leader's local state while still holding the lock */
	memcpy(node->partOids, pdss->pdss_partoids, sizeof(Oid) * origCount);
	LWLockRelease(&pdss->pdss_lock);
	node->did_pruning = false;
}

/* ----------------------------------------------------------------
 *		ExecDynamicSeqScanInitializeWorker
 *
 *		Copy relevant information from TOC into planstate.
 * ----------------------------------------------------------------
 */
void
ExecDynamicSeqScanInitializeWorker(DynamicSeqScanState *node,
								   ParallelWorkerContext *pwcxt)
{
	ParallelDynamicSeqScanDesc *pdss;

	pdss = shm_toc_lookup(pwcxt->toc, node->ss.ps.plan->plan_node_id, false);
	node->pdss = pdss;

	/* Sync local partition list from shared state */
	LWLockAcquire(&pdss->pdss_lock, LW_SHARED);
	node->nOids = pdss->pdss_npartitions;
	node->partOids = palloc(sizeof(Oid) * node->nOids);
	memcpy(node->partOids, pdss->pdss_partoids, sizeof(Oid) * node->nOids);
	LWLockRelease(&pdss->pdss_lock);
}
