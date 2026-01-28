/*-------------------------------------------------------------------------
 *
 * nodePartitionTopK.c
 *	  Executor support for PARTITION TOP K WITH TIES (RANK <= K).
 *
 *	  Key design choices:
 *	    - Type-aware hash/eq via lookup_type_cache (TOAST + collation safe)
 *	    - Tie buffer backed by tuplestore (auto spill-to-disk via work_mem)
 *	    - Final output as flat array (not List)
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "common/hashfn.h"
#include "executor/executor.h"
#include "executor/nodePartitionTopK.h"
#include "fmgr.h"
#include "lib/binaryheap.h"
#include "miscadmin.h"
#include "nodes/execnodes.h"
#include "utils/datum.h"
#include "utils/hsearch.h"
#include "utils/lsyscache.h"
#include "utils/tuplestore.h"
#include "utils/typcache.h"

/* Initial number of hash table buckets for partition tracking */
#define PARTITION_HASH_INIT_SIZE	128

/*
 * PartitionHashKey -- hash key for partition grouping.
 *
 * The 'node' pointer is embedded so that hash/match callbacks can access
 * per-node state (hash functions, collations, type info) without relying
 * on process-global variables.
 */
typedef struct PartitionHashKey
{
	PartitionTopKState *node;	/* back-pointer for hash/match callbacks */
	int			nkeys;
	Datum	   *values;
	bool	   *isnull;
} PartitionHashKey;

/*
 * PartitionRuntimeState -- per-partition runtime state.
 *
 * Each partition maintains a bounded binary heap for top (K-1) tuples and
 * a tuplestore for the K-th rank tie group.  The 'node' pointer is needed
 * by heap_compare_fn (via the binaryheap callback argument).
 */
typedef struct PartitionRuntimeState
{
	binaryheap *topk_heap;		/* min-heap for top (K-1) tuples; NULL if K==1 */
	int			heap_capacity;	/* = K - 1 */

	Tuplestorestate *tie_store;	/* tie buffer (spills to disk via work_mem) */
	int			tie_count;

	TupleTableSlot *tie_key_slot;	/* reference slot for tie comparison */

	PartitionTopKState *node;	/* back-pointer for heap compare callback */
} PartitionRuntimeState;

/*
 * PartitionHashEntry -- hash table entry for partition lookup.
 */
typedef struct PartitionHashEntry
{
	PartitionHashKey key;
	PartitionRuntimeState *state;
} PartitionHashEntry;


/* ----------------------------------------------------------------
 *		Hash/match callbacks for partition hash table
 *
 *		These use type-specific hash/equality functions looked up via
 *		lookup_type_cache, which correctly handle TOAST detoasting and
 *		collation-aware comparison.
 * ----------------------------------------------------------------
 */

/*
 * partition_hash_hash -- compute hash value for a partition key.
 */
static uint32
partition_hash_hash(const void *key, Size keysize)
{
	const PartitionHashKey *hkey = (const PartitionHashKey *) key;
	PartitionTopKState *node = hkey->node;
	uint32		hashkey = 0;
	int			i;

	Assert(node != NULL);
	Assert(hkey->nkeys == node->numPartitionCols);

	for (i = 0; i < hkey->nkeys; i++)
	{
		/* rotate hashkey left 1 bit at each step */
		hashkey = (hashkey << 1) | ((hashkey & 0x80000000) ? 1 : 0);

		if (!hkey->isnull[i])
		{
			uint32	hval;
			Oid		typid = node->partitionColTypes[i];

			if (typid == INT4OID)
				hval = murmurhash32((uint32) DatumGetInt32(hkey->values[i]));
			else if (typid == INT8OID)
			{
				int64	val64 = DatumGetInt64(hkey->values[i]);
				uint32	lohalf = (uint32) val64;
				uint32	hihalf = (uint32) ((uint64) val64 >> 32);

				lohalf ^= (val64 >= 0) ? hihalf : ~hihalf;
				hval = murmurhash32(lohalf);
			}
			else
				hval = DatumGetUInt32(FunctionCall1Coll(
					&node->partitionHashFuncs[i],
					node->partitionCollations[i],
					hkey->values[i]));
			hashkey ^= hval;
		}
	}

	return murmurhash32(hashkey);
}

