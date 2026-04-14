/*-------------------------------------------------------------------------
 *
 * nodeSort.c
 *	  Routines to handle sorting of relations.
 *
 * Portions Copyright (c) 2007-2008, Cloudberry inc
 * Portions Copyright (c) 2012-Present VMware, Inc. or its affiliates.
 * Portions Copyright (c) 1996-2021, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/executor/nodeSort.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/parallel.h"
#include "executor/execdebug.h"
#include "executor/nodeSort.h"
#include "lib/stringinfo.h"             /* StringInfo */
#include "miscadmin.h"
#include "utils/tuplesort.h"
#include "cdb/cdbvars.h" /* CDB *//* gp_sort_flags */
#include "utils/workfile_mgr.h"
#include "executor/instrument.h"
#include "utils/faultinjector.h"

#include "vecexecutor/nodeSort.h"
#include "vecexecutor/execnodes.h"
#include "vecexecutor/executor.h"
#include "vecexecutor/execslot.h"
#include "utils/tuptable_vec.h"
#include "utils/vecsort.h"
#include "vecexecutor/execAmi.h"

static void ExecVecSortExplainEnd(PlanState *planstate, struct StringInfoData *buf);
static void ExecEagerFreeVecSort(SortState *node);

/* ----------------------------------------------------------------
 *		ExecSort
 *
 *		Sorts tuples from the outer subtree of the node using tuplesort,
 *		which saves the results in a temporary file or memory. After the
 *		initial call, returns a tuple from the file with each call.
 *
 *		Conditions:
 *		  -- none.
 *
 *		Initial States:
 *		  -- the outer child is prepared to return the first tuple.
 * ----------------------------------------------------------------
 */
static TupleTableSlot *
ExecVecSort(PlanState *pstate)
{
	SortState  *node = castNode(SortState, pstate);
    VecSortState *vnode;

	CHECK_FOR_INTERRUPTS();

    vnode = (VecSortState *) node;

	TupleTableSlot *slot = ExecuteVecPlan(&vnode->estate);

	if (TupIsNull(slot) && !node->delayEagerFree)
	{
		ExecEagerFreeVecSort(node);
	}
	return slot;
}

/* ----------------------------------------------------------------
 *		ExecInitVecSort
 *
 *		Creates the run-time state information for the sort node
 *		produced by the planner and initializes its outer subtree.
 * ----------------------------------------------------------------
 */
