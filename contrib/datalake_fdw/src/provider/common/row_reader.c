#include "postgres.h"
#include "utils/builtins.h"
#include "utils/memutils.h"
#include "utils/hsearch.h"
#include "nodes/parsenodes.h"
#include "gopher/gopher.h"
#include "common/hashfn.h"

#include "src/common/fileSystemWrapper.h"
#include "src/provider/iceberg/iceberg_task_reader.h"
#include "src/provider/iceberg/iceberg_file_index.h"
#include "src/provider/hudi/hudi_task_reader.h"
#include "row_reader.h"
#include "src/common/dataBufferArray_c.h"
#include "src/datalake_def.h"

extern bool disableCacheFile;

static bool resownerCallbackRegistered;
static DatalakeRemoteFileHandle *openRemoteHandles;

static void flatCombinedTasks(List *combinedScanTasks, List **fileScanTasks);
static bool isCacheEnabled(char *cacheEnabled);
static void icebergFileIndexMapInitialize(DatalakeRowReader *reader);

static List *
createFieldDescription(TupleDesc tupleDesc)
{
	int i;
	List *result = NIL;

	for (i = 0; i < tupleDesc->natts; i++)
	{
		DatalakeFieldDescription *fieldDesc = (DatalakeFieldDescription *) palloc0(sizeof(DatalakeFieldDescription));
		Oid typeOid = TupleDescAttr(tupleDesc, i)->atttypid;
		int typeMod = TupleDescAttr(tupleDesc, i)->atttypmod;
		const char *attname = NameStr(TupleDescAttr(tupleDesc, i)->attname);

		strcpy(fieldDesc->name, attname);
		fieldDesc->typeOid = typeOid;
		fieldDesc->typeMod = typeMod;

		result = lappend(result, fieldDesc);
	}

	return result;
}

static DatalakeRemoteFileHandle *
createRemoteFileHandle(gopherFS gopherFilesystem)
{
	DatalakeRemoteFileHandle *result;

	result = MemoryContextAlloc(TopMemoryContext, sizeof(DatalakeRemoteFileHandle));
	result->gopherFilesystem = gopherFilesystem;
	result->reader = NULL;
	result->prev = NULL;
	result->next = openRemoteHandles;
	result->owner = CurrentResourceOwner;

	if (openRemoteHandles)
		openRemoteHandles->prev = result;

	openRemoteHandles = result;

	return result;
}

static void
destroyRemoteFileHandle(DatalakeRemoteFileHandle *handle)
{
	if (handle->prev)
		handle->prev->next = handle->next;
	else
		openRemoteHandles = openRemoteHandles->next;

	if (handle->next)
		handle->next->prev = handle->prev;

	if (handle->gopherFilesystem)
		gopherDisconnect(handle->gopherFilesystem);

	if (handle->reader)
		datalakeRowReaderClose(handle->reader);

	pfree(handle);
}

static void
remoteFileAbortCallback(ResourceReleasePhase phase,
						bool isCommit,
						bool isTopLevel,
						void *arg)
{
	DatalakeRemoteFileHandle *cur;
	DatalakeRemoteFileHandle *next;

	if (phase != RESOURCE_RELEASE_AFTER_LOCKS)
		return;

	next = openRemoteHandles;
	while (next)
	{
		cur = next;
		next = cur->next;

		if (cur->owner == CurrentResourceOwner)
			destroyRemoteFileHandle(cur);
	}
}

static Reader icebergHandler = {
	createIcebergTaskReader,
	icebergTaskReaderNext,
	icebergTaskReaderClose
};

static Reader hudiHandler = {
	createHudiTaskReader,
	hudiTaskReaderNext,
	hudiTaskReaderClose
};

static Reader deltaLakeHandler = {
	NULL,
	NULL,
	NULL
};

typedef struct FileMapEntry {
    char    *filename;
    int     file_id;
} FileMapEntry;

/* 
 * Custom hash func: Derefference pointer and hash the string content
 */
static uint32
filename_hash(const void *key, Size keysize)
{
    const char *str = (const char *) key;
    return DatumGetUInt32(hash_any((const unsigned char *) str, strlen(str)));
}

/* 
 * Custom hash cmp: Derefference pointer and compare the string content
 */
static int
filename_match(const void *key1, const void *key2, Size keysize)
{
    const char *s1 = (const char *) key1;
    const char *s2 = (const char *) key2;

    return strcmp(s1, s2);
}