/*
 * partition_hash_match -- equality check for partition keys.
 *
 * Returns 0 if equal, 1 if not equal (dynahash convention).
 */
static int
partition_hash_match(const void *key1, const void *key2, Size keysize)
{
	const PartitionHashKey *k1 = (const PartitionHashKey *) key1;
	const PartitionHashKey *k2 = (const PartitionHashKey *) key2;
	PartitionTopKState *node = k1->node;
	int			i;

	Assert(node != NULL);
	Assert(k1->nkeys == k2->nkeys && k1->nkeys == node->numPartitionCols);

	for (i = 0; i < k1->nkeys; i++)
	{
		if (k1->isnull[i] != k2->isnull[i])
			return 1;	/* not equal */

		if (!k1->isnull[i])
		{
			Oid		typid = node->partitionColTypes[i];
			bool	equal;

			if (typid == INT4OID)
				equal = (DatumGetInt32(k1->values[i]) == DatumGetInt32(k2->values[i]));
			else if (typid == INT8OID)
				equal = (DatumGetInt64(k1->values[i]) == DatumGetInt64(k2->values[i]));
			else
				equal = DatumGetBool(FunctionCall2Coll(
						&node->partitionEqFuncs[i],
						node->partitionCollations[i],
						k1->values[i], k2->values[i]));
			if (!equal)
				return 1;	/* not equal */
		}
	}
	return 0;	/* equal */
}


/* ----------------------------------------------------------------
 *		Internal helper functions
 * ----------------------------------------------------------------
 */

/*
 * heap_compare_fn -- binaryheap comparison callback.
 *
 * Implements a max-heap on sort keys so that the worst (largest rank) tuple
 * is at the root and can be quickly evicted.
 */
static int
heap_compare_fn(Datum a, Datum b, void *arg)
{
	TupleTableSlot *slot_a = (TupleTableSlot *) DatumGetPointer(a);
	TupleTableSlot *slot_b = (TupleTableSlot *) DatumGetPointer(b);
	PartitionRuntimeState *rts = (PartitionRuntimeState *) arg;
	PartitionTopKState *node = rts->node;
	int			i;

	for (i = 0; i < node->numSortCols; i++)
	{
		bool		isnull_a,
					isnull_b;
		AttrNumber	attnum = node->sortColIdx[i];
		Datum		val_a = slot_getattr(slot_a, attnum, &isnull_a);
		Datum		val_b = slot_getattr(slot_b, attnum, &isnull_b);
		int			cmp;

		cmp = ApplySortComparator(val_a, isnull_a, val_b, isnull_b,
								  &node->sortKeys[i]);
		if (cmp != 0)
			return cmp;
	}

	return 0;
}

/*
 * create_partition_state -- allocate per-partition runtime state.
 *
 * Allocated in partitionContext so it gets cleaned up on rescan.
 */
static PartitionRuntimeState *
create_partition_state(PartitionTopKState *node)
{
	MemoryContext oldctx;
	PartitionRuntimeState *rts;
	int			k = node->top_k;

	oldctx = MemoryContextSwitchTo(node->partitionContext);

	rts = palloc0(sizeof(PartitionRuntimeState));
	rts->node = node;

	if (k > 1)
	{
		rts->heap_capacity = k - 1;
		rts->topk_heap = binaryheap_allocate(rts->heap_capacity,
											 heap_compare_fn,
											 rts);
	}

	/* Lazy: tuplestore created on first use in handle_evicted_or_new_candidate */
	rts->tie_store = NULL;
	rts->tie_count = 0;
	rts->tie_key_slot = NULL;

	MemoryContextSwitchTo(oldctx);
	return rts;
}

