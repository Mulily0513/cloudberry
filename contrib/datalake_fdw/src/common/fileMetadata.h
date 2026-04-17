#pragma once

#include "postgres.h"
#include "cdb/cdbdisp.h"
#include "cdb/cdbdispatchresult.h"
#include "nodes/pg_list.h"
#include "libpq/libpq-fe.h"
#include "src/datalake_type.h"

extern void FDW_SendMeta(bytea *msg);
extern void FDW_RecvMeta(CdbDispatcherState * ds);
extern int RecvMetaMethod(PGconn *conn, int msgLength);

/* Serialization: relid is included in wire format for per-Oid routing */
extern bytea* FDW_serializeMeta(void *meta, Oid relid);

/* Serialize file list to JSON for POST request body */
extern void FDW_serialize_file_list_to_json(List *file_list, StringInfoData *output);

/* QD-side per-Oid metadata map (replaces former FDW_ResultMetaList) */
extern void  FDW_InitMetaMap(void);
extern List *FDW_GetMetaList(Oid relid);
extern void  FDW_ClearMetaList(Oid relid);

extern void  FDW_FreeMetaList(List *meta_list);

extern ProcessDispatchResult_hook_type datalake_prev_ProcessDispatchResult;
