#include "hudi_read.h"

namespace Datalake {
namespace Internal {

void hudiRead::createHandler(void *sstate)
{
    initParameter(sstate);
    protocolContext = datalakeCreateContext(scanstate->options);
    datalakeProtocolImportStart(scanstate, protocolContext, includes_columns);
}

int64_t hudiRead::read(void *values, void *nulls)
{
    record_.nulls = (bool *)nulls;
    record_.values = (Datum *)values;
    protocolContext->record = &record_;

    return datalakeRowReaderNext(protocolContext->file->reader, protocolContext->record);
}

void hudiRead::destroyHandler()
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