/*
 * compare_all_sort_keys -- compare two slots on all sort columns.
 *
 * Returns negative if slot_a < slot_b, 0 if equal, positive if slot_a > slot_b.
 */
static int
compare_all_sort_keys(PartitionTopKState *node,
					  TupleTableSlot *slot_a, TupleTableSlot *slot_b)
{
	int		i;

	for (i = 0; i < node->numSortCols; i++)
	{
		bool		isnull_a,
					isnull_b;
		Datum		val_a = slot_getattr(slot_a, node->sortColIdx[i], &isnull_a);
		Datum		val_b = slot_getattr(slot_b, node->sortColIdx[i], &isnull_b);
		int			cmp;

		cmp = ApplySortComparator(val_a, isnull_a, val_b, isnull_b,
								  &node->sortKeys[i]);
		if (cmp != 0)
			return cmp;
	}
	return 0;
}

/*
 * handle_evicted_or_new_candidate -- process a candidate for the tie group.
 *
 * Called when a tuple is evicted from the top-(K-1) heap, or when K==1.
 * Maintains the tie group tuplestore: replaces, appends, or discards the
 * candidate based on comparison with the current tie reference.
 */
static void
handle_evicted_or_new_candidate(PartitionRuntimeState *rts,
								TupleTableSlot *candidate)
{
	PartitionTopKState *node = rts->node;
	int			cmp;

	if (rts->tie_key_slot == NULL)
	{
		/* First entry in tie group -- keep candidate as comparison reference */
		rts->tie_key_slot = candidate;
		if (rts->tie_store == NULL)
			rts->tie_store = tuplestore_begin_heap(true, false, work_mem);
		tuplestore_puttupleslot(rts->tie_store, candidate);
		rts->tie_count = 1;
		return;
	}

	/* Compare candidate against tie reference on all sort columns */
	cmp = compare_all_sort_keys(node, candidate, rts->tie_key_slot);

	if (cmp < 0)
	{
		/* New row is better -- replace entire tie group */
		if (rts->tie_store)
			tuplestore_end(rts->tie_store);
		rts->tie_store = tuplestore_begin_heap(true, false, work_mem);
		ExecDropSingleTupleTableSlot(rts->tie_key_slot);

		rts->tie_key_slot = candidate;
		tuplestore_puttupleslot(rts->tie_store, candidate);
		rts->tie_count = 1;
	}
	else if (cmp == 0)
	{
		/* Tie -- append to tuplestore */
		if (rts->tie_store == NULL)
			rts->tie_store = tuplestore_begin_heap(true, false, work_mem);
		tuplestore_puttupleslot(rts->tie_store, candidate);
		ExecDropSingleTupleTableSlot(candidate);
		rts->tie_count++;
	}
	else
	{
		/* Candidate is worse than current tie group -- drop it */
		ExecDropSingleTupleTableSlot(candidate);
	}
}

/*
 * copy_input_slot -- copy a child slot into partitionContext.
 *
 * Allocates a new MinimalTuple-backed slot, copies the tuple data, and
 * materializes it so the slot is self-contained.
 */
static TupleTableSlot *
copy_input_slot(PartitionTopKState *node, TupleTableSlot *slot)
{
	TupleTableSlot *copied_slot;
	MemoryContext	oldctx;

	oldctx = MemoryContextSwitchTo(node->partitionContext);
	copied_slot = MakeSingleTupleTableSlot(slot->tts_tupleDescriptor,
										   &TTSOpsMinimalTuple);
	ExecCopySlot(copied_slot, slot);
	ExecMaterializeSlot(copied_slot);
	MemoryContextSwitchTo(oldctx);

	return copied_slot;
}

