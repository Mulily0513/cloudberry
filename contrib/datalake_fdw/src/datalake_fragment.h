#ifndef DATALAKE_FRAGMENT_H
#define DATALAKE_FRAGMENT_H


#include "datalake_def.h"

List *datalakeGetExternalFragmentList(Relation relation, List *quals, dataLakeOptions *options, int64_t *totalSize);

List *datalakeDeserializeExternalFragmentList(Relation relation, List *quals, dataLakeOptions *options, List *fragmentInfo);

List *datalakeGetNextPartitionFragmentList(dataLakeOptions *options, int64_t *totalSize);

List *datalakeGetFragmentList(dataLakeOptions *options, int64_t *totalSize);

void datalakeCommitExternalWrite(Relation relation, dataLakeFdwScanState *sstate, List *file_list);

char *datalakeGetExternalWriteLocation(Oid relid);

IcebergTableStatistics *datalakeGetTableStatistics(Oid relid, dataLakeOptions *options);


void datalakeFreeFragmentLists(List *fragments);

#endif