SortState *
ExecInitVecSort(Sort *node, EState *estate, int eflags)
{
	SortState       *sortstate;
    VecSortState    *vsortstate;

    SO1_printf("ExecInitVecSort: %s\n",
               "initializing sort node");

    /*
	 * create state structure
	 */
	vsortstate = (VecSortState*) palloc0(sizeof(VecSortState));
	sortstate = (SortState*) vsortstate;
	NodeSetTag(sortstate, T_SortState);
	sortstate->ss.ps.plan = (Plan *) node;
	sortstate->ss.ps.state = estate;
	sortstate->ss.ps.ExecProcNode = ExecVecSort;

	/*
	 * We must have random access to the sort output to do backward scan or
	 * mark/restore.  We also prefer to materialize the sort output if we
	 * might be called on to rewind and replay it many times.
	 */
	sortstate->randomAccess = (eflags & (EXEC_FLAG_REWIND |
										 EXEC_FLAG_BACKWARD |
										 EXEC_FLAG_MARK)) != 0;

	sortstate->bounded = false;
	sortstate->sort_Done = false;
	sortstate->tuplesortstate = NULL;

	/* CDB */

	/* BUT:
	 * The LIMIT optimizations requires exprcontext in which to
	 * evaluate the limit/offset parameters.
	 */
	ExecAssignExprContext(estate, &sortstate->ss.ps);

	/*
	 * Miscellaneous initialization
	 *
	 * Sort nodes don't initialize their ExprContexts because they never call
	 * ExecQual or ExecProject.
	 */

	/*
	 * CDB: Offer extra info for EXPLAIN ANALYZE.
	 */
	if (estate->es_instrument && (estate->es_instrument & INSTRUMENT_CDB))
	{
		/* Allocate string buffer. */
		sortstate->ss.ps.cdbexplainbuf = makeStringInfo();

		/* Request a callback at end of query. */
		sortstate->ss.ps.cdbexplainfun = ExecVecSortExplainEnd;
	}

	/*
	 * If eflag contains EXEC_FLAG_REWIND or EXEC_FLAG_BACKWARD or EXEC_FLAG_MARK,
	 * then this node is not eager free safe.
	 */
	sortstate->delayEagerFree =
		((eflags & (EXEC_FLAG_REWIND | EXEC_FLAG_BACKWARD | EXEC_FLAG_MARK)) != 0);

	/*
	 * initialize child nodes
	 *
	 * We shield the child node from the need to support BACKWARD, or
	 * MARK/RESTORE.
	 */

	eflags &= ~(EXEC_FLAG_BACKWARD | EXEC_FLAG_MARK);

	/*
	 * If Sort does not have any external parameters, then it
	 * can shield the child node from being rescanned as well, hence
	 * we can clear the EXEC_FLAG_REWIND as well. If there are parameters,
	 * don't clear the REWIND flag, as the child will be rewound.
	 */

	if (node->plan.allParam == NULL || node->plan.extParam == NULL)
	{
		eflags &= ~EXEC_FLAG_REWIND;
	}

	outerPlanState(sortstate) = VecExecInitNode(outerPlan(node), estate, eflags);

	/*
	 * If the child node of a Material is a Motion, then this Material node is
	 * not eager free safe.
	 */
	if (IsA(outerPlan((Plan *)node), Motion))
	{
		sortstate->delayEagerFree = true;
	}

	/*
	 * Initialize scan slot and type.
	 */
	ExecCreateScanSlotFromOuterPlan(estate, &sortstate->ss, &TTSOpsVecTuple);

	/*
	 * Initialize return slot and type. No need to initialize projection info
	 * because this node doesn't do projections.
	 */
	ExecInitResultTupleSlotTL(&sortstate->ss.ps, &TTSOpsVecTuple);
	sortstate->ss.ps.ps_ProjInfo = NULL;

	SO1_printf("ExecInitVecSort: %s\n",
			   "sort node initialized");
	PostBuildVecPlan((PlanState *)vsortstate, &vsortstate->estate);

	return sortstate;
}

/* ----------------------------------------------------------------
 *		ExecEndVecSort(node)
 * ----------------------------------------------------------------
 */
void
ExecEndVecSort(SortState *node)
{
	SO1_printf("ExecEndVecSort: %s\n",
			   "shutting down sort node");

    /* release tuple schema */
	FreeVecSchema(node->ss.ps.ps_ResultTupleSlot);

	ExecEagerFreeVecSort(node);
	
	VecSortState *vnode = (VecSortState *)node;

	/*
	 * Teardown order is contractual for TopK Runtime Filter.
	 *
	 * The Sort's underlying Arrow ExecPlan may contain a TopKNode that
	 * holds a non-owning pointer to a TopKThresholdState owned by the
	 * child SeqScan's PaxDatasetInterface (via ParallelScanDesc). The
	 * lifetime contract (documented on arrow::compute::TopKThresholdState
	 * in arrow/compute/exec/topk_threshold_state.h) requires that the
	 * ExecPlan be destroyed *before* the threshold state.
	 *
	 * FreeVecExecuteState() releases the GArrowExecutePlan, which:
	 *   - synchronously waits for all worker threads to stop, and
	 *   - destroys TopKNode (the only writer of the threshold state).
	 *
	 * Only after that is it safe to recurse into the child SeqScan's
	 * ExecEnd, which releases PaxDatasetInterface and destroys the
	 * TopKThresholdState along with it.
	 *
	 * DO NOT swap these two calls. Doing so would let the TopKNode
	 * (or, in error paths, still-running workers) reference a destroyed
	 * threshold state.
	 */
	FreeVecExecuteState(&vnode->estate);            /* (1) plan first  */

	/*
	 * shut down the subplan
	 */
	VecExecEndNode(outerPlanState(node));           /* (2) state after */

	SO1_printf("ExecEndVecSort: %s\n",
			   "sort node shutdown");
}

