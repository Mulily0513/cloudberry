/*
 * iceberg_toolkit_volume_fdw.c
 *	  Iceberg volume FDW toolkit functions for reading iceberg tables
 */

#include "postgres.h"
#include "access/table.h"
#include "fmgr.h"
#include "funcapi.h"
#include "utils/builtins.h"
#include "utils/memutils.h"
#include "nodes/execnodes.h"
#include "nodes/makefuncs.h"
#include "nodes/pathnodes.h"
#include "executor/executor.h"
#include "foreign/foreign.h"
#include "foreign/fdwapi.h"
#include "catalog/pg_foreign_server.h"
#include "catalog/pg_foreign_data_wrapper.h"
#include "catalog/pg_type.h"
#include "catalog/pg_operator.h"
#include "catalog/namespace.h"
#include "optimizer/pathnode.h"
#include "optimizer/optimizer.h"
#include "optimizer/cost.h"
#include "utils/syscache.h"
#include "utils/lsyscache.h"
#include "utils/fmgroids.h"
#include <float.h>
#include "../components/agent_cli/c_interface/agent_cjson_builder.hpp"
#include "../common/iceberg_constants.h"
#include "../iceberg_catalog_fdw/iceberg_catalog_fdw.h"
#include "../common/iceberg_toolkit_common.h"
#include "iceberg_volume_fdw.h"

/*
 * Filter condition for WHERE clauses
 */
typedef struct IcebergFilterCondition {
    char *column_name;
    char *operator_name;  /* "=", ">", "<", ">=", "<=", "<>" */
    char *value_str;
} IcebergFilterCondition;


/*
 * Build minimal FDW context - only what ForeignScanState needs
 */
static ForeignScanState*
iceberg_build_fdw_context(TupleDesc tupdesc,
                          List *filter_conditions,
                          char *volumeServer,
                          char *volumeTable,
                          agentcli_cJSON *fragment);

/*
 * Build qual expressions from filter conditions
 */
static List* iceberg_build_qual_expressions(List *filter_conditions, TupleDesc tupdesc);
#include "iceberg_volume_fdw.h"

#define ICEBERG_CATALOG_FDW_NAME "iceberg_catalog_fdw"
#define ICEBERG_VOLUME_FDW_NAME "iceberg_volume_fdw"

/*
 * Scan state structure
 */
typedef struct IcebergToolkitScanState {
    /* Fragment data */
    char *fragmentsJson;
    agentcli_cJSON *fragments;
    int currentFragmentIndex;

    /* location */
    char *location;

    /* Volume FDW state */
    ForeignScanState *volumeScanState;
    FdwRoutine *volumeFdwRoutine;
    bool volumeScanActive;

    /* Limits and counters */
    int rowLimit;
    int rowsReturned;
    bool scanComplete;

    /* Memory management */
    MemoryContext scanContext;

    /* Parameters */
    char *nameSpace;
    char *tableName;
    char *catalogServer;
    char *catalogTable;
    char *volumeServer;
    char *volumeTable;
} IcebergToolkitScanState;

/*
 * Function declarations
 */
