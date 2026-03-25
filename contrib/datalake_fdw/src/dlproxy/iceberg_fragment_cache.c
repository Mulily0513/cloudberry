/*-------------------------------------------------------------------------
 *
 * iceberg_fragment_cache.c
 *		Per-backend LRU cache for Iceberg fragment JSON responses.
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "utils/memutils.h"

#include "iceberg_fragment_cache.h"

static IcebergFragmentCacheEntry cache_entries[FRAGMENT_CACHE_MAX_ENTRIES];
static int cache_count = 0;
static int lru_clock = 0;

IcebergFragmentCacheEntry *
iceberg_fragment_cache_lookup(Oid relid)
{
	for (int i = 0; i < cache_count; i++)
	{
		if (cache_entries[i].relid == relid)
		{
			cache_entries[i].lru_counter = ++lru_clock;
			return &cache_entries[i];
		}
	}
	return NULL;
}

void
iceberg_fragment_cache_store(Oid relid, int64 snapshotId,
							 const char *json, size_t jsonSize)
{
	IcebergFragmentCacheEntry *entry = NULL;

	/* Check if entry already exists for this relid */
	for (int i = 0; i < cache_count; i++)
	{
		if (cache_entries[i].relid == relid)
		{
			entry = &cache_entries[i];
			break;
		}
	}

	/* If not found, find a slot */
	if (entry == NULL)
	{
		if (cache_count < FRAGMENT_CACHE_MAX_ENTRIES)
		{
			entry = &cache_entries[cache_count++];
			entry->raw_json = NULL;
		}
		else
		{
			/* Evict LRU entry */
			int min_lru = cache_entries[0].lru_counter;
			int min_idx = 0;

			for (int i = 1; i < cache_count; i++)
			{
				if (cache_entries[i].lru_counter < min_lru)
				{
					min_lru = cache_entries[i].lru_counter;
					min_idx = i;
				}
			}
			entry = &cache_entries[min_idx];
		}
	}

	/* Free old json if any */
	if (entry->raw_json != NULL)
	{
		pfree(entry->raw_json);
		entry->raw_json = NULL;
	}

	/* Store new entry in TopMemoryContext */
	{
		MemoryContext oldcxt = MemoryContextSwitchTo(TopMemoryContext);

		entry->relid = relid;
		entry->snapshot_id = snapshotId;
		entry->raw_json = palloc(jsonSize);
		memcpy(entry->raw_json, json, jsonSize);
		entry->raw_json_size = jsonSize;
		entry->lru_counter = ++lru_clock;

		MemoryContextSwitchTo(oldcxt);
	}
}

void
iceberg_fragment_cache_invalidate(Oid relid)
{
	for (int i = 0; i < cache_count; i++)
	{
		if (cache_entries[i].relid == relid)
		{
			if (cache_entries[i].raw_json != NULL)
			{
				pfree(cache_entries[i].raw_json);
				cache_entries[i].raw_json = NULL;
			}

			/* Move last entry to fill the gap */
			if (i < cache_count - 1)
				cache_entries[i] = cache_entries[cache_count - 1];

			cache_count--;
			return;
		}
	}
}

void
iceberg_fragment_cache_invalidate_all(void)
{
	for (int i = 0; i < cache_count; i++)
	{
		if (cache_entries[i].raw_json != NULL)
		{
			pfree(cache_entries[i].raw_json);
			cache_entries[i].raw_json = NULL;
		}
	}
	cache_count = 0;
	lru_clock = 0;
}