/*
 * insert_tuple_into_partition_state -- insert a tuple into the partition's
 * top-K tracking structure.
 *
 * Compares using the original child slot first (slot_getattr is valid on it),
 * and only copies when the tuple will actually be kept.  This eliminates
 * the vast majority of palloc/copy/free overhead for large inputs.
 *
 * For K==1, delegates directly to the tie handler.  For K>1, maintains a
 * bounded binary heap of size (K-1) and routes evicted tuples to the tie
 * handler.
 */
static void
insert_tuple_into_partition_state(PartitionTopKState *node,
								  PartitionRuntimeState *rts,
								  TupleTableSlot *slot)
{
	int		k = node->top_k;

	if (k == 1)
	{
		/* K==1: compare first, copy only if needed */
		if (rts->tie_key_slot != NULL)
		{
			int		cmp = compare_all_sort_keys(node, slot, rts->tie_key_slot);

			if (cmp > 0)
				return;		/* worse than tie group -- skip */
		}
		handle_evicted_or_new_candidate(rts, copy_input_slot(node, slot));
		return;
	}

	Assert(rts->topk_heap != NULL);

	if (rts->topk_heap->bh_size < rts->heap_capacity)
	{
		/* Heap not full -- must copy to fill it */
		TupleTableSlot *copied_slot = copy_input_slot(node, slot);

		binaryheap_add_unordered(rts->topk_heap, (Datum) copied_slot);
		if (rts->topk_heap->bh_size == rts->heap_capacity)
			binaryheap_build(rts->topk_heap);
		return;
	}

	/* Heap is full -- compare with original slot before copying */
	{
		TupleTableSlot *heap_top;
		int			cmp_with_heap;

		heap_top = (TupleTableSlot *) binaryheap_first(rts->topk_heap);
		cmp_with_heap = heap_compare_fn((Datum) slot, (Datum) heap_top, rts);

		if (cmp_with_heap < 0)
		{
			/* New row is better than heap top -- copy and replace */
			TupleTableSlot *copied_slot = copy_input_slot(node, slot);

			binaryheap_replace_first(rts->topk_heap, (Datum) copied_slot);
			handle_evicted_or_new_candidate(rts, heap_top);
		}
		else
		{
			/* Not better than heap top -- check against tie group */
			if (rts->tie_key_slot != NULL)
			{
				int		cmp_with_tie = compare_all_sort_keys(node, slot,
															 rts->tie_key_slot);

				if (cmp_with_tie > 0)
					return;		/* worse than tie group -- skip without copy */
			}
			handle_evicted_or_new_candidate(rts, copy_input_slot(node, slot));
		}
	}
}

/*
 * finalize_all_partitions -- collect all partition results into a flat array.
 *
 * Two-pass approach: first pass counts total rows across all partitions,
 * second pass copies tuples into the pre-allocated output array.  This avoids
 * incremental List appends and gives O(1) random access during output.
 */
