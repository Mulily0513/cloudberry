#ifndef ICEBERG_TASK_READER_H
#define ICEBERG_TASK_READER_H

#include "postgres.h"
#include "src/dlproxy/datalake.h"
#include "src/provider/common/utils.h"

typedef struct IcebergTaskReader
{
	Reader       *dataReader;
	FileScanTask *fileScanTask;
	int           taskId;
	uint32        fileId;			/* File ID in the index map */
} IcebergTaskReader;

Reader *createIcebergTaskReader(void *args);
bool icebergTaskReaderNext(Reader *reader, DatalakeInternalRecord *record);
void icebergTaskReaderClose(Reader *reader);

#endif // ICEBERG_TASK_READER_H
