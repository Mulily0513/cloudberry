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
    protocolContext->record = (DatalakeInternalRecord *) palloc0(sizeof(DatalakeInternalRecord));
    protocolContext->record->nulls = (bool *)nulls;
    protocolContext->record->values = (Datum *)values;

    return datalakeRowReaderNext(protocolContext->file->reader, protocolContext->record);
}

int64_t icebergRead::read(void *values, void *nulls, void *tid)
{
    ItemPointer tidPtr = (ItemPointer)tid;
    int64_t result;

    protocolContext->record = (DatalakeInternalRecord *) palloc0(sizeof(DatalakeInternalRecord));
    protocolContext->record->nulls = (bool *)nulls;
    protocolContext->record->values = (Datum *)values;

    result = datalakeRowReaderNext(protocolContext->file->reader, protocolContext->record);

    if (result && tidPtr != NULL)
    {
        uint32 fileId = protocolContext->record->fileId;
        int64_t position = protocolContext->record->position;

        icebergEncodeTID(tidPtr, fileId, position);
    }

    return result;
}

void icebergRead::destroyHandler()
{
    releaseResources();
    datalakeCleanupContext(protocolContext);
}

}
}