static void
finalize_all_partitions(PartitionTopKState *node)
{
	HASH_SEQ_STATUS status;
	PartitionHashEntry *entry;
	MemoryContext oldctx;
	int			total_rows = 0;
	int			idx = 0;

	/* First pass: count total rows */
	hash_seq_init(&status, node->partition_hash);
	while ((entry = (PartitionHashEntry *) hash_seq_search(&status)) != NULL)
	{
		PartitionRuntimeState *rts = entry->state;
		int		heap_rows = rts->topk_heap ? rts->topk_heap->bh_size : 0;

		total_rows += heap_rows + rts->tie_count;
	}

	if (total_rows == 0)
	{
		node->final_output_array = NULL;
		node->final_output_count = 0;
		return;
	}

	/* Allocate final_output_array in es_query_cxt (long-lived) */
	oldctx = MemoryContextSwitchTo(node->ps.state->es_query_cxt);
	node->final_output_array = (TupleTableSlot **)
		palloc0(total_rows * sizeof(TupleTableSlot *));
	node->final_output_count = total_rows;
	MemoryContextSwitchTo(oldctx);

	/* Second pass: fill array */
	hash_seq_init(&status, node->partition_hash);
	while ((entry = (PartitionHashEntry *) hash_seq_search(&status)) != NULL)
	{
		PartitionRuntimeState *rts = entry->state;

		/* Extract heap (top K-1) in reverse order */
		if (rts->topk_heap && rts->topk_heap->bh_size > 0)
		{
			TupleTableSlot **temp;
			int		j = 0;
			int		i;

			/* Ensure the heap property before extracting */
			if (!rts->topk_heap->bh_has_heap_property)
				binaryheap_build(rts->topk_heap);

			temp = (TupleTableSlot **)
				palloc(rts->topk_heap->bh_size * sizeof(TupleTableSlot *));
			while (!binaryheap_empty(rts->topk_heap))
			{
				temp[j++] = (TupleTableSlot *)
					binaryheap_remove_first(rts->topk_heap);
			}

			/* Move heap slot pointers directly to final array (zero-copy).
			 * Safe because ExecEnd/ExecReScan drops these slots from
			 * final_output_array BEFORE resetting partitionContext, and the
			 * heap is empty after extraction (bh_size==0) so
			 * cleanup_partition_resources won't attempt to free them again. */
			for (i = j - 1; i >= 0; i--)
			{
				node->final_output_array[idx] = temp[i];
				idx++;
			}
			pfree(temp);
		}

		/* Append tie group from tuplestore */
		if (rts->tie_count > 0)
		{
			TupleTableSlot *read_slot;

			read_slot = MakeSingleTupleTableSlot(node->tupdesc,
												 &TTSOpsMinimalTuple);
			tuplestore_rescan(rts->tie_store);
			oldctx = MemoryContextSwitchTo(node->ps.state->es_query_cxt);
			while (tuplestore_gettupleslot(rts->tie_store, true, false,
										   read_slot))
			{
				node->final_output_array[idx] =
					MakeSingleTupleTableSlot(node->tupdesc,
											 &TTSOpsMinimalTuple);
				ExecCopySlot(node->final_output_array[idx], read_slot);
				idx++;
			}
			MemoryContextSwitchTo(oldctx);
			ExecDropSingleTupleTableSlot(read_slot);
		}
	}

	Assert(idx == total_rows);
}

/*
 * fill_partition_key -- extract partition key values from a slot into
 * the lookup key structure using the pre-allocated scratch buffers.
 */
static void
fill_partition_key(PartitionTopKState *node, TupleTableSlot *slot,
				   PartitionHashKey *key)
{
	int		i;

	key->node = node;
	key->nkeys = node->numPartitionCols;
	key->values = node->temp_key_values;
	key->isnull = node->temp_key_isnull;

	for (i = 0; i < key->nkeys; i++)
	{
		key->values[i] = slot_getattr(slot, node->partitionColIdx[i],
									  &key->isnull[i]);
	}
}

/*
 * cleanup_partition_resources -- drop heap slots, tuplestore, and tie slot
 * for all partitions.  Called from both ExecEnd and ExecReScan.
 */
static void
cleanup_partition_resources(PartitionTopKState *node)
{
	HASH_SEQ_STATUS status;
	PartitionHashEntry *entry;

	hash_seq_init(&status, node->partition_hash);
	while ((entry = (PartitionHashEntry *) hash_seq_search(&status)) != NULL)
	{
		PartitionRuntimeState *rts = entry->state;
		int		i;

		/* Drop all slots in the heap to decrement tuple descriptor refcounts */
		if (rts->topk_heap)
		{
			for (i = 0; i < rts->topk_heap->bh_size; i++)
			{
				TupleTableSlot *s = (TupleTableSlot *)
					DatumGetPointer(rts->topk_heap->bh_nodes[i]);

				if (s)
					ExecDropSingleTupleTableSlot(s);
			}
		}

		if (rts->tie_store)
			tuplestore_end(rts->tie_store);
		if (rts->tie_key_slot)
			ExecDropSingleTupleTableSlot(rts->tie_key_slot);
	}
}