DatalakeRowReader *
datalakeCreateRowReader(MemoryContext mcxt,
				TupleDesc tupleDesc,
				int nTblColumn,
				bool *attrUsed,
				gopherFS gopherFilesystem,
				List *combinedScanTasks,
				DLTblFmt format,
				ExternalTableMetadata *tableOptions)
{
	DatalakeRowReader *reader = palloc0(sizeof(DatalakeRowReader));

	flatCombinedTasks(combinedScanTasks, &reader->fileScanTasks);
	list_free(combinedScanTasks);

	reader->datafileDesc = createFieldDescription(tupleDesc);
	reader->attrUsed = attrUsed;
	reader->gopherFilesystem = gopherFilesystem;
	reader->mcxt = mcxt;
	reader->tableOptions = tableOptions;
	reader->buffer = datalake_buffer_arr_create(tupleDesc->natts + 8);
	reader->format = format;
	reader->fileIndexMapInitialized = false;  /* Will be lazily initialized on first iteration */

	/* Set handler based on format */
	if (FORMAT_IS_ICEBERG(format))
		reader->handler = &icebergHandler;
	else if (FORMAT_IS_HUDI(format))
		reader->handler = &hudiHandler;
	else
		reader->handler = &deltaLakeHandler;

	reader->taskMcxt = AllocSetContextCreate(CurrentMemoryContext,
											 "RowReaderContext",
											 ALLOCSET_DEFAULT_MINSIZE,
											 ALLOCSET_DEFAULT_INITSIZE,
											 ALLOCSET_DEFAULT_MAXSIZE);
	reader->curMcxt = CurrentMemoryContext;

	return reader;
}

bool
datalakeRowReaderNext(DatalakeRowReader *reader, DatalakeInternalRecord *record)
{
	/*
	 * For Iceberg tables, lazily initialize fileIndexMap on first iteration.
	 * This ensures we only build the index when actually needed (i.e., during UPDATE/DELETE),
	 * avoiding unnecessary overhead for plain SELECT queries.
	 */
	if (FORMAT_IS_ICEBERG(reader->format) &&
		datalake_iceberg_file_index_map != NULL &&
		!reader->fileIndexMapInitialized)
	{
		icebergFileIndexMapInitialize(reader);
	}

	while (true)
	{
		if (reader->handler->Next(reader->curReader, record))
		{
			return true;
		}
		else if (list_length(reader->fileScanTasks) > 0)
		{
			DatalakeReaderInitInfo initInfo;
			FileScanTask *curTask;

			reader->handler->Close(reader->curReader);
			MemoryContextSwitchTo(reader->curMcxt);

			curTask = list_nth(reader->fileScanTasks, 0);

			initInfo.taskId = reader->curReaderIndex++;
			initInfo.mcxt = reader->mcxt;
			initInfo.datafileDesc = reader->datafileDesc;
			initInfo.attrUsed = reader->attrUsed;
			initInfo.gopherFilesystem = reader->gopherFilesystem;
			initInfo.fileScanTask = curTask;
			initInfo.tableOptions = reader->tableOptions;
			initInfo.buffer = reader->buffer;

			/* For Iceberg tables, use the file ID stored in the task */
			if (datalake_iceberg_file_index_map != NULL)
			{
				uint32 fileId = curTask->fileId;
				initInfo.fileId = fileId;
				elog(DEBUG2, "Task %d using file ID %u for %s",
					 initInfo.taskId, fileId, curTask->dataFile->filePath);
			}
			else
			{
				initInfo.fileId = 0;  /* Not used for non-Iceberg tables */
			}

			MemoryContextReset(reader->taskMcxt);
			MemoryContextSwitchTo(reader->taskMcxt);

			reader->curReader = reader->handler->Create(&initInfo);
			reader->fileScanTasks = list_delete_first(reader->fileScanTasks);
		}
		else
		{
			reader->handler->Close(reader->curReader);
			MemoryContextSwitchTo(reader->curMcxt);
			reader->curReader = NULL;
			return false;
		}
	}

	return false;
}

void
datalakeRowReaderClose(DatalakeRowReader *reader)
{
	if (reader->curReader)
		reader->handler->Close(reader->curReader);

	list_free_deep(reader->datafileDesc);
	MemoryContextDelete(reader->taskMcxt);
	if (reader->buffer)
		datalake_buffer_arr_destroy(reader->buffer);
	pfree(reader);
}

static void
flatCombinedTasks(List *combinedScanTasks, List **fileScanTasks)
{
	ListCell *lco;
	ListCell *lci;
	List *combinedScanTask;

	foreach(lco, combinedScanTasks)
	{
		combinedScanTask = (List *) lfirst(lco);

		foreach(lci, combinedScanTask)
		{
			FileScanTask *task = (FileScanTask *) lfirst(lci);

			*fileScanTasks = lappend(*fileScanTasks, task);
		}

		list_free(combinedScanTask);
	}
}

/*
 * Lazy initialization function for Iceberg file index map.
 * This function is called on the first iteration to build the file index map.
 * It deduplicates files using a hash table and populates the global fileIndexMap.
 */
