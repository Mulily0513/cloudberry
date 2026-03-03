#pragma once

#include "postgres.h"
#include "src/datalake_type.h"
#include "cdb/cdbdisp.h"
#include "cdb/cdbdispatchresult.h"
#include "nodes/pg_list.h"
#include "libpq/libpq-fe.h"

extern void FDW_SendMeta(bytea *msg);
extern void FDW_RecvMeta(CdbDispatcherState * ds);
extern int RecvMetaMethod(PGconn *conn, int msgLength);

extern bytea* FDW_serializeMeta(void *meta);
extern List* FDW_deserializeMeta(struct pg_result *res);

/* Serialize file list to JSON for POST request body */
extern void FDW_serialize_file_list_to_json(List *file_list, StringInfoData *output);

extern List *FDW_ResultMetaList;
extern ProcessDispatchResult_hook_type datalake_prev_ProcessDispatchResult;