/*
 * init_partition_hash -- create (or re-create) the partition hash table.
 */
static HTAB *
init_partition_hash(MemoryContext hcxt)
{
	HASHCTL		ctl;

	ctl.keysize = sizeof(PartitionHashKey);
	ctl.entrysize = sizeof(PartitionHashEntry);
	ctl.hash = partition_hash_hash;
	ctl.match = partition_hash_match;
	ctl.hcxt = hcxt;

	return hash_create("PartitionTopK Hash Table",
					   PARTITION_HASH_INIT_SIZE,
					   &ctl,
					   HASH_ELEM | HASH_FUNCTION | HASH_CONTEXT | HASH_COMPARE);
}


/* ----------------------------------------------------------------
 *		ExecPartitionTopK
 *
 *		Main execution entry point.  Consumes all input tuples on
 *		first call, partitions them into bounded heaps, then returns
 *		results one-at-a-time from the finalized output array.
 * ----------------------------------------------------------------
 */
static TupleTableSlot *
ExecPartitionTopK(PlanState *pstate)
{
	PartitionTopKState *node = (PartitionTopKState *) pstate;
	PlanState  *outerPlan = outerPlanState(node);

	/* Phase 1: consume all input tuples */
	if (!node->input_consumed)
	{
		while (true)
		{
			TupleTableSlot	   *slot;
			PartitionHashKey	lookup_key;
			PartitionHashEntry *entry;
			bool				found;

			CHECK_FOR_INTERRUPTS();

			slot = ExecProcNode(outerPlan);
			if (TupIsNull(slot))
				break;

			fill_partition_key(node, slot, &lookup_key);

			entry = (PartitionHashEntry *) hash_search(
				node->partition_hash, &lookup_key, HASH_ENTER, &found);

			if (!found)
			{
				MemoryContext datumctx;
				int		i;

				/*
				 * Copy the key into partitionContext.  We must switch to
				 * partitionContext before calling datumCopy, because datumCopy
				 * uses palloc internally for pass-by-reference types, and we
				 * need the datum copies to live in partitionContext alongside
				 * the hash entry (not in a short-lived per-tuple context).
				 */
				entry->key.node = node;
				entry->key.nkeys = lookup_key.nkeys;
				entry->key.values = (Datum *)
					MemoryContextAlloc(node->partitionContext,
									   lookup_key.nkeys * sizeof(Datum));
				entry->key.isnull = (bool *)
					MemoryContextAlloc(node->partitionContext,
									   lookup_key.nkeys * sizeof(bool));

				datumctx = MemoryContextSwitchTo(node->partitionContext);
				for (i = 0; i < lookup_key.nkeys; i++)
				{
					if (lookup_key.isnull[i])
					{
						entry->key.values[i] = (Datum) 0;
						entry->key.isnull[i] = true;
					}
					else
					{
						entry->key.values[i] = datumCopy(
							lookup_key.values[i],
							node->partitionColTypByVals[i],
							node->partitionColTypLens[i]);
						entry->key.isnull[i] = false;
					}
				}
				MemoryContextSwitchTo(datumctx);

				entry->state = create_partition_state(node);
			}

			insert_tuple_into_partition_state(node, entry->state, slot);
		}
		node->input_consumed = true;
	}

	/* Phase 2: finalize all partitions into output array */
	if (!node->all_partitions_finalized)
	{
		finalize_all_partitions(node);
		node->all_partitions_finalized = true;
	}

	/* Phase 3: return tuples one at a time */
	if (node->output_index < node->final_output_count)
		return node->final_output_array[node->output_index++];

	return ExecClearTuple(node->ps.ps_ResultTupleSlot);
}