extern Datum iceberg_toolkit_volume_fdw(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(iceberg_toolkit_volume_fdw);

/* Operation handlers */
static Datum iceberg_toolkit_read_operation(PG_FUNCTION_ARGS);
static Datum iceberg_toolkit_append_operation(PG_FUNCTION_ARGS);
static Datum iceberg_toolkit_update_operation(PG_FUNCTION_ARGS);

/* Read operation helpers */
static IcebergToolkitScanState* initIcebergScan(IcebergCatalogOperation operation, char *nameSpace, char *tableName, int rowLimit,
                                               char *catalogServer, char *catalogTable,
                                               char *volumeServer, char *volumeTable);
static HeapTuple getNextIcebergTuple(IcebergToolkitScanState *scanState);
static bool startNextFragmentScan(IcebergToolkitScanState *scanState);
static void cleanupIcebergScan(IcebergToolkitScanState *scanState);

/* Insert operation helpers */
static ForeignScanState* setupVolumeScan(agentcli_cJSON *fragment,
                                        char *volumeServer,
                                        char *volumeTable,
                                        char *tableName);

const char*
exec_foreign_insert(char *nameSpace, char *tableName,
                    char *catalogServer, char *catalogTable,
                    char *volumeServer, char *volumeTable, char* location);

const char*
exec_foreign_update(char *nameSpace, char *tableName,
                    char *catalogServer, char *catalogTable,
                    char *volumeServer, char *volumeTable, char* location);

/*
 * Main function - dispatches to operation handlers
 */
Datum iceberg_toolkit_volume_fdw(PG_FUNCTION_ARGS)
{
    FuncCallContext *funcctx;

    if (SRF_IS_FIRSTCALL()) {
        funcctx = SRF_FIRSTCALL_INIT();

        /* Parse operation type */
        char *operation = text_to_cstring(PG_GETARG_TEXT_PP(0));

        if (strcmp(operation, DATALAKEFDW_ICEBERG_OP_READ) == 0) {
            return iceberg_toolkit_read_operation(fcinfo);
        }
        else if (strcmp(operation, DATALAKEFDW_ICEBERG_OP_APPEND) == 0) {
            return iceberg_toolkit_append_operation(fcinfo);
        }
        else if (strcmp(operation, DATALAKEFDW_ICEBERG_OP_UPDATE) == 0) {
            elog(ERROR, "unsupported update operation, "
                "Because there are too many operations involved, it's difficult to write a single SQL function.");
            return iceberg_toolkit_update_operation(fcinfo);
        }
        else {
            elog(ERROR, "Unsupported operation: %s", operation);
        }
    }

    /* This should not be reached for first call */
    funcctx = SRF_PERCALL_SETUP();

    /* Handle read operation continuation */
    if (funcctx->user_fctx) {
        IcebergToolkitScanState *scanState = (IcebergToolkitScanState*)funcctx->user_fctx;
        HeapTuple tuple = getNextIcebergTuple(scanState);

        if (tuple != NULL) {
            SRF_RETURN_NEXT(funcctx, HeapTupleGetDatum(tuple));
        } else {
            cleanupIcebergScan(scanState);
            SRF_RETURN_DONE(funcctx);
        }
    }

    SRF_RETURN_DONE(funcctx);
}

/*
 * Initialize iceberg scan state
 */
static IcebergToolkitScanState* initIcebergScan(IcebergCatalogOperation operation, char *nameSpace, char *tableName, int rowLimit,
                                               char *catalogServer, char *catalogTable,
                                               char *volumeServer, char *volumeTable)
{
    IcebergToolkitScanState *scanState = palloc0(sizeof(IcebergToolkitScanState));

    /* Create memory context */
    scanState->scanContext = AllocSetContextCreate(CurrentMemoryContext,
                                                  "IcebergToolkitScan",
                                                  ALLOCSET_DEFAULT_SIZES);

    /* Initialize parameters */
    scanState->nameSpace = pstrdup(nameSpace);
    scanState->tableName = pstrdup(tableName);
    scanState->catalogServer = pstrdup(catalogServer);
    scanState->catalogTable = pstrdup(catalogTable);
    scanState->volumeServer = pstrdup(volumeServer);
    scanState->volumeTable = pstrdup(volumeTable);

    /* Initialize counters */
    scanState->rowLimit = rowLimit > 0 ? rowLimit : 1000;
    scanState->rowsReturned = 0;
    scanState->scanComplete = false;
    scanState->currentFragmentIndex = 0;
    scanState->volumeScanActive = false;

    StringInfoData result;
    initStringInfo(&result);

    if (operation == ICEBERG_GET_FRAGMENT)
    {
        /* Get fragments from catalog FDW */
        IcebergCatalogFdwState *catalogState = iceberg_execute_catalog_operation(
            operation, nameSpace, tableName,
            catalogServer, catalogTable, volumeServer, volumeTable, NULL);

        if (catalogState->lastStatus != ICEBERG_SUCCESS) {
            iceberg_format_error_response(&result, "scan", nameSpace, tableName, catalogState);
        }

        scanState->fragmentsJson = pstrdup(catalogState->response.responseBody);
        scanState->fragments = agentcli_cJSON_Parse(scanState->fragmentsJson);

        if (!scanState->fragments) {
            elog(ERROR, "Failed to parse fragments JSON");
        }
    }
    else if (operation == ICEBERG_LOAD_TABLE)
    {
        IcebergCatalogFdwState *catalogState = iceberg_execute_catalog_operation(
            operation, nameSpace, tableName,
            catalogServer, catalogTable, volumeServer, volumeTable, NULL);

        if (catalogState->lastStatus != ICEBERG_SUCCESS) {
            iceberg_format_error_response(&result, "scan", nameSpace, tableName, catalogState);
        }
        agentcli_cJSON* appendJson = agentcli_cJSON_Parse(catalogState->response.responseBody);
        agentcli_cJSON* meta_location = agentcli_cJSON_GetObjectItem(appendJson, "metadata-location");
        if (agentcli_cJSON_IsString(meta_location))
        {
            scanState->location = pstrdup(meta_location->valuestring);
        }
    }

    /* Prepare volume FDW routine */
    scanState->volumeFdwRoutine = iceberg_get_volume_fdw_routine();

    return scanState;
}

/*
 * Get next tuple from iceberg table
 */
static HeapTuple getNextIcebergTuple(IcebergToolkitScanState *scanState)
{
    /* Check if we've reached the limit */
    if (scanState->rowsReturned >= scanState->rowLimit || scanState->scanComplete) {
        return NULL;
    }

    /* If no active volume scan, start next fragment */
    if (!scanState->volumeScanActive) {
        if (!startNextFragmentScan(scanState)) {
            scanState->scanComplete = true;
            return NULL;
        }
    }

    /* Get next row from current fragment */
    TupleTableSlot *slot = scanState->volumeFdwRoutine->IterateForeignScan(scanState->volumeScanState);

    if (slot != NULL) {
        scanState->rowsReturned++;
        return ExecCopySlotHeapTuple(slot);
    } else {
        /* Current fragment scan complete, move to next */
        scanState->volumeFdwRoutine->EndForeignScan(scanState->volumeScanState);
        scanState->volumeScanActive = false;
        scanState->currentFragmentIndex++;

        /* Recursively get next tuple from next fragment */
        return getNextIcebergTuple(scanState);
    }
}

/*
 * Start scanning next fragment
 */
static bool startNextFragmentScan(IcebergToolkitScanState *scanState)
{
    /* We only have one fragment object, not an array */
    if (scanState->currentFragmentIndex > 0) {
        return false; /* Already processed the single fragment */
    }

    /* Setup volume scan for this fragment */
    scanState->volumeScanState = setupVolumeScan(scanState->fragments,
                                                scanState->volumeServer,
                                                scanState->volumeTable,
                                                scanState->tableName);
    scanState->volumeFdwRoutine->BeginForeignScan(scanState->volumeScanState, 0);
    scanState->volumeScanActive = true;

    return true;
}



/*
 * Setup volume scan state for a fragment with complete FDW context
 */
static ForeignScanState* setupVolumeScan(agentcli_cJSON *fragment,
                                        char *volumeServer,
                                        char *volumeTable,
                                        char *tableName)
{
    /* Extract table information from fragment if available */
    RangeVar *rv = makeRangeVar(NULL, (char*)tableName, -1);
    Oid relationOid = RangeVarGetRelid(rv, NoLock, false);

    /* Open the relation */
    Relation rel = table_open(relationOid, AccessShareLock);
    TupleDesc tupdesc = RelationGetDescr(rel);

    ForeignScanState * result = iceberg_build_fdw_context(tupdesc, NIL, volumeServer, volumeTable, fragment);

    table_close(rel, AccessShareLock);

    return result;
}

/*
 * Read operation handler - returns multiple rows
 */
static Datum iceberg_toolkit_read_operation(PG_FUNCTION_ARGS)
{
    FuncCallContext *funcctx = SRF_FIRSTCALL_INIT();

    /* Parse read-specific arguments */
    char *nameSpace = text_to_cstring(PG_GETARG_TEXT_PP(1));
    char *tableName = text_to_cstring(PG_GETARG_TEXT_PP(2));
    int rowLimit = PG_GETARG_INT32(3);
    char *catalogServer = text_to_cstring(PG_GETARG_TEXT_PP(4));
    char *catalogTable = text_to_cstring(PG_GETARG_TEXT_PP(5));
    char *volumeServer = text_to_cstring(PG_GETARG_TEXT_PP(6));
    char *volumeTable = text_to_cstring(PG_GETARG_TEXT_PP(7));

    /* Initialize scan state */
    IcebergToolkitScanState *scanState = initIcebergScan(
        ICEBERG_GET_FRAGMENT, nameSpace, tableName, rowLimit,
        catalogServer, catalogTable, volumeServer, volumeTable);

    funcctx->user_fctx = scanState;

    return (Datum) 0; /* Will be handled by continuation calls */
}

/*
 * Append operation handler - returns single result
 */
static Datum iceberg_toolkit_append_operation(PG_FUNCTION_ARGS)
{
    /* Parse append-specific arguments */
    char *nameSpace = text_to_cstring(PG_GETARG_TEXT_PP(1));
    char *tableName = text_to_cstring(PG_GETARG_TEXT_PP(2));
    char *catalogServer = text_to_cstring(PG_GETARG_TEXT_PP(4));
    char *catalogTable = text_to_cstring(PG_GETARG_TEXT_PP(5));
    char *volumeServer = text_to_cstring(PG_GETARG_TEXT_PP(6));
    char *volumeTable = text_to_cstring(PG_GETARG_TEXT_PP(7));

    StringInfoData result;
    initStringInfo(&result);

    /* Initialize scan state for metadata location */
    IcebergToolkitScanState *scanState = initIcebergScan(
        ICEBERG_LOAD_TABLE, nameSpace, tableName, 1000,
        catalogServer, catalogTable, volumeServer, volumeTable);

    /* Execute foreign insert */
    const char* metadata = exec_foreign_insert(
        nameSpace, tableName, catalogServer, catalogTable,
        volumeServer, volumeTable, scanState->location);

    if (metadata == NULL) {
        elog(ERROR, "get empty iceberg metadata");
    }

    /* Execute append operation through catalog FDW */
    IcebergCatalogFdwState *catalogState = iceberg_execute_catalog_operation(
        ICEBERG_APPEND, nameSpace, tableName,
        catalogServer, catalogTable, volumeServer, volumeTable, metadata);

    if (catalogState->lastStatus == ICEBERG_SUCCESS) {
        iceberg_format_success_response(&result, DATALAKEFDW_ICEBERG_OP_APPEND, nameSpace, tableName, NULL);
    } else {
        iceberg_format_error_response(&result, DATALAKEFDW_ICEBERG_OP_APPEND, nameSpace, tableName, NULL);
    }

    /* Return single result */
    Datum values[1];
    bool nulls[1] = {false};
    values[0] = CStringGetTextDatum(result.data);

    TupleDesc tupdesc = CreateTemplateTupleDesc(1);
    TupleDescInitEntry(tupdesc, 1, DATALAKEFDW_ICEBERG_KEY_RESULT, TEXTOID, -1, 0);
    BlessTupleDesc(tupdesc);

    HeapTuple tuple = heap_form_tuple(tupdesc, values, nulls);
    PG_RETURN_DATUM(HeapTupleGetDatum(tuple));
}

const char*
exec_foreign_insert(char *nameSpace, char *tableName,
                    char *catalogServer, char *catalogTable,
                    char *volumeServer, char *volumeTable, char* location)
{
    RangeVar *rv = makeRangeVar(NULL, tableName, -1);
    Oid relationOid = RangeVarGetRelid(rv, NoLock, false);
    Relation rel = table_open(relationOid, AccessShareLock);

    FdwRoutine *volumeFdwRoutine = iceberg_get_volume_fdw_routine();

    EState *estate = CreateExecutorState();
    ResultRelInfo *resultRelInfo = makeNode(ResultRelInfo);
    resultRelInfo->ri_RelationDesc = rel;

    ModifyTableState *mtstate = makeNode(ModifyTableState);
    mtstate->ps.state = estate;

    icebergVolumeScanState *fdwState = (icebergVolumeScanState*)palloc0(sizeof(icebergVolumeScanState));
    fdwState->iceTable.volumn_server_name = pstrdup(volumeServer);
    fdwState->iceTable.volumn_name = pstrdup(volumeTable);
    resultRelInfo->ri_FdwState = (void*)fdwState;

    List *fdw_private = list_make1(makeString(location));
    volumeFdwRoutine->BeginForeignModify(mtstate, resultRelInfo, fdw_private, 0, 0);

    //TODO: need insert real data
    volumeFdwRoutine->EndForeignModify(estate, resultRelInfo);

    return (const char*)resultRelInfo->ri_FdwState;
}

/*
 * Cleanup scan state
 */
static void cleanupIcebergScan(IcebergToolkitScanState *scanState)
{
    if (scanState) {
        if (scanState->volumeScanActive && scanState->volumeFdwRoutine) {
            scanState->volumeFdwRoutine->EndForeignScan(scanState->volumeScanState);
        }

        /* Clean up TupleTableSlot to release TupleDesc reference */
        if (scanState->volumeScanState && scanState->volumeScanState->ss.ss_ScanTupleSlot) {
            ExecDropSingleTupleTableSlot(scanState->volumeScanState->ss.ss_ScanTupleSlot);
        }

        if (scanState->fragments) {
            agentcli_cJSON_Delete(scanState->fragments);
        }

        if (scanState->scanContext) {
            MemoryContextDelete(scanState->scanContext);
        }
    }
}

/*
 * Build qual expressions from filter conditions
 */
static List*
iceberg_build_qual_expressions(List *filter_conditions, TupleDesc tupdesc)
{
    List *qual_list = NIL;

    ListCell *lc;
    foreach(lc, filter_conditions) {
        IcebergFilterCondition *cond = (IcebergFilterCondition*)lfirst(lc);

        /* Find column by name */
        int attno = -1;
        for (int i = 0; i < tupdesc->natts; i++) {
            Form_pg_attribute attr = TupleDescAttr(tupdesc, i);
            if (strcmp(NameStr(attr->attname), cond->column_name) == 0) {
                attno = i + 1;
                break;
            }
        }

        if (attno == -1)
            continue; /* Column not found */

        Form_pg_attribute attr = TupleDescAttr(tupdesc, attno - 1);

        /* Create Var for column */
        Var *var = makeNode(Var);
        var->varno = 1;
        var->varattno = attno;
        var->vartype = attr->atttypid;
        var->vartypmod = attr->atttypmod;
        var->varcollid = attr->attcollation;
        var->varlevelsup = 0;

        /* Create Const for value */
        Const *const_val = makeNode(Const);
        const_val->consttype = attr->atttypid;
        const_val->constlen = get_typlen(attr->atttypid);
        const_val->constbyval = get_typbyval(attr->atttypid);
        const_val->constisnull = false;

        /* Convert string value to proper type */
        Oid typinput;
        Oid typioparam;
        getTypeInputInfo(attr->atttypid, &typinput, &typioparam);
        const_val->constvalue = OidInputFunctionCall(typinput, cond->value_str, 
                                                    typioparam, attr->atttypmod);

        /* Create operator expression */
        OpExpr *opexpr = makeNode(OpExpr);

        /* Find operator OID using syscache lookup */
        Oid oproid = InvalidOid;
        HeapTuple opertup;

        /* Look up operator by name and operand types in pg_catalog */
        Oid pg_catalog_oid = get_namespace_oid("pg_catalog", false);
        opertup = SearchSysCache4(OPERNAMENSP,
                                 CStringGetDatum(cond->operator_name),
                                 ObjectIdGetDatum(attr->atttypid),
                                 ObjectIdGetDatum(attr->atttypid),
                                 ObjectIdGetDatum(pg_catalog_oid));

        if (HeapTupleIsValid(opertup)) {
            oproid = ((Form_pg_operator) GETSTRUCT(opertup))->oid;
            ReleaseSysCache(opertup);
        }

        if (!OidIsValid(oproid)) {
            continue; /* Skip invalid operators */
        }

        opexpr->opno = oproid;
        opexpr->opfuncid = get_opcode(oproid);
        opexpr->opresulttype = BOOLOID;
        opexpr->args = list_make2(var, const_val);

        qual_list = lappend(qual_list, opexpr);
    }

    return qual_list;
}

/*
 * Build minimal FDW context - only what ForeignScanState needs
 */
static ForeignScanState*
iceberg_build_fdw_context(TupleDesc tupdesc,
                          List *filter_conditions,
                          char *volumeServer,
                          char *volumeTable,
                          agentcli_cJSON *fragment)
{
    /* 1. Create ForeignScanState */
    ForeignScanState *scanState = makeNode(ForeignScanState);
    EState *estate = CreateExecutorState();
    scanState->ss.ps.state = estate;

    /* 2. Create minimal ForeignScan plan */
    ForeignScan *fsplan = makeNode(ForeignScan);
    fsplan->scan.plan.type = T_ForeignScan;
    fsplan->scan.scanrelid = 1;
    fsplan->fs_server = InvalidOid;
    fsplan->fdw_exprs = NIL;
    fsplan->fdw_recheck_quals = NIL;
    fsplan->fs_relids = NULL;

    /* Create fdw_private list with required data for volume FDW */
    List *fdw_private = NIL;

    /* Add retrieved_attrs - list of column numbers to read */
    List *retrieved_attrs = NIL;
    if (tupdesc) {
        for (int i = 0; i < tupdesc->natts; i++) {
            Form_pg_attribute attr = TupleDescAttr(tupdesc, i);
            if (!attr->attisdropped) {
                retrieved_attrs = lappend_int(retrieved_attrs, i + 1);
            }
        }
    }
    fdw_private = lappend(fdw_private, retrieved_attrs);

    /* Add other required fdw_private elements that volume FDW expects */
    fdw_private = lappend(fdw_private, NIL);  /* attrs_used */
    fdw_private = lappend(fdw_private, NIL);  /* other data */

    fsplan->fdw_private = fdw_private;
    fsplan->fdw_scan_tlist = NIL;

    /* Set basic cost estimates */
    fsplan->scan.plan.startup_cost = 10.0;
    fsplan->scan.plan.total_cost = 100.0;
    fsplan->scan.plan.plan_rows = 1000;
    fsplan->scan.plan.plan_width = 32;

    /* Build qual expressions if we have filter conditions */
    if (filter_conditions && tupdesc) {
        fsplan->scan.plan.qual = iceberg_build_qual_expressions(filter_conditions, tupdesc);
    } else {
        fsplan->scan.plan.qual = NIL;
    }

    /* Build target list if we have tupdesc */
    if (tupdesc) {
        List *targetlist = NIL;
        for (int i = 0; i < tupdesc->natts; i++) {
            Form_pg_attribute attr = TupleDescAttr(tupdesc, i);

            if (attr->attisdropped)
                continue;

            Var *var = makeNode(Var);
            var->varno = 1;
            var->varattno = i + 1;
            var->vartype = attr->atttypid;
            var->vartypmod = attr->atttypmod;
            var->varcollid = attr->attcollation;
            var->varlevelsup = 0;

            TargetEntry *tle = makeTargetEntry((Expr*)var, i + 1,
                                              pstrdup(NameStr(attr->attname)), false);
            targetlist = lappend(targetlist, tle);
        }
        fsplan->scan.plan.targetlist = targetlist;
    } else {
        fsplan->scan.plan.targetlist = NIL;
    }

    scanState->ss.ps.plan = (Plan*)fsplan;

    /* 3. Create tuple slot if we have tupdesc */
    if (tupdesc) {
        scanState->ss.ss_ScanTupleSlot = MakeTupleTableSlot(tupdesc, &TTSOpsHeapTuple);
    }

    /* 4. Setup FDW private data */
    icebergVolumeScanState *fdwState = (icebergVolumeScanState*)palloc0(sizeof(icebergVolumeScanState));

    /* Extract fragment information */
    if (fragment) {
        /* Use the entire fragment as response */
        char *fragment_str = agentcli_cJSON_Print(fragment);
        if (fragment_str && strcmp(fragment_str, DATALAKEFDW_ICEBERG_VALUE_NULL) != 0) {
            fdwState->iceTable.agentCliRespond = pstrdup(fragment_str);
        }
        free(fragment_str);

        agentcli_cJSON *filePath = agentcli_cJSON_GetObjectItem(fragment, DATALAKEFDW_ICEBERG_KEY_FILE_PATH);
        if (filePath && agentcli_cJSON_IsString(filePath)) {
            fdwState->iceTable.basePath = pstrdup(filePath->valuestring);
        }
    }

    fdwState->iceTable.volumn_server_name = pstrdup(volumeServer);
    fdwState->iceTable.volumn_name = pstrdup(volumeTable);

    scanState->fdw_state = (void*)fdwState;

    return scanState;
}

/*
 * update operation handler - returns single result
 */
const char*
exec_foreign_update(char *nameSpace, char *tableName,
                    char *catalogServer, char *catalogTable,
                    char *volumeServer, char *volumeTable, char* location)
{
    RangeVar *rv = makeRangeVar(NULL, tableName, -1);
    Oid relationOid = RangeVarGetRelid(rv, NoLock, false);
    Relation rel = table_open(relationOid, AccessShareLock);

    FdwRoutine *volumeFdwRoutine = iceberg_get_volume_fdw_routine();

    EState *estate = CreateExecutorState();
    ResultRelInfo *resultRelInfo = makeNode(ResultRelInfo);
    resultRelInfo->ri_RelationDesc = rel;

    ModifyTableState *mtstate = makeNode(ModifyTableState);
    mtstate->ps.state = estate;

    icebergVolumeScanState *fdwState = (icebergVolumeScanState*)palloc0(sizeof(icebergVolumeScanState));
    fdwState->iceTable.volumn_server_name = pstrdup(volumeServer);
    fdwState->iceTable.volumn_name = pstrdup(volumeTable);
    resultRelInfo->ri_FdwState = (void*)fdwState;

    List *fdw_private = list_make1(makeString(location));

    int supportUpdate = volumeFdwRoutine->IsForeignRelUpdatable(rel);
    if (supportUpdate != 1) {
        elog(ERROR, "volume fdw toolkit already support update, but return not support");
    }

    volumeFdwRoutine->BeginForeignModify(mtstate, resultRelInfo, fdw_private, 0, 0);

    volumeFdwRoutine->EndForeignModify(estate, resultRelInfo);

    return (const char*)resultRelInfo->ri_FdwState;
}

static Datum iceberg_toolkit_update_operation(PG_FUNCTION_ARGS)
{
    /* Parse update-specific arguments */
    char *nameSpace = text_to_cstring(PG_GETARG_TEXT_PP(1));
    char *tableName = text_to_cstring(PG_GETARG_TEXT_PP(2));
    char *catalogServer = text_to_cstring(PG_GETARG_TEXT_PP(4));
    char *catalogTable = text_to_cstring(PG_GETARG_TEXT_PP(5));
    char *volumeServer = text_to_cstring(PG_GETARG_TEXT_PP(6));
    char *volumeTable = text_to_cstring(PG_GETARG_TEXT_PP(7));

    StringInfoData result;
    initStringInfo(&result);

    /* Initialize scan state for metadata location */
    IcebergToolkitScanState *scanState = initIcebergScan(
        ICEBERG_LOAD_TABLE, nameSpace, tableName, 1000,
        catalogServer, catalogTable, volumeServer, volumeTable);

    /* Execute foreign update */
    const char* metadata = exec_foreign_update(
        nameSpace, tableName, catalogServer, catalogTable,
        volumeServer, volumeTable, scanState->location);

    if (metadata == NULL) {
        elog(ERROR, "get empty iceberg metadata");
    }

    /* Execute update operation through catalog FDW */
    IcebergCatalogFdwState *catalogState = iceberg_execute_catalog_operation(
        ICEBERG_UPDATE, nameSpace, tableName,
        catalogServer, catalogTable, volumeServer, volumeTable, metadata);

    if (catalogState->lastStatus == ICEBERG_SUCCESS) {
        iceberg_format_success_response(&result, DATALAKEFDW_ICEBERG_OP_UPDATE, nameSpace, tableName, NULL);
    } else {
        iceberg_format_error_response(&result, DATALAKEFDW_ICEBERG_OP_UPDATE, nameSpace, tableName, NULL);
    }

    /* Return single result */
    Datum values[1];
    bool nulls[1] = {false};
    values[0] = CStringGetTextDatum(result.data);

    TupleDesc tupdesc = CreateTemplateTupleDesc(1);
    TupleDescInitEntry(tupdesc, 1, DATALAKEFDW_ICEBERG_KEY_RESULT, TEXTOID, -1, 0);
    BlessTupleDesc(tupdesc);

    HeapTuple tuple = heap_form_tuple(tupdesc, values, nulls);
    PG_RETURN_DATUM(HeapTupleGetDatum(tuple));
}