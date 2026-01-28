/*-------------------------------------------------------------------------
 *
 * nodePartitionTopK.h
 *	  Function declarations for nodePartitionTopK.c
 *
 *-------------------------------------------------------------------------
 */
#ifndef NODEPARTITIONTOPK_H
#define NODEPARTITIONTOPK_H

#include "nodes/execnodes.h"

extern PartitionTopKState *ExecInitPartitionTopK(PartitionTopK *node, EState *estate, int eflags);
extern void ExecEndPartitionTopK(PartitionTopKState *node);
extern void ExecReScanPartitionTopK(PartitionTopKState *node);

#endif /* NODEPARTITIONTOPK_H */