/* ----------------------------------------------------------------
 *		ExecEndPartitionTopK
 *
 *		Shutdown and release all resources.
 * ----------------------------------------------------------------
 */
void
ExecEndPartitionTopK(PartitionTopKState *node)
{
	/* End the outer plan first */
	ExecEndNode(outerPlanState(node));

	/* Clean out the result tuple slot */
	ExecClearTuple(node->ps.ps_ResultTupleSlot);

	/* Drop final output slots (allocated in es_query_cxt) */
	if (node->final_output_array)
	{
		int		i;

		for (i = 0; i < node->final_output_count; i++)
		{
			if (node->final_output_array[i])
				ExecDropSingleTupleTableSlot(node->final_output_array[i]);
		}
		pfree(node->final_output_array);
		node->final_output_array = NULL;
		node->final_output_count = 0;
	}

	/* Clean up all per-partition resources before destroying memory context */
	cleanup_partition_resources(node);

	/* Release all memory used by this node */
	MemoryContextDelete(node->partitionContext);
}


/* ----------------------------------------------------------------
 *		ExecReScanPartitionTopK
 *
 *		Reset state for a new scan.  Frees output slots (in es_query_cxt),
 *		cleans up per-partition resources, resets partitionContext, and
 *		re-creates the hash table.
 * ----------------------------------------------------------------
 */
void
ExecReScanPartitionTopK(PartitionTopKState *node)
{
	/* Free final output slots (allocated in es_query_cxt, not partitionContext) */
	if (node->final_output_array)
	{
		int		i;

		for (i = 0; i < node->final_output_count; i++)
		{
			if (node->final_output_array[i])
				ExecDropSingleTupleTableSlot(node->final_output_array[i]);
		}
		pfree(node->final_output_array);
	}

	/* Clean up all per-partition resources before resetting memory context */
	cleanup_partition_resources(node);

	MemoryContextReset(node->partitionContext);

	node->partition_hash = init_partition_hash(node->partitionContext);

	node->input_consumed = false;
	node->all_partitions_finalized = false;
	node->final_output_array = NULL;
	node->final_output_count = 0;
	node->output_index = 0;

	ExecReScan(outerPlanState(node));
}


/* ----------------------------------------------------------------
 *		ExecInitPartitionTopK
 *
 *		Initialize the PartitionTopK executor node.
 * ----------------------------------------------------------------
 */