static void
icebergFileIndexMapInitialize(DatalakeRowReader *reader)
{
	ListCell *lc;
	uint32 fileCount = 0;
	HTAB *filePathToIdMap = NULL;  /* Map: filePath -> fileId */
	HASHCTL hashCtl;

	Assert(!reader->fileIndexMapInitialized);

	/* Create hash set for file path deduplication */
	MemSet(&hashCtl, 0, sizeof(hashCtl));
	hashCtl.keysize = sizeof(char*);  /* Max file path length */
	hashCtl.entrysize = sizeof(FileMapEntry);  /* Store fileId as value */
	hashCtl.hash = filename_hash;
	hashCtl.match = filename_match;
	hashCtl.hcxt = reader->mcxt;

	filePathToIdMap = hash_create("Iceberg file path to ID map",
	                              1024,  /* Initial size */
	                              &hashCtl,
	                              HASH_ELEM | HASH_COMPARE | HASH_FUNCTION | HASH_CONTEXT);

	/* Build file index by scanning all tasks, deduplicating files */
	foreach(lc, reader->fileScanTasks)
	{
		FileScanTask *task = (FileScanTask *) lfirst(lc);
		const char *filePath = task->dataFile->filePath;
		int64 recordCount = task->dataFile->recordCount;
		bool found;
		FileMapEntry *entry;
		uint32 fileId;

		/* Check if file path already has an assigned ID */
		entry = (FileMapEntry *) hash_search(filePathToIdMap,
		                                     (void *) &filePath,
		                                     HASH_FIND, &found);

		if (!found)
		{
			/* File not seen before, assign new ID and add to global index */
			fileId = icebergAddFile(datalake_iceberg_file_index_map, filePath, recordCount);

			/* Store ID in hash map for future lookups */
			entry = (FileMapEntry *) hash_search(filePathToIdMap,
			                                     (void *) &filePath,
			                                     HASH_ENTER, &found);
			entry->file_id = fileId;

			fileCount++;
		}
		else
		{
			/* File already exists, retrieve assigned ID */
			fileId = entry->file_id;
		}

		/* Store file ID in task for later use */
		task->fileId = fileId;
	}

	/* Clean up hash map */
	hash_destroy(filePathToIdMap);

	elog(DEBUG1, "Built Iceberg file index with %u unique files", fileCount);

	/* Mark as initialized */
	reader->fileIndexMapInitialized = true;
}

static bool
checkInterrupt(void)
{
	if (!InterruptPending)
		return false;

	if (InterruptHoldoffCount != 0 || CritSectionCount != 0)
		return false;

	return true;
}

DatalakeProtocolContext *
datalakeCreateContext(dataLakeOptions *options)
{
	DatalakeProtocolContext *context;
	gopherConfig    *gopherConfig;
	gopherFS        fs;

	context = (DatalakeProtocolContext *)palloc0(sizeof(DatalakeProtocolContext));

	disableCacheFile = !isCacheEnabled(options->cache_enabled);
	gopherConfig = datalakeCreateGopherConfig((void*)(options->gopher));
	gopherUserCanceledCallBack(&checkInterrupt);

	fs = gopherConnect(*gopherConfig);
	if (fs == NULL)
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("failed to connect to gopher: %s", gopherGetLastError())));

	datalakeGopherConfigDestroy(gopherConfig);

	if (!resownerCallbackRegistered)
	{
		RegisterResourceReleaseCallback(remoteFileAbortCallback, NULL);
		resownerCallbackRegistered = true;
	}

	context->file = createRemoteFileHandle(fs);

	return context;
}

static bool
isCacheEnabled(char *cacheEnabled)
{
	if (cacheEnabled)
	{
		if (pg_strcasecmp(cacheEnabled, "false") == 0)
			return false;
		else if (pg_strcasecmp(cacheEnabled, "true") == 0)
			return true;
		else
			ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("invalid value for boolean option \"cache_enabled\": %s", cacheEnabled)));
	}

	return true;
}

void
datalakeCleanupContext(DatalakeProtocolContext *context)
{
	if (context->file)
		destroyRemoteFileHandle(context->file);

	if (context->rowContext)
		MemoryContextDelete(context->rowContext);

	if (context->record)
		pfree(context->record);

	pfree(context);
}

void
datalakeProtocolImportStart(dataLakeFdwScanState *scanstate, DatalakeProtocolContext *context, bool *attrUsed)
{
	int       i = 0;
	ListCell *lc;
	int       numSegments = getgpsegmentCount();
	List     *combinedScanTasks = NIL;

    ExternalTableMetadata *tableOptions = (ExternalTableMetadata *)linitial(scanstate->fragments);

	foreach_with_count(lc, scanstate->fragments, i)
	{
		if (i == 0) continue;
		int idx = i - 1;
		List *combinedScanTask = (List *) lfirst(lc);

		if (GpIdentity.segindex == (idx % numSegments))
			combinedScanTasks = lappend(combinedScanTasks, combinedScanTask);
	}

	context->rowContext = AllocSetContextCreate(CurrentMemoryContext,
												"DatalakeExtensionContext",
												ALLOCSET_DEFAULT_MINSIZE,
												ALLOCSET_DEFAULT_INITSIZE,
												ALLOCSET_DEFAULT_MAXSIZE);

	context->file->reader = datalakeCreateRowReader(context->rowContext,
													scanstate->scan_tupdesc,
													scanstate->rel->rd_att->natts,
													attrUsed,
													context->file->gopherFilesystem,
													combinedScanTasks,
													scanstate->options->format,
													tableOptions);
}
