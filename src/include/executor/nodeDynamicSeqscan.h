/*-------------------------------------------------------------------------
 *
 * nodeDynamicSeqscan.h
 *
 * Portions Copyright (c) 2012 - present, EMC/Greenplum
 * Portions Copyright (c) 2012-Present VMware, Inc. or its affiliates.
 *
 *
 * IDENTIFICATION
 *	    src/include/executor/nodeDynamicSeqscan.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef NODEDYNAMICSEQSCAN_H
#define NODEDYNAMICSEQSCAN_H

#include "access/parallel.h"
#include "nodes/execnodes.h"

extern DynamicSeqScanState *ExecInitDynamicSeqScan(DynamicSeqScan *node, EState *estate, int eflags);
extern TupleTableSlot *ExecDynamicSeqScan(PlanState *pstate);
extern void ExecEndDynamicSeqScan(DynamicSeqScanState *node);
extern void ExecReScanDynamicSeqScan(DynamicSeqScanState *node);

/* Parallel support */
extern void ExecDynamicSeqScanEstimate(DynamicSeqScanState *node,
									   ParallelContext *pcxt);
extern void ExecDynamicSeqScanInitializeDSM(DynamicSeqScanState *node,
											ParallelContext *pcxt);
extern void ExecDynamicSeqScanReInitializeDSM(DynamicSeqScanState *node,
											  ParallelContext *pcxt);
extern void ExecDynamicSeqScanInitializeWorker(DynamicSeqScanState *node,
											   ParallelWorkerContext *pwcxt);

#endif