PartitionTopKState *
ExecInitPartitionTopK(PartitionTopK *node, EState *estate, int eflags)
{
	PartitionTopKState *state;
	Plan	   *outerPlan;
	int			i;

	Assert(!(eflags & (EXEC_FLAG_BACKWARD | EXEC_FLAG_MARK)));

	state = makeNode(PartitionTopKState);
	state->ps.plan = (Plan *) node;
	state->ps.state = estate;
	state->ps.ExecProcNode = ExecPartitionTopK;

	ExecAssignExprContext(estate, &state->ps);
	state->ps.ps_ProjInfo = NULL;

	/* Create memory context for per-partition data (reset on rescan) */
	state->partitionContext = AllocSetContextCreate(CurrentMemoryContext,
													"PartitionTopKContext",
													ALLOCSET_DEFAULT_SIZES);

	/* Save plan parameters */
	state->numPartitionCols = node->numPartitionCols;
	state->partitionColIdx = node->partitionColIdx;
	state->top_k = node->top_k;
	if (state->top_k < 1)
		elog(ERROR, "partition top-k requires top_k >= 1, got %d",
			 state->top_k);
	state->numSortCols = node->numSortCols;
	state->sortColIdx = node->sortColIdx;
	state->sortOperators = node->sortOperators;
	state->collations = node->collations;
	state->nullsFirst = node->nullsFirst;

	/* Initialize outer plan */
	outerPlan = outerPlan(node);
	outerPlanState(state) = ExecInitNode(outerPlan, estate, eflags);
	state->tupdesc = ExecGetResultType(outerPlanState(state));

	ExecInitResultTupleSlotTL(&state->ps, &TTSOpsMinimalTuple);

	/*
	 * Allocate type info, sort keys, hash/eq functions, and scratch buffers
	 * in es_query_cxt (NOT partitionContext) because partitionContext is reset
	 * on rescan and these structures must survive across rescans.
	 */
	{
		MemoryContext oldctx = MemoryContextSwitchTo(estate->es_query_cxt);

		/* Cache partition column type info from actual tuple descriptor */
		state->partitionColTypes =
			(Oid *) palloc(sizeof(Oid) * state->numPartitionCols);
		state->partitionColTypLens =
			(int16 *) palloc(sizeof(int16) * state->numPartitionCols);
		state->partitionColTypByVals =
			(bool *) palloc(sizeof(bool) * state->numPartitionCols);

		/* Look up type-aware hash/eq functions for partition keys */
		state->partitionHashFuncs =
			(FmgrInfo *) palloc(sizeof(FmgrInfo) * state->numPartitionCols);
		state->partitionEqFuncs =
			(FmgrInfo *) palloc(sizeof(FmgrInfo) * state->numPartitionCols);
		state->partitionCollations =
			(Oid *) palloc(sizeof(Oid) * state->numPartitionCols);

		for (i = 0; i < state->numPartitionCols; i++)
		{
			Form_pg_attribute att;
			TypeCacheEntry *typentry;

			att = TupleDescAttr(state->tupdesc,
								state->partitionColIdx[i] - 1);

			state->partitionColTypes[i] = att->atttypid;
			state->partitionColTypLens[i] = att->attlen;
			state->partitionColTypByVals[i] = att->attbyval;
			state->partitionCollations[i] = att->attcollation;

			typentry = lookup_type_cache(att->atttypid,
										 TYPECACHE_HASH_PROC_FINFO |
										 TYPECACHE_EQ_OPR_FINFO);

			if (!OidIsValid(typentry->hash_proc_finfo.fn_oid))
				elog(ERROR, "could not find hash function for type %u",
					 att->atttypid);
			if (!OidIsValid(typentry->eq_opr_finfo.fn_oid))
				elog(ERROR, "could not find equality function for type %u",
					 att->atttypid);

			fmgr_info_copy(&state->partitionHashFuncs[i],
						   &typentry->hash_proc_finfo,
						   CurrentMemoryContext);
			fmgr_info_copy(&state->partitionEqFuncs[i],
						   &typentry->eq_opr_finfo,
						   CurrentMemoryContext);
		}

		/* Allocate and initialize sort support */
		state->sortKeys = (SortSupport)
			palloc0(state->numSortCols * sizeof(SortSupportData));
		for (i = 0; i < state->numSortCols; i++)
		{
			SortSupport ssup = &state->sortKeys[i];

			ssup->ssup_cxt = estate->es_query_cxt;
			ssup->ssup_collation = node->collations
				? node->collations[i] : InvalidOid;
			ssup->ssup_nulls_first = node->nullsFirst[i];
			PrepareSortSupportFromOrderingOp(node->sortOperators[i], ssup);
		}

		/* Scratch buffers for partition key lookup */
		state->temp_key_values =
			(Datum *) palloc(sizeof(Datum) * state->numPartitionCols);
		state->temp_key_isnull =
			(bool *) palloc(sizeof(bool) * state->numPartitionCols);

		MemoryContextSwitchTo(oldctx);
	}

	/* Initialize partition hash table */
	state->partition_hash = init_partition_hash(state->partitionContext);

	state->input_consumed = false;
	state->all_partitions_finalized = false;
	state->final_output_array = NULL;
	state->final_output_count = 0;
	state->output_index = 0;

	return state;
}
