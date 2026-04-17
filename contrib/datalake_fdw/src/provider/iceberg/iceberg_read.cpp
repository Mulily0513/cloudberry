#include "iceberg_read.h"

extern "C"
{
#include "iceberg_file_index.h"
}

namespace Datalake {
namespace Internal {

void icebergRead::createHandler(void *sstate)
{
    initParameter(sstate);
    protocolContext = datalakeCreateContext(scanstate->options);
    datalakeProtocolImportStart(scanstate, protocolContext, includes_columns);
}

int64_t icebergRead::read(void *values, void *nulls)
{
    record_.nulls = (bool *)nulls;
    record_.values = (Datum *)values;
    protocolContext->record = &record_;

    /* Fast path: bypass datalakeRowReaderNext overhead */
    if (datalakeRowReaderFastNext(protocolContext->file->reader, protocolContext->record))
        return 1;

    /* Slow path: file switch or first call */
    return datalakeRowReaderNext(protocolContext->file->reader, protocolContext->record);
}

int64_t icebergRead::read(void *values, void *nulls, void *tid)
{
    ItemPointer tidPtr = (ItemPointer)tid;

    record_.nulls = (bool *)nulls;
    record_.values = (Datum *)values;
    protocolContext->record = &record_;

    /* Fast path: bypass datalakeRowReaderNext overhead */
    if (datalakeRowReaderFastNext(protocolContext->file->reader, protocolContext->record))
    {
        if (tidPtr != NULL)
        {
            icebergEncodeTID(tidPtr, protocolContext->record->fileId,
                             protocolContext->record->position);
        }
        return 1;
    }

    /* Slow path: file switch or first call */
    int64_t result = datalakeRowReaderNext(protocolContext->file->reader, protocolContext->record);

    if (result && tidPtr != NULL)
    {
        icebergEncodeTID(tidPtr, protocolContext->record->fileId,
                         protocolContext->record->position);
    }

    return result;
}

void icebergRead::destroyHandler()
{
    releaseResources();
    /*
     * record_ is a C++ member (not palloc'd), so clear the pointer
     * before datalakeCleanupContext() which would pfree() it.
     */
    protocolContext->record = NULL;
    datalakeCleanupContext(protocolContext);
}

}
}