/* ----------------------------------------------------------------
 *		ExecVecSortMarkPos
 *
 *		Calls tuplesort to save the current position in the sorted file.
 * ----------------------------------------------------------------
 */
void
ExecVecSortMarkPos(SortState *node)
{
	/*
	 * if we haven't sorted yet, just return
	 */
	if (!node->sort_Done)
		return;

	tuplesort_markpos((Tuplesortstate *) node->tuplesortstate);
}

/* ----------------------------------------------------------------
 *		ExecVecSortRestrPos
 *
 *		Calls tuplesort to restore the last saved sort file position.
 * ----------------------------------------------------------------
 */
void
ExecVecSortRestrPos(SortState *node)
{
	/*
	 * if we haven't sorted yet, just return.
	 */
	if (!node->sort_Done)
		return;
    
	/*
	 * restore the scan to the previously marked position
	 */
	tuplesort_restorepos((Tuplesortstate *) node->tuplesortstate);
}

void
ExecReScanVecSort(SortState *node)
{
	PlanState  *outerPlan = outerPlanState(node);

	/*
	 * If we haven't sorted yet, just return. If outerplan's chgParam is not
	 * NULL then it will be re-scanned by ExecProcNode, else no reason to
	 * re-scan it at all.
	 */
	if (!node->sort_Done)
		return;

	/* must drop pointer to sort result tuple */
	ExecClearTuple(node->ss.ps.ps_ResultTupleSlot);

	/*
	 * If subnode is to be rescanned then we forget previous sort results; we
	 * have to re-read the subplan and re-sort.  Also must re-sort if the
	 * bounded-sort parameters changed or we didn't select randomAccess.
	 *
	 * Otherwise we can just rewind and rescan the sorted output.
	 */
	if (outerPlan->chgParam != NULL ||
		node->bounded != node->bounded_Done ||
		node->bound != node->bound_Done ||
		!node->randomAccess ||
		(node->tuplesortstate == NULL))
	{
		node->sort_Done = false;

		if (node->tuplesortstate != NULL)
		{
			vecsort_end((VecTuplesortstate *) node->tuplesortstate);
			node->tuplesortstate = NULL;
		}

		/*
		 * if chgParam of subnode is not null then plan will be re-scanned by
		 * first ExecProcNode.
		 */
		if (outerPlan->chgParam == NULL)
			ExecReScan(outerPlan); /* FIXME: we will move cbdb code(execAmi.c) in the future */
	}
	else
		vecsort_rescan((VecTuplesortstate *) node->tuplesortstate);
}


/*
 * ExecVecSortExplainEnd
 *      Called before ExecutorEnd to finish EXPLAIN ANALYZE reporting.
 */
void
ExecVecSortExplainEnd(PlanState *planstate, struct StringInfoData *buf)
{
	/* FIXME: implement statistic */
	return;
}                               /* ExecVecSortExplainEnd */

static void
ExecEagerFreeVecSort(SortState *node)
{
	/* clean out the tuple table */
	ExecClearTuple(node->ss.ss_ScanTupleSlot);

	/* must drop pointer to sort result tuple */
	ExecClearTuple(node->ss.ps.ps_ResultTupleSlot);

	if (node->tuplesortstate != NULL)
	{
		/*
		 * Save stats like in ExecVecSortExplainEnd, so that we can display
		 * them later in EXPLAIN ANALYZE.
		 */
		vecsort_get_stats(node->tuplesortstate,
						  &node->sortstats);
		if (node->ss.ps.instrument)
		{
			node->ss.ps.instrument->workfileCreated = (node->sortstats.spaceType == SORT_SPACE_TYPE_DISK);
			node->ss.ps.instrument->workmemused = node->sortstats.workmemused;
		}

		vecsort_end((VecTuplesortstate *) node->tuplesortstate);
		node->tuplesortstate = NULL;
	}
}

void
ExecSquelchVecSort(SortState *node)
{
	if (!node->delayEagerFree)
	{
		ExecEagerFreeVecSort(node);
		ExecVecSquelchNode(outerPlanState(node));
	}
}
