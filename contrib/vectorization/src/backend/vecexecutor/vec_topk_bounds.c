/*-------------------------------------------------------------------------
 *
 * vec_topk_bounds.c
 *	  Module-private hash table keyed by Sort Plan pointer, carrying the
 *	  TopK bound from Limit's ExecInit down to Sort's Arrow-plan build.
 *
 *	  Design notes
 *	  ------------
 *	  The hash lives in the first-caller EState's es_query_cxt and is
 *	  referenced by a single module-level pointer.  A
 *	  MemoryContextResetCallback registered on that cxt nulls the
 *	  pointer when the cxt is freed, so the next query sees NULL and
 *	  lazily creates its own hash.
 *
 *	  Nesting (e.g. SPI inside a vectorized query) reuses the outer
 *	  query's hash: inner-query entries live in the outer's cxt until
 *	  the outer ends.  Keys are Plan pointers, which are distinct across
 *	  nested queries, so entries do not collide.  This small extra
 *	  memory usage is accepted as the price of a global-pointer design.
 *
 *	  Rescan safety: callers must skip registration when EXEC_FLAG_REWIND
 *	  is set (see ExecInitVecLimit); the Arrow plan is built only once
 *	  during init and cannot be rebuilt for a different K on rescan.
 *
 * Portions Copyright (c) 2023-2025, HashData Technology Limited.
 *
 * IDENTIFICATION
 *		contrib/vectorization/src/backend/vecexecutor/vec_topk_bounds.c
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "utils/hsearch.h"
#include "utils/memutils.h"

#include "vecexecutor/vec_topk_bounds.h"

typedef struct VecTopkEntry
{
	Plan	   *key;			/* HASH_BLOBS: raw bytes of the pointer */
	int64		bound;
} VecTopkEntry;

static HTAB *vec_topk_bounds = NULL;

static void
vec_topk_bounds_reset_cb(void *arg)
{
	/*
	 * The HTAB itself is allocated in the cxt that is about to be freed, so
	 * we only need to drop our module-level reference.  The next call to
	 * vec_topk_bounds_set() will lazily recreate the hash.
	 */
	vec_topk_bounds = NULL;
}

static HTAB *
ensure_htab(EState *estate)
{
	HASHCTL		ctl;
	MemoryContextCallback *cb;

	if (vec_topk_bounds != NULL)
		return vec_topk_bounds;

	MemSet(&ctl, 0, sizeof(ctl));
	ctl.keysize = sizeof(Plan *);
	ctl.entrysize = sizeof(VecTopkEntry);
	ctl.hcxt = estate->es_query_cxt;

	vec_topk_bounds = hash_create("vec_topk_bounds", 8, &ctl,
								  HASH_ELEM | HASH_BLOBS | HASH_CONTEXT);

	cb = (MemoryContextCallback *) MemoryContextAlloc(estate->es_query_cxt,
													  sizeof(*cb));
	cb->func = vec_topk_bounds_reset_cb;
	cb->arg = NULL;
	MemoryContextRegisterResetCallback(estate->es_query_cxt, cb);

	return vec_topk_bounds;
}

void
vec_topk_bounds_set(EState *estate, Plan *sort_plan, int64 k)
{
	VecTopkEntry *e;
	bool		found;

	e = (VecTopkEntry *) hash_search(ensure_htab(estate),
									 &sort_plan, HASH_ENTER, &found);
	e->bound = k;
}

int64
vec_topk_bounds_lookup(Plan *sort_plan)
{
	VecTopkEntry *e;

	if (vec_topk_bounds == NULL)
		return -1;

	e = (VecTopkEntry *) hash_search(vec_topk_bounds,
									 &sort_plan, HASH_FIND, NULL);
	return e ? e->bound : -1;
}
