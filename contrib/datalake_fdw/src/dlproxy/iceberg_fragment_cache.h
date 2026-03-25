/*-------------------------------------------------------------------------
 *
 * iceberg_fragment_cache.h
 *		Per-backend cache for Iceberg fragment JSON responses.
 *
 * Caches raw JSON responses from the Java datalake_agent, keyed by
 * relation OID. Uses Iceberg snapshot IDs as cache version tags:
 * if the snapshot hasn't changed, the cached JSON is still valid.
 *
 *-------------------------------------------------------------------------
 */
#ifndef ICEBERG_FRAGMENT_CACHE_H
#define ICEBERG_FRAGMENT_CACHE_H

#include "postgres.h"

#define FRAGMENT_CACHE_MAX_ENTRIES 64

typedef struct IcebergFragmentCacheEntry
{
	Oid     relid;
	int64   snapshot_id;
	char   *raw_json;       /* allocated in TopMemoryContext */
	size_t  raw_json_size;
	int     lru_counter;    /* lower = older */
} IcebergFragmentCacheEntry;

/*
 * Look up a cached fragment entry for the given relid.
 * Returns NULL if not found.
 */
extern IcebergFragmentCacheEntry *iceberg_fragment_cache_lookup(Oid relid);

/*
 * Store a fragment JSON response in the cache.
 * Copies json into TopMemoryContext. Evicts LRU entry if full.
 */
extern void iceberg_fragment_cache_store(Oid relid, int64 snapshotId,
										 const char *json, size_t jsonSize);

/*
 * Invalidate cache entry for the given relid (e.g., after write).
 */
extern void iceberg_fragment_cache_invalidate(Oid relid);

/*
 * Invalidate all cache entries.
 */
extern void iceberg_fragment_cache_invalidate_all(void);

#endif /* ICEBERG_FRAGMENT_CACHE_H */
