/*-------------------------------------------------------------------------
 *
 * vec_topk_bounds.h
 *	  Runtime-only channel for passing the TopK bound (K) from a Limit
 *	  node down to its immediate child Sort node during vectorized
 *	  execution init.
 *
 *	  The bound is produced by ExecInitVecLimit (after recompute_limits)
 *	  and consumed later by BuildProject while constructing the Arrow
 *	  plan for the Sort.  Because the Limit's ExecInit calls the child
 *	  Sort's ExecInit before returning, the hand-off is strictly
 *	  depth-first: write happens first, read happens during the nested
 *	  init call.
 *
 *	  This replaces an earlier design that extended the Sort Plan node
 *	  with extra fields (VecSortPlan).  That approach broke under MPP
 *	  dispatch because the extra fields did not survive plan
 *	  serialization to QEs.  See vec_topk_bounds.c for lifecycle details.
 *
 * Portions Copyright (c) 2023-2025, HashData Technology Limited.
 *
 * IDENTIFICATION
 *		contrib/vectorization/src/include/vecexecutor/vec_topk_bounds.h
 *-------------------------------------------------------------------------
 */
#ifndef VEC_TOPK_BOUNDS_H
#define VEC_TOPK_BOUNDS_H

#include "postgres.h"
#include "nodes/execnodes.h"
#include "nodes/plannodes.h"

/*
 * Register a TopK bound (K) for a Sort plan node.  The Sort's Arrow-plan
 * build path will pick this up and emit a TopKNode instead of an
 * OrderByNode.  Entries are keyed by the Sort Plan pointer.
 */
extern void vec_topk_bounds_set(EState *estate, Plan *sort_plan, int64 k);

/*
 * Look up a previously registered TopK bound.  Returns the K value, or -1
 * if no entry exists for this Sort (caller should fall back to OrderBy).
 */
extern int64 vec_topk_bounds_lookup(Plan *sort_plan);

#endif							/* VEC_TOPK_BOUNDS_H */
