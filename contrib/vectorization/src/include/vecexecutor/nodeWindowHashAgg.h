/*-------------------------------------------------------------------------
 *
 * nodeWindowHashAgg.h
 *
 * Portions Copyright (c) 2016-Present, HashData
 *
 * src/include/vecexecutor/nodeWindowHashAgg.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef VEC_NODEWINDOWHASHAGG_H
#define VEC_NODEWINDOWHASHAGG_H

#include "nodes/execnodes.h"

#include "vecnodes/plannodes.h"
#include "vecexecutor/execnodes.h"

extern WindowHashAggState *ExecInitVecWindowHashAgg(WindowHashAgg *node, EState *estate, int eflags);
extern void ExecEndVecWindowHashAgg(WindowHashAggState *node);
extern void ExecSquelchVecWindowHashAgg(WindowHashAggState *node);
extern void ExecReScanVecWindowHashAgg(WindowHashAggState *node);

#endif   /* VEC_NODEWINDOWHASHAGG_H*/

