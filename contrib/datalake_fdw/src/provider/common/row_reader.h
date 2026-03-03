#ifndef ROW_READER_H
#define ROW_READER_H

#include "postgres.h"
#include "src/dlproxy/datalake.h"
#include "utils.h"

DatalakeRowReader *datalakeCreateRowReader(MemoryContext mcxt,
						   TupleDesc tupleDesc,
						   int nTblColumn,
						   bool *attrUsed,
						   gopherFS gopherFilesystem,
						   List *combinedScanTasks,
						   DLTblFmt format,
						   ExternalTableMetadata *tableOptions);
bool datalakeRowReaderNext(DatalakeRowReader *reader, DatalakeInternalRecord *record);
void datalakeRowReaderClose(DatalakeRowReader *reader);

/*
 * The following functions are migrated from datalake_extension.c in hashdata 3X.
 * see https://code.hashdata.xyz/hashdata/hashdata/-/blob/v3.x/gpcontrib/datalake_extension/src/datalake_extension.c?ref_type=heads
 */
DatalakeProtocolContext *datalakeCreateContext(dataLakeOptions *options);
void datalakeCleanupContext(DatalakeProtocolContext *context);
void datalakeProtocolImportStart(dataLakeFdwScanState *scanstate, DatalakeProtocolContext *context, bool *attrUsed);


#endif // ROW_READER_H
