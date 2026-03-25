/*
 * datalake_fdw.c
 *		  Foreign-data wrapper for dataLake (Platform Extension Framework)
 *
 */


#include "datalake_def.h"
#include "datalake_option.h"
#include "datalake_fragment.h"
#include "common/fileSystemWrapper.h"
#include "common/partition_selector.h"
#include "common/random_segment.h"
#include "common/grammar_convert.h"
#include "src/dlproxy/datalake.h"

#include "postgres.h"

#include "access/formatter.h"
#include "access/reloptions.h"
#include "access/table.h"
#include "access/detoast.h"
#include "tcop/tcopprot.h"
#include "cdb/cdbsreh.h"
#include "cdb/cdbvars.h"
#include "cdb/cdbsrlz.h"
#include "cdb/cdbdisp.h"
#include "commands/copy.h"
#if (PG_VERSION_NUM >= 140000)
#include "commands/copyfrom_internal.h"
#include "commands/copyto_internal.h"
#endif
#include "commands/defrem.h"
#include "commands/explain.h"
#include "commands/vacuum.h"
#include "executor/spi.h"
#include "executor/tstoreReceiver.h"
#include "funcapi.h"
#include "foreign/fdwapi.h"
#include "foreign/foreign.h"
#include "nodes/pg_list.h"
#include "nodes/makefuncs.h"
#include "optimizer/optimizer.h"
#include "optimizer/paths.h"
#include "optimizer/pathnode.h"
#include "optimizer/planmain.h"
#include "optimizer/restrictinfo.h"
#include "optimizer/cost.h"
#include "optimizer/appendinfo.h"
#include "parser/parsetree.h"
#include "parser/parse_relation.h"
#if (PG_VERSION_NUM >= 140000)
#include "utils/backend_progress.h"
#endif
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/sampling.h"
#include "utils/typcache.h"
#include "utils/acl.h"
#include "tcop/utility.h"
#include "src/common/fileMetadata.h"
#include "dlproxy/protocol.h"
#include "cdb/cdbdispatchresult.h"
#include "src/provider/iceberg/iceberg_file_index.h"


PG_MODULE_MAGIC;

#define DATALAKE_SEGMENT_ID                 GpIdentity.segindex
#define DATALAKE_SEGMENT_COUNT              getgpsegmentCount()
#define EXEC_FLAG_VECTOR 0x8000

extern Datum datalake_fdw_handler(PG_FUNCTION_ARGS);

extern Bitmapset **acquire_func_colLargeRowIndexes;
extern double *acquire_func_colNDVBySeg;

void _PG_init(void);

/*
 * SQL functions
 */
PG_FUNCTION_INFO_V1(datalake_fdw_handler);
PG_FUNCTION_INFO_V1(datalake_acquire_sample_rows);

/*
 * FDW functions declarations
 */
static void dataLakeGetForeignRelSize(PlannerInfo *root, RelOptInfo *baserel, Oid foreigntableid);

#if PG_VERSION_NUM >= 90500
static ForeignScan *
dataLakeGetForeignPlan(PlannerInfo *root,
				  RelOptInfo *baserel,
				  Oid foreigntableid,
				  ForeignPath *best_path,
				  List *tlist,
				  List *scan_clauses,
				  Plan *outer_plan);
#else
static ForeignScan *
dataLakeGetForeignPlan(PlannerInfo *root,
				  RelOptInfo *baserel,
				  Oid foreigntableid,
				  ForeignPath *best_path,
				  List *tlist,	/* target list */
				  List *scan_clauses);
#endif

static void dataLakeGetForeignPaths(PlannerInfo *root, RelOptInfo *baserel, Oid foreigntableid);

static void dataLakeExplainForeignScan(ForeignScanState *node, ExplainState *es);

static void dataLakeBeginForeignScan(ForeignScanState *node, int eflags);

static TupleTableSlot *dataLakeIterateForeignScan(ForeignScanState *node);

static void dataLakeReScanForeignScan(ForeignScanState *node);

static void dataLakeEndForeignScan(ForeignScanState *node);

static void dataLakeAddForeignUpdateTargets(PlannerInfo *root,
											Index rtindex,
											RangeTblEntry *target_rte,
											Relation target_relation);

/* Foreign updates */
static List *dataLakePlanForeignModify(PlannerInfo *root,
									ModifyTable *plan,
									Index resultRelation,
									int subplan_index);

static void dataLakeBeginForeignModify(ModifyTableState *mtstate, ResultRelInfo *resultRelInfo, List *fdw_private, int subplan_index, int eflags);

static TupleTableSlot *dataLakeExecForeignInsert(EState *estate, ResultRelInfo *resultRelInfo, TupleTableSlot *slot, TupleTableSlot *planSlot);

static TupleTableSlot *dataLakeExecForeignUpdate(EState *estate, ResultRelInfo *rinfo, TupleTableSlot *slot, TupleTableSlot *planSlot);

static TupleTableSlot *dataLakeExecForeignDelete (EState *estate,
												  ResultRelInfo *rinfo,
												  TupleTableSlot *slot,
												  TupleTableSlot *planSlot);

static void dataLakeEndForeignModify(EState *estate, ResultRelInfo *resultRelInfo);

static int dataLakeIsForeignRelUpdatable(Relation rel);

static bool dataLakeIsForeignScanParallelSafe(PlannerInfo *root, RelOptInfo *rel,
							  RangeTblEntry *rte);

static int	dataLakeAcquireSampleRowsFunc(Relation relation, int elevel,
										  HeapTuple *rows, int targRows,
										  double *totalRows,
										  double *totalDeadRows);
static bool dataLakeAnalyzeForeignTable(Relation relation,
										AcquireSampleRowsFunc *func,
										BlockNumber *totalPages);
static void costDataLakeScan(ForeignPath *path, PlannerInfo *root,
				 RelOptInfo *baserel, ParamPathInfo *param_info);

/*
 * Helper functions
 */
static void InitCopyState(ForeignScanState *node, dataLakeFdwScanState *sstate);

int CopyRead(void *outbuf, int minlen, int maxlen, void *extra);

void initScanStatue(ForeignScanState *node, dataLakeFdwScanState *dataLakesstate);

void iterateScanStatus(ForeignScanState *node, dataLakeFdwScanState *dataLakesstate);

void iterateRecordBatch(dataLakeFdwScanState *dataLakesstate, VirtualTupleTableSlot *vslot);

void endScanStatus(dataLakeFdwScanState *dataLakesstate);

void freeFdwPrivateList(List *fdw_private);

void freeFdwPrivatePartitionList(List *fdw_private);

void freeFdwPrivate(dataLakeFdwScanState *sstate, ForeignScan *foreignScan);

static void InitParseStateTo(dataLakeFdwScanState *dataLakesstate, CopyToState cstate);

static void initModify(ModifyTableState *state, ResultRelInfo *resultRelInfo);

static void initCopyStateForModify(ResultRelInfo *resultRelInfo);

static void insertModify(ResultRelInfo *resultRelInfo, TupleTableSlot *slot);

static void endModify(ResultRelInfo *resultRelInfo);

static void prepareIcebergDeleteJunkSlot(ResultRelInfo *rinfo, TupleTableSlot *planSlot,
										 const char *opName);

/* Callback function to clean up global fileIndexMap on memory context reset */
static void fileIndexMapCallback(void *arg);

/* Callback function to clean up FDW_ResultMetaList on memory context reset */
static void resultMetaListCallback(void *arg);

bool hasZeorSelectedPartition(dataLakeFdwScanState *dataLakesstate);

void CsvTextErrorCallback(dataLakeFdwScanState *dataLakesstate);

void DatalakeErrorCallback(void *arg);

List* buildAttnameList(dataLakeFdwScanState *sstate);

#if (PG_VERSION_NUM < 140000)
static CopyState
BeginCopyTo(Relation forrel, List *options);
#else
static CopyToState
BeginCopyToModify(Relation forrel, List *options);
#endif

#if (PG_VERSION_NUM >= 140000)
static void EndCopyScan(CopyFromState cstate);
static void EndCopyModify(CopyToState cstate);
#endif

static void datalake_ProcessUtility(PlannedStmt *pstmt,
									const char *queryString,
									bool readOnlyTree,
									ProcessUtilityContext context,
									ParamListInfo params,
									QueryEnvironment *queryEnv,
									DestReceiver *dest,
									QueryCompletion *qc);

ProcessUtility_hook_type datalake_prev_ProcessUtility = NULL;
ProcessDispatchResult_hook_type datalake_prev_ProcessDispatchResult = NULL;

/*
 * Workspace for analyzing a foreign table.
 */
typedef struct DataLakeAnalyzeState
{
	/* collected sample rows */
	HeapTuple  *rows;			/* array of size targrows */
	int			targrows;		/* target # of sample rows */
	int			numrows;		/* # of sample rows collected */

	/* for random sampling */
	double		samplerows;		/* # of rows fetched */
	double		rowstoskip;		/* # of rows to skip before next sample */
	ReservoirStateData rstate;	/* state for reservoir sampling */

	/* working memory contexts */
	MemoryContext anl_cxt;		/* context for per-analyze lifespan data */
	MemoryContext temp_cxt;		/* context for per-tuple temporary data */
} DataLakeAnalyzeState;

static bool disableFilterPushdown;
bool disableCacheFile;
int icebergPostionDeletesThreshold;
int hudiLogMergerThreshold;
double hudiLogSizeScaleFactor;
int external_table_limit_segment_num;
bool enable_list_in_master;
bool enable_get_block_location;
bool enable_iceberg_fragment_cache;

void
_PG_init(void)
{
	if (!process_shared_preload_libraries_in_progress)
		return;

	DefineCustomBoolVariable("datalake.disable_filter_pushdown",
								"Enable passing of query constraints to datalake extension",
								NULL,
								&disableFilterPushdown,
								false,
								PGC_USERSET,
								0,
								NULL,
								NULL,
								NULL);

	DefineCustomBoolVariable("datalake.disable_cache_file",
								"Enable cache files",
								NULL,
								&disableCacheFile,
								false,
								PGC_USERSET,
								0,
								NULL,
								NULL,
								NULL);

	DefineCustomBoolVariable("datalake.external_table_debug",
							"If the value is true, datalake foreign table to turn debug on.",
							NULL,
							&external_table_debug,
							false,
							PGC_USERSET,
							GUC_NO_SHOW_ALL | GUC_NOT_IN_SAMPLE,
							NULL,
							NULL,
							NULL);

	DefineCustomBoolVariable("datalake.external_table_new_text",
							"If the value is true, datalake foreign table use the new logic parse text.",
							NULL,
							&external_table_new_text,
							false,
							PGC_USERSET,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable("datalake.iceberg_postion_deletes_threshold",
							"default iceberg position delete file threshold",
							NULL,
							&icebergPostionDeletesThreshold,
							100000,
							100000,
							10000000,
							PGC_USERSET,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable("datalake.hudi_log_merger_threshold",
							"The log size threshold in MB, for merging log records",
							NULL,
							&hudiLogMergerThreshold,
							512,
							128,
							10240,
							PGC_USERSET,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomRealVariable("datalake.hudi_log_scale_factor",
							"The calculation factor used for determining the size of temporary file",
							NULL,
							&hudiLogSizeScaleFactor,
							1.3,
							1,
							10,
							PGC_USERSET,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable("datalake.external_table_limit_segment_num",
							"Limit the number of segments executed datalake foreign table.",
							NULL,
							&external_table_limit_segment_num,
							0,
							0,
							INT_MAX,
							PGC_USERSET,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomBoolVariable("datalake.external_table_ignore_hidden_file",
							"If the value is true, datalake foreign table ignore read hidden file or directory.",
							NULL,
							&external_table_ignore_hidden_file,
							false,
							PGC_USERSET,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomBoolVariable("datalake.enable_set_hdfs_user",
							"If the value is true, set the user for writing to HDFS based on the users in the user mapping.",
							NULL,
							&enable_set_hdfs_user,
							true,
							PGC_USERSET,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomBoolVariable("datalake.enable_list_in_master",
							"If the guc is set to true, the list directory operation will be executed only by the master."
							"Note: This guc is only used for hdfs list directory operation.",
							NULL,
							&enable_list_in_master,
							true,
							PGC_USERSET,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomBoolVariable("datalake.enable_get_block_location",
							"If the guc is set to true, the list directory operation will get hdfs block location."
							"Note: This guc is only used for hdfs list directory operation.",
							NULL,
							&enable_get_block_location,
							false,
							PGC_USERSET,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomBoolVariable("datalake.enable_iceberg_fragment_cache",
							"Cache Iceberg fragment lists using snapshot ID validation.",
							NULL,
							&enable_iceberg_fragment_cache,
							true,
							PGC_USERSET,
							0,
							NULL,
							NULL,
							NULL);

	datalake_prev_ProcessUtility = ProcessUtility_hook;
	ProcessUtility_hook = datalake_ProcessUtility;
	datalake_prev_ProcessDispatchResult = ProcessDispatchResult_hook;
	ProcessDispatchResult_hook = FDW_RecvMeta;
	FDWRecvProtocol = RecvMetaMethod;
}

static void
datalake_ProcessUtility(PlannedStmt *pstmt,
						const char *queryString,
						bool readOnlyTree,
						ProcessUtilityContext context,
						ParamListInfo params,
						QueryEnvironment *queryEnv,
						DestReceiver *dest,
						QueryCompletion *qc)
{
	switch (nodeTag(pstmt->utilityStmt))
	{
		case T_CreateExternalStmt:
		{
			CreateExternalStmt *createExtStmt;
			CreateForeignTableStmt *foreignStmt = NULL;

			createExtStmt = (CreateExternalStmt *) pstmt->utilityStmt;
			foreignStmt = ConvertExternalTableStmt(createExtStmt);
			if (foreignStmt)
				pstmt->utilityStmt = (Node *) foreignStmt;
			break;
		}
		default:
			break;
	}

	if (datalake_prev_ProcessUtility)
		(*datalake_prev_ProcessUtility) (pstmt, queryString, readOnlyTree,
										 context, params, queryEnv,
										 dest, qc);
	else
		standard_ProcessUtility(pstmt, queryString, readOnlyTree,
								context, params, queryEnv,
								dest, qc);
}

/*
 * Foreign-data wrapper handler functions:
 * returns a struct with pointers to the
 * datalake_fdw callback routines.
 */
Datum
datalake_fdw_handler(PG_FUNCTION_ARGS)
{
	FdwRoutine *fdw_routine = makeNode(FdwRoutine);

	/*
	 * foreign table scan support
	 */

	/* master - only */
	fdw_routine->GetForeignRelSize = dataLakeGetForeignRelSize;
	fdw_routine->GetForeignPaths = dataLakeGetForeignPaths;
	fdw_routine->GetForeignPlan = dataLakeGetForeignPlan;

	fdw_routine->ExplainForeignScan = dataLakeExplainForeignScan;

	/* segment - only when mpp_execute = segments */
	fdw_routine->BeginForeignScan = dataLakeBeginForeignScan;
	fdw_routine->IterateForeignScan = dataLakeIterateForeignScan;
	fdw_routine->ReScanForeignScan = dataLakeReScanForeignScan;
	fdw_routine->EndForeignScan = dataLakeEndForeignScan;

	/*
	 * foreign table insert support
	 */

	/*
	 * AddForeignUpdateTargets set to NULL, no extra target expressions are
	 * added
	 */
	// fdw_routine->AddForeignUpdateTargets = icebergAddForeignUpdateTargets;
	fdw_routine->AddForeignUpdateTargets = dataLakeAddForeignUpdateTargets;

	/*
	 * PlanForeignModify set to NULL, no additional plan-time actions are
	 * taken
	 */
	fdw_routine->PlanForeignModify = dataLakePlanForeignModify;
	fdw_routine->BeginForeignModify = dataLakeBeginForeignModify;
	fdw_routine->ExecForeignInsert = dataLakeExecForeignInsert;

	/*
	 * ExecForeignUpdate and ExecForeignDelete set to NULL since updates and
	 * deletes are not supported
	 */
	fdw_routine->ExecForeignUpdate = dataLakeExecForeignUpdate;
	fdw_routine->ExecForeignDelete = dataLakeExecForeignDelete;
	fdw_routine->EndForeignModify = dataLakeEndForeignModify;
	fdw_routine->IsForeignRelUpdatable = dataLakeIsForeignRelUpdatable;
	fdw_routine->IsForeignScanParallelSafe = dataLakeIsForeignScanParallelSafe;


	/* Support functions for ANALYZE */
	fdw_routine->AnalyzeForeignTable = dataLakeAnalyzeForeignTable;

	PG_RETURN_POINTER(fdw_routine);
}



static void
extract_used_attributes(RelOptInfo *baserel)
{
    dataLakeFdwPlanState *fdw_private = (dataLakeFdwPlanState *) baserel->fdw_private;
    ListCell *lc;

    pull_varattnos((Node *) baserel->reltarget->exprs,
                   baserel->relid,
                   &fdw_private->attrs_used);

    foreach(lc, baserel->baserestrictinfo)
    {
        RestrictInfo *rinfo = (RestrictInfo *) lfirst(lc);

        pull_varattnos((Node *) rinfo->clause,
                       baserel->relid,
                       &fdw_private->attrs_used);
    }

    if (bms_is_empty(fdw_private->attrs_used))
    {
        bms_free(fdw_private->attrs_used);
        fdw_private->attrs_used = bms_make_singleton(1 - FirstLowInvalidHeapAttributeNumber);
    }
}

/*
 * datalakeBuildRetrievedAttrsList
 *		Build an explicit list of attribute numbers to retrieve for a scan.
 *
 * Unlike datalakeDeparseTargetList() in deparse.c (which returns NIL for
 * whole-row references, relying on the caller to interpret NIL as "all
 * columns"), this function always returns an explicit list of all non-dropped
 * column numbers.  This is needed by the FDW scan path, which passes the
 * list through fdw_private and expects it to be self-contained.
 */
static void
datalakeBuildRetrievedAttrsList(Relation rel, Bitmapset *attrs_used, List **retrieved_attrs)
{
	TupleDesc	tupdesc = RelationGetDescr(rel);
	bool		have_wholerow;
	int			i;

	*retrieved_attrs = NIL;

	/* If there's a whole-row reference, we'll need all the columns. */
	have_wholerow = bms_is_member(0 - FirstLowInvalidHeapAttributeNumber, attrs_used);

	for (i = 1; i <= tupdesc->natts; i++)
	{
		Form_pg_attribute attr = TupleDescAttr(tupdesc, i - 1);

		/* Ignore dropped attributes. */
		if (attr->attisdropped)
			continue;

		if (have_wholerow ||
			bms_is_member(i - FirstLowInvalidHeapAttributeNumber, attrs_used))
		{
			*retrieved_attrs = lappend_int(*retrieved_attrs, i);
		}
	}
}

/*
 * GetForeignRelSize
 *		set relation size estimates for a foreign table
 */
static void
dataLakeGetForeignRelSize(PlannerInfo *root, RelOptInfo *baserel, Oid foreigntableid)
{
	dataLakeFdwPlanState* fdw_private = (dataLakeFdwPlanState *) palloc0(sizeof(dataLakeFdwPlanState));
	baserel->fdw_private = fdw_private;

	/*
	 * For Iceberg tables, fetch actual row count and data size from Iceberg
	 * snapshot metadata to provide accurate estimates to the optimizer.
	 */
	if (Gp_role == GP_ROLE_DISPATCH)
	{
		dataLakeOptions *options = datalakeGetOptions(foreigntableid);

		if (FORMAT_IS_ICEBERG(options->format))
		{
			PG_TRY();
			{
				IcebergTableStatistics *stats =
					datalakeGetTableStatistics(foreigntableid, options);

				if (stats != NULL)
				{
					if (stats->recordCount > 0)
					{
						baserel->tuples = (double) stats->recordCount;

						/*
						 * Also update the relcache entry so that ORCA's
						 * cdb_estimate_partitioned_numtuples() sees the
						 * accurate row count.  Without this, ORCA computes
						 * ndistinct = stadistinct_ratio * pg_class.reltuples
						 * using the stale pg_class value, leading to massive
						 * underestimates that cause catastrophic Broadcast
						 * plans (e.g. TPC-H Q3: 157s vs 10s).
						 *
						 * This is an in-memory-only update to the relcache;
						 * it does NOT modify pg_class on disk.
						 */
						Relation rel = RelationIdGetRelation(foreigntableid);
						if (RelationIsValid(rel))
						{
							if (stats->recordCount > 0)
								rel->rd_rel->reltuples =
									(float4) stats->recordCount;
							if (stats->bytesInDataFile > 0)
								rel->rd_rel->relpages =
									(int32) ((stats->bytesInDataFile + (BLCKSZ - 1)) / BLCKSZ);
							RelationClose(rel);
						}
					}

					if (stats->bytesInDataFile > 0)
					{
						baserel->pages = (BlockNumber)
							((stats->bytesInDataFile + (BLCKSZ - 1)) / BLCKSZ);
					}

					pfree(stats);
				}
			}
			PG_CATCH();
			{
				ErrorData *edata = CopyErrorData();
				if (edata->sqlerrcode == ERRCODE_QUERY_CANCELED ||
					edata->sqlerrcode == ERRCODE_ADMIN_SHUTDOWN)
				{
					FreeErrorData(edata);
					PG_RE_THROW();
				}
				FlushErrorState();
				FreeErrorData(edata);
				elog(DEBUG1, "datalake_fdw: failed to fetch Iceberg statistics "
					 "for relation %u, using default estimates", foreigntableid);
			}
			PG_END_TRY();
		}
	}

	set_baserel_size_estimates(root, baserel);
}

/*
 * dataLakeGetForeignPaths
 */
static void
dataLakeGetForeignPaths(PlannerInfo *root,
				   RelOptInfo *baserel,
				   Oid foreigntableid)
{
	ForeignPath *path = NULL;
	int			total_cost = 50000;
	Relation	rel;
	dataLakeFdwPlanState *fdw_private = (dataLakeFdwPlanState*)baserel->fdw_private;

	RangeTblEntry *rte = planner_rt_fetch(baserel->relid, root);

	rel = table_open(rte->relid, NoLock);

	/* Collect used attributes to reduce number of read columns during scan */
	extract_used_attributes(baserel);

	datalakeBuildRetrievedAttrsList(rel, fdw_private->attrs_used, &fdw_private->retrieved_attrs);

	path = create_foreignscan_path(root, baserel,
#if PG_VERSION_NUM >= 90600
								   NULL,	/* default pathtarget */
#endif
								   baserel->rows,
								   50000,
								   total_cost,
								   NIL, /* no pathkeys */
								   NULL,	/* no outer rel either */
#if PG_VERSION_NUM >= 90500
								   NULL,	/* no extra plan */
#endif
								   NULL);
	heap_close(rel, NoLock);
	/*
	 * Create a ForeignPath node and add it as only possible path.
	 */

	costDataLakeScan(path, root, baserel, path->path.param_info);

	add_path(baserel, (Path *) path, root);
	set_cheapest(baserel);
}

/*
 * GetForeignPlan
 *		create a ForeignScan plan node
 */
#if PG_VERSION_NUM >= 90500
static ForeignScan *
dataLakeGetForeignPlan(PlannerInfo *root,
				  RelOptInfo *baserel,
				  Oid foreigntableid,
				  ForeignPath *best_path,
				  List *tlist,
				  List *scan_clauses,
				  Plan *outer_plan)
#else
static ForeignScan *
dataLakeGetForeignPlan(PlannerInfo *root,
				  RelOptInfo *baserel,
				  Oid foreigntableid,
				  ForeignPath *best_path,
				  List *tlist,	/* target list */
				  List *scan_clauses)
#endif
{
	dataLakeFdwPlanState *fdw_private = (dataLakeFdwPlanState*)baserel->fdw_private;

	Index		scan_relid = baserel->relid;


	scan_clauses = extract_actual_clauses(scan_clauses, false);

	List* private_lists = list_make2(makeString("scan"), fdw_private->retrieved_attrs);

	return make_foreignscan(
							tlist,
							scan_clauses,
							scan_relid,
							NIL,	/* no expressions to evaluate */
							private_lists
	#if PG_VERSION_NUM >= 90500
								,NIL
								,NIL
								,outer_plan
	#endif
	);
}


/*
 * dataLakeExplainForeignScan
 *		Produce extra output for EXPLAIN of a ForeignScan on a foreign table
 */
static void
dataLakeExplainForeignScan(ForeignScanState *node, ExplainState *es)
{
	elog(DEBUG5, "datalake_fdw: dataLakeExplainForeignScan starts on segment: %d", DATALAKE_SEGMENT_ID);

	/* TODO: make this a meaningful callback */

	elog(DEBUG5, "datalake_fdw: dataLakeExplainForeignScan ends on segment: %d", DATALAKE_SEGMENT_ID);
}

/*
 * BeginForeignScan
 *   called during executor startup. perform any initialization
 *   needed, but not start the actual scan.
 */
static void
dataLakeBeginForeignScan(ForeignScanState *node, int eflags)
{
	elog(DEBUG5, "datalake_fdw: dataLakeBeginForeignScan starts on segment: %d", DATALAKE_SEGMENT_ID);

	/*
	 * Do nothing in EXPLAIN (no ANALYZE) case.  node->fdw_state stays NULL.
	 */
	if (eflags & EXEC_FLAG_EXPLAIN_ONLY)
		return;

	dataLakeFdwScanState *dataLakesstate  	= (dataLakeFdwScanState *) palloc0(sizeof(dataLakeFdwScanState));
	dataLakesstate->scan_tupdesc			= CreateTupleDescCopy(node->ss.ps.scandesc);
	Oid			foreigntableid 				= RelationGetRelid(node->ss.ss_currentRelation);
	dataLakesstate->options 				= datalakeGetOptions(foreigntableid);
	dataLakesstate->rel						= node->ss.ss_currentRelation;
	List* fragmentData						= NIL;
	ForeignScan *foreignScan      			= (ForeignScan *) node->ss.ps.plan;
	int segmentcount						= getgpsegmentCount();
	List *selected_segments					= NIL;
	dataLakesstate->provider = NULL;
	if (gp_external_enable_filter_pushdown)
		dataLakesstate->quals = node->ss.ps.plan->qual;

	if (eflags & EXEC_FLAG_VECTOR)
	{
		dataLakesstate->options->vectorization = true;
		datalakeCheckValidRecordBatchOpt(dataLakesstate->options);
	}

	List *retrieved_attrs = (List *) list_nth(foreignScan->fdw_private, FdwScanPrivateRetrievedAttrs);
	/*
	 * When queried at all of the nodes. Need to go to object storage
	 * do list operation get file list. The file list is sent to the
	 * segment for scheduling
	 */
	if (Gp_role == GP_ROLE_DISPATCH)
	{
		fragmentData = datalakeGetExternalFragmentList(dataLakesstate->rel, dataLakesstate->quals, dataLakesstate->options, NULL);

		/*
		 * Save fragment list on coordinator before list_concat (which may
		 * be destructive).  BeginForeignModify will attach this to the
		 * ModifyTable plan's fdw_private for dispatch to writer QEs.
		 */
		if (FORMAT_IS_ICEBERG(dataLakesstate->options->format) && fragmentData != NIL)
			datalake_iceberg_all_fragments = list_copy(fragmentData);

		if (fragmentData != NIL)
		{
			foreignScan->fdw_private = list_concat(foreignScan->fdw_private, fragmentData);
		}
		/* master does not process any fragments */
		List *random_segments = datalakeSelectRandomSegments(segmentcount, external_table_limit_segment_num);
		/* put the random segments into the list */
		foreignScan->fdw_private = list_concat(foreignScan->fdw_private, random_segments);

		/* register resource context for gopher */
		dataLakesstate->gopher_handle_t = gopher_registe_resource_context(/*gp_is_writer*/false);
		node->fdw_state = (void*)dataLakesstate;
		return;
	}

	/* the last segmentcount elements are the selected random segments */
	int len = list_length(foreignScan->fdw_private);
	for (int i = segmentcount; i >= 1; i--)
	{
		int idx = intVal(list_nth(foreignScan->fdw_private, len - i));
		selected_segments = lappend_int(selected_segments, idx);
	}

	/* remove the last segmentcount elements */
	foreignScan->fdw_private = list_truncate(foreignScan->fdw_private, len - segmentcount);
	fragmentData = datalakeDeserializeExternalFragmentList(dataLakesstate->rel, dataLakesstate->quals, dataLakesstate->options, foreignScan->fdw_private);

	dataLakesstate->selected_segments = selected_segments;
	dataLakesstate->options->readFdw = true;
	dataLakesstate->provider = initProvider(dataLakesstate->options->format, DL_OP_READ, dataLakesstate->options->vectorization);
	dataLakesstate->rel = node->ss.ss_currentRelation;
	dataLakesstate->fragments = fragmentData;
	dataLakesstate->retrieved_attrs = retrieved_attrs;

	/*
	 * For Iceberg tables, save the full fragment list (containing ALL
	 * segments' files) so that BeginForeignModify can use it to build a
	 * globally consistent file index map.  Without this, each segment would
	 * independently number files starting from 0, and Redistribute Motion
	 * during UPDATE would send rows to segments whose local file IDs don't
	 * match the originating segment's encoding.
	 */
	if (FORMAT_IS_ICEBERG(dataLakesstate->options->format))
		datalake_iceberg_all_fragments = fragmentData;

	/*
	 * For Iceberg UPDATE/DELETE with cross-slice plans (e.g. Redistribute
	 * Motion between Foreign Scan and ModifyTable), the scan runs in a
	 * separate QE process from BeginForeignModify.  We need the file index
	 * map here so that:
	 *   (a) icebergEncodeTID encodes correct, globally consistent file IDs
	 *   (b) iterateScanStatus stores HeapTuples (not VirtualTuples), which
	 *       is required for system column access (gp_segment_id, ctid)
	 *
	 * Detect the UPDATE/DELETE context by checking if the plan's target list
	 * references ctid (added by dataLakeAddForeignUpdateTargets).
	 */
	if (FORMAT_IS_ICEBERG(dataLakesstate->options->format) &&
		datalake_iceberg_file_index_map == NULL &&
		foreignScan->fsSystemCol)
	{
		MemoryContextCallback *mcb;

		datalake_iceberg_file_index_map = icebergCreateFileIndexMap();
		if (datalake_iceberg_file_index_map == NULL)
			ereport(ERROR,
					(errcode(ERRCODE_OUT_OF_MEMORY),
					 errmsg("failed to create Iceberg file index map")));

		if (datalake_iceberg_all_fragments != NULL)
			icebergFileIndexMapPopulateFromAllFragments(
				datalake_iceberg_file_index_map,
				datalake_iceberg_all_fragments);

		mcb = MemoryContextAlloc(CurrentMemoryContext,
								  sizeof(MemoryContextCallback));
		mcb->func = fileIndexMapCallback;
		mcb->arg = NULL;
		MemoryContextRegisterResetCallback(CurrentMemoryContext, mcb);

		elog(DEBUG2, "datalake_fdw: Created Iceberg file index map in BeginForeignScan (cross-slice UPDATE/DELETE)");
	}

	if (hasZeorSelectedPartition(dataLakesstate))
	{
		node->fdw_state = (void*)dataLakesstate;
		return;
	}

	initScanStatue(node, dataLakesstate);

	node->fdw_state = (void*)dataLakesstate;

	elog(DEBUG5, "datalake_fdw: dataLakeBeginForeignScan ends on segment: %d", DATALAKE_SEGMENT_ID);
}

/*
 * error context callback for Datalake
 *
 * The argument for the error context must be CopyFromState.
 */
void CsvTextErrorCallback(dataLakeFdwScanState *dataLakesstate)
{
	CopyFromState cstate = (CopyFromState) dataLakesstate->cstate.cstate_scan;
	char		curlineno_str[32];
	char		filename[1024];

	snprintf(curlineno_str, sizeof(curlineno_str), UINT64_FORMAT,
			 cstate->cur_lineno);

	const char* name = getReadProviderFileName(dataLakesstate->provider);
	if (name != NULL)
	{
		snprintf(filename, sizeof(filename), "file %s,", name);
	}
	else
	{
		filename[0] = '\0';
	}

	if (cstate->opts.binary)
	{
		/* can't usefully display the data */
		if (cstate->cur_attname)
			errcontext("Foreign table  %s, %s line %s, column %s",
					   cstate->cur_relname, filename, curlineno_str,
					   cstate->cur_attname);
		else
			errcontext("Foreign table %s, %s line %s",
					   cstate->cur_relname, filename, curlineno_str);
	}
	else
	{
		if (cstate->cur_attname && cstate->cur_attval)
		{
			/* error is relevant to a particular column */
			char	   *attval;

			attval = limit_printout_length(cstate->cur_attval);
			errcontext("Foreign table %s, %s line %s, column %s: \"%s\"",
					   cstate->cur_relname, filename, curlineno_str,
					   cstate->cur_attname, attval);
			pfree(attval);
		}
		else if (cstate->cur_attname)
		{
			/* error is relevant to a particular column, value is NULL */
			errcontext("Foreign table %s, %s line %s, column %s: null input",
					   cstate->cur_relname, filename, curlineno_str,
					   cstate->cur_attname);
		}
		else
		{
			/*
			 * Error is relevant to a particular line.
			 *
			 * If line_buf still contains the correct line, print it.
			 */
			if (cstate->line_buf_valid)
			{
				char	   *lineval;

				lineval = limit_printout_length(cstate->line_buf.data);
				errcontext("Foreign table %s, %s line %s: \"%s\"",
						   cstate->cur_relname, filename, curlineno_str, lineval);
				pfree(lineval);
			}
			else
			{
				errcontext("Foreign table %s, %s line %s",
						   cstate->cur_relname, filename, curlineno_str);
			}
		}
	}
}

void DatalakeErrorCallback(void *arg)
{
	dataLakeFdwScanState *dataLakesstate = (dataLakeFdwScanState*)arg;
	if (FORMAT_IS_CSV(dataLakesstate->options->format) ||
		FORMAT_IS_TEXT(dataLakesstate->options->format))
	{
		CsvTextErrorCallback(dataLakesstate);
	}
}

/*
 * IterateForeignScan
 *		Retrieve next row from the result set, or clear tuple slot to indicate
 *		EOF.
 *   Fetch one row from the foreign source, returning it in a tuple table slot
 *    (the node's ScanTupleSlot should be used for this purpose).
 *  Return NULL if no more rows are available.
 */
static TupleTableSlot *
dataLakeIterateForeignScan(ForeignScanState *node)
{
	elog(DEBUG5, "datalake_fdw: dataLakeIterateForeignScan Executing on segment: %d", DATALAKE_SEGMENT_ID);
	dataLakeFdwScanState *dataLakesstate = (dataLakeFdwScanState*)node->fdw_state;

	if (dataLakesstate->options->hiveOption->partitiontable
	    && !dataLakesstate->options->hiveOption->hivePartitionConstraints)
	{
		TupleTableSlot *slot = node->ss.ss_ScanTupleSlot;
		ExecClearTuple(slot);
		return slot;
	}

	if (dataLakesstate->options->vectorization)
	{
		VirtualTupleTableSlot *vslot = (VirtualTupleTableSlot*)node->ss.ss_ScanTupleSlot;
		ExecClearTuple(&vslot->base);
		iterateRecordBatch(dataLakesstate, vslot);
		return (TupleTableSlot*)vslot;
	}
	else
	{
		TupleTableSlot *slot = node->ss.ss_ScanTupleSlot;
		ErrorContextCallback errcallback;
		if (FORMAT_IS_CSV(dataLakesstate->options->format) ||
			FORMAT_IS_TEXT(dataLakesstate->options->format) ||
			FORMAT_IS_CUSTOM(dataLakesstate->options->format))
		{
			/* Set up callback to identify error line number. */
			errcallback.callback = DatalakeErrorCallback;
			errcallback.arg = (void *) dataLakesstate;
			errcallback.previous = error_context_stack;
			error_context_stack = &errcallback;
		}

		/*
		* The protocol for loading a virtual tuple into a slot is first
		* ExecClearTuple, then fill the values/isnull arrays, then
		* ExecStoreVirtualTuple.  If we don't find another row in the file, we
		* just skip the last step, leaving the slot empty as required.
		*
		* We can pass ExprContext = NULL because we read all columns from the
		* file, so no need to evaluate default expressions.
		*/
		ExecClearTuple(slot);
		memset(slot->tts_isnull, 1, slot->tts_tupleDescriptor->natts * sizeof(bool));

		iterateScanStatus(node, dataLakesstate);

		if (FORMAT_IS_CSV(dataLakesstate->options->format) ||
			FORMAT_IS_TEXT(dataLakesstate->options->format) ||
			FORMAT_IS_CUSTOM(dataLakesstate->options->format))
		{
			/* Remove error callback. */
			error_context_stack = errcallback.previous;
		}
		return slot;
	}
}

/*
 * hasZeorSelectedPartition
 * If the hivePartitionConstraints condition is a null then
 * there is no partition key table.
 */
bool hasZeorSelectedPartition(dataLakeFdwScanState *dataLakesstate)
{
	if (dataLakesstate->options->hiveOption->partitiontable &&
		!dataLakesstate->options->hiveOption->hivePartitionConstraints)
	{
		return true;
	}
	return false;
}

/*
 * ReScanForeignScan
 *		Restart the scan from the beginning
 */
static void
dataLakeReScanForeignScan(ForeignScanState *node)
{
	elog(DEBUG5, "datalake_fdw: dataLakeReScanForeignScan starts on segment: %d", DATALAKE_SEGMENT_ID);
	dataLakeFdwScanState *dataLakesstate = (dataLakeFdwScanState*)node->fdw_state;
	if (FORMAT_IS_CUSTOM(dataLakesstate->options->format))
	{
		datalake_to_exttable_ReScanForeignScan(node);
	}
	endScanStatus(dataLakesstate);
	initScanStatue(node, dataLakesstate);
	elog(DEBUG5, "datalake_fdw: dataLakeReScanForeignScan ends on segment: %d", DATALAKE_SEGMENT_ID);
}

/*
 * EndForeignScan
 *		End the scan and release resources.
 */
static void
dataLakeEndForeignScan(ForeignScanState *node)
{
	elog(DEBUG5, "datalake_fdw: dataLakeEndForeignScan starts on segment: %d", DATALAKE_SEGMENT_ID);

	/* Do nothing in EXPLAIN (no ANALYZE) case. */
	if (node->fdw_state == NULL)
	{
		return;
	}

	ForeignScan *foreignScan = (ForeignScan *) node->ss.ps.plan;

	dataLakeFdwScanState *sstate = (dataLakeFdwScanState*)node->fdw_state;

	/* Release resources */
	if (foreignScan->fdw_private)
	{
		elog(DEBUG5, "Freeing fdw_private");
		freeFdwPrivate(sstate, foreignScan);
		foreignScan->fdw_private = NULL;
	}

	if (Gp_role == GP_ROLE_DISPATCH)
	{
		/* release resource context for gopher */
		cleanup_gopher_resource_context(sstate->gopher_handle_t);
		sstate->gopher_handle_t = NULL;
		datalake_iceberg_all_fragments = NULL;
		return;
	}

	if (FORMAT_IS_CUSTOM(sstate->options->format))
	{
		datalake_to_exttable_EndForeignScan(node);
	}

	endScanStatus(sstate);

	/*
	 * Clear the global fragment reference to prevent dangling pointers.
	 * The fragment data lives in the scan's memory context which is about
	 * to be freed.  If a subsequent query (e.g. UPDATE after INSERT) runs
	 * in the same backend, it must not use stale fragment pointers.
	 */
	datalake_iceberg_all_fragments = NULL;

	elog(DEBUG5, "datalake_fdw: dataLakeEndForeignScan ends on segment: %d", DATALAKE_SEGMENT_ID);
}

void dataLakeAddForeignUpdateTargets(PlannerInfo *root, Index rtindex, RangeTblEntry *target_rte, Relation target_relation)
{
	Oid			reloid;
	Oid			vartypeid;
	int32		type_mod;
	Oid			type_coll;
	Var			*var;

	reloid = RelationGetRelid(target_relation);
	get_atttypetypmodcoll(reloid, GpSegmentIdAttributeNumber, &vartypeid, &type_mod, &type_coll);
	var = makeVar(rtindex,
				GpSegmentIdAttributeNumber,
				vartypeid,
				type_mod,
				type_coll,
				0);
	add_row_identity_var(root, var, rtindex, "gp_segment_id");

	get_atttypetypmodcoll(reloid, SelfItemPointerAttributeNumber, &vartypeid, &type_mod, &type_coll);
	var = makeVar(rtindex,
				SelfItemPointerAttributeNumber,
				vartypeid,
				type_mod,
				type_coll,
				0);
	add_row_identity_var(root, var, rtindex, "ctid");
}

/*
 * dataLakePlanForeignModify
 * 		Generate file prefix
*/
static List *dataLakePlanForeignModify(PlannerInfo *root,
									   ModifyTable *plan,
									   Index resultRelation,
									   int subplan_index)
{
	RangeTblEntry *rte = planner_rt_fetch(resultRelation, root);
	
	char *filePrefix = datalakeGetExternalWriteLocation(rte->relid);
	return list_make1(makeString(filePrefix));
}


/*
 * dataLakeBeginForeignModify
 *		Begin an insert operation on a foreign table
 */
static void
dataLakeBeginForeignModify(ModifyTableState *mtstate,
					  ResultRelInfo *resultRelInfo,
					  List *fdw_private,
					  int subplan_index,
					  int eflags)
{
	elog(DEBUG5, "datalake_fdw: dataLakeBeginForeignModify starts on segment: %d", DATALAKE_SEGMENT_ID);

	if (eflags & EXEC_FLAG_EXPLAIN_ONLY)
		return;
	int			i;
	dataLakeFdwScanState *dataLakesstate  = (dataLakeFdwScanState *) palloc0(sizeof(dataLakeFdwScanState));
	dataLakesstate->modify_state = (dataLakeModifyState *) palloc0(sizeof(dataLakeModifyState));
	Relation	relation 			= resultRelInfo->ri_RelationDesc;
	dataLakesstate->options 		= datalakeGetOptions(RelationGetRelid(relation));
	dataLakesstate->rel				= relation;
	dataLakesstate->options->readFdw = false;
	resultRelInfo->ri_FdwState = dataLakesstate;
	dataLakesstate->modify_state->us_provider = NULL;
	dataLakesstate->modify_state->us_slot = NULL;
	dataLakesstate->cmd = mtstate->operation;

	if (Gp_role == GP_ROLE_DISPATCH)
	{
		MemoryContextCallback *mcb;

		FDW_ResultMetaList = NIL;

		/*
		 * For Iceberg UPDATE/DELETE, attach the global fragment list to
		 * fdwPrivLists so the writer QEs can populate the file index map.
		 * In cross-slice plans (e.g. Redistribute Motion between the
		 * ForeignScan and ModifyTable), the writer QE process doesn't run
		 * BeginForeignScan, so it has no fragment data.  We solve this by
		 * appending the fragments to the ModifyTable's fdw_private, which
		 * is serialized and dispatched to all writer QEs.
		 */
		if (datalake_iceberg_all_fragments != NULL &&
			(mtstate->operation == CMD_UPDATE || mtstate->operation == CMD_DELETE))
		{
			ModifyTable *mt = (ModifyTable *) mtstate->ps.plan;
			ListCell *cell = list_nth_cell(mt->fdwPrivLists, subplan_index);
			List *privList = (List *) lfirst(cell);

			/*
			 * Build a new list rather than mutating the cached plan node
			 * with list_concat.  This avoids double-append on prepared
			 * statement re-execution and ensures the appended ListCells
			 * live in the current executor memory context.
			 */
			List *newPrivList = list_copy(privList);
			newPrivList = list_concat(newPrivList,
									  list_copy(datalake_iceberg_all_fragments));
			lfirst(cell) = newPrivList;
		}

		mcb = MemoryContextAlloc(CurrentMemoryContext,
								  sizeof(MemoryContextCallback));
		mcb->func = resultMetaListCallback;
		mcb->arg = NULL;
		MemoryContextRegisterResetCallback(CurrentMemoryContext, mcb);

		return;
	}

	Value *val = lfirst(list_nth_cell(fdw_private, FdwModifyFileDir));
	dataLakesstate->fragments = lappend(dataLakesstate->fragments, pstrdup(val->val.str));

	/*
	 * For Iceberg UPDATE/DELETE, extract the global fragment list that the
	 * coordinator appended to fdw_private.  This is needed in the writer QE
	 * which doesn't run BeginForeignScan (the ForeignScan is in a different
	 * slice).  fdw_private layout: [0]=fileDir, [1..N]=fragment list.
	 */
	if (FORMAT_IS_ICEBERG(dataLakesstate->options->format) &&
		datalake_iceberg_all_fragments == NULL &&
		list_length(fdw_private) > 1)
	{
		datalake_iceberg_all_fragments = list_copy_tail(fdw_private, 1);
	}

	if (mtstate->operation == CMD_UPDATE || mtstate->operation == CMD_DELETE)
	{
		dataLakesstate->modify_state->us_ctid_no = ExecFindJunkAttributeInTlist(outerPlanState(mtstate)->plan->targetlist, "ctid");
		if (!AttributeNumberIsValid(dataLakesstate->modify_state->us_ctid_no))
			elog(ERROR, "could not find junk ctid column");

		TupleDesc target_tupdesc = CreateTemplateTupleDesc(2);
		for (i = 0; i < DATALAKE_ICEBERG_JUNK_NUM; i++)
		{
			IcebergJunkInfo *info = &datalake_iceberg_junk_info[i];
			TupleDescInitEntry(target_tupdesc, (AttrNumber) (i + 1), info->name, info->type, -1, 0);
		}
		dataLakesstate->modify_state->us_slot = MakeSingleTupleTableSlot(target_tupdesc, &TTSOpsVirtual);

		/*
		 * For Iceberg UPDATE/DELETE operations, create global fileIndexMap.
		 * This will be used to map file IDs to file paths during the modify phase.
		 * It will be freed in EndForeignModify.
		 * We also register a memory context callback to ensure cleanup on error/abort.
		 */
		if (FORMAT_IS_ICEBERG(dataLakesstate->options->format) &&
			datalake_iceberg_file_index_map == NULL)
		{
			MemoryContextCallback *mcb;

			datalake_iceberg_file_index_map = icebergCreateFileIndexMap();
			if (datalake_iceberg_file_index_map == NULL)
				ereport(ERROR,
						(errcode(ERRCODE_OUT_OF_MEMORY),
						 errmsg("failed to create Iceberg file index map")));

			/*
			 * Eagerly populate the file index map with ALL files from ALL
			 * segments' fragments.  This ensures every segment assigns the
			 * same file ID to the same file path, which is critical when
			 * Redistribute Motion sends rows across segments during UPDATE.
			 *
			 * datalake_iceberg_all_fragments was saved by BeginForeignScan
			 * and contains the complete, ordered fragment list that every
			 * segment received from the coordinator.
			 */
			if (datalake_iceberg_all_fragments != NULL)
			{
				icebergFileIndexMapPopulateFromAllFragments(
					datalake_iceberg_file_index_map,
					datalake_iceberg_all_fragments);
			}

			/*
			 * Register a callback to clean up the fileIndexMap when the memory context
			 * is reset or deleted. This ensures cleanup happens even in case of errors
			 * or transaction abort.
			 */
			mcb = MemoryContextAlloc(CurrentMemoryContext,
									  sizeof(MemoryContextCallback));
			mcb->func = fileIndexMapCallback;
			mcb->arg = NULL;
			MemoryContextRegisterResetCallback(CurrentMemoryContext, mcb);

			elog(DEBUG2, "datalake_fdw: Created global Iceberg file index map in BeginForeignModify for %s",
					mtstate->operation == CMD_UPDATE ? "UPDATE" : "DELETE");
		}
	}

	initModify(mtstate, resultRelInfo);

	elog(DEBUG5, "datalake_fdw: dataLakeBeginForeignModify ends on segment: %d", DATALAKE_SEGMENT_ID);
}

/*
 * dataLakeExecForeignInsert
 *		Insert one row into a foreign table
 */
static TupleTableSlot *
dataLakeExecForeignInsert(EState *estate,
					 ResultRelInfo *resultRelInfo,
					 TupleTableSlot *slot,
					 TupleTableSlot *planSlot)
{
	elog(DEBUG5, "datalake_fdw: dataLakeExecForeignInsert starts on segment: %d", DATALAKE_SEGMENT_ID);
	insertModify(resultRelInfo, slot);

	elog(DEBUG5, "datalake_fdw: dataLakeExecForeignInsert ends on segment: %d", DATALAKE_SEGMENT_ID);
	return slot;
}

/*
 * prepareIcebergDeleteJunkSlot
 *		Common helper for UPDATE and DELETE: decode ctid from the plan slot
 *		to obtain the Iceberg file path and row position, then fill the
 *		junk slot that the delete-file writer expects.
 */
static void
prepareIcebergDeleteJunkSlot(ResultRelInfo *rinfo, TupleTableSlot *planSlot,
							 const char *opName)
{
	dataLakeFdwScanState	*sstate		= (dataLakeFdwScanState*)rinfo->ri_FdwState;
	dataLakeModifyState		*mstate		= sstate->modify_state;
	TupleTableSlot			*junk_slot;
	Datum		datum;
	bool		isNull;
	ItemPointer	ctid;
	uint32		fileId;
	int64		position;
	const char *filePath;

	Assert(mstate);
	junk_slot = mstate->us_slot;
	ExecClearTuple(junk_slot);

	datum = ExecGetJunkAttribute(planSlot,
								mstate->us_ctid_no,
								&isNull);
	/* shouldn't ever get a null result... */
	if (isNull)
		elog(ERROR, "ctid is NULL");
	ctid = (ItemPointer) DatumGetPointer(datum);

	/* Decode tid to get fileId and position */
	icebergDecodeTID(ctid, &fileId, &position);

	/* Get file path from global fileIndexMap */
	filePath = icebergGetFilePath(datalake_iceberg_file_index_map, fileId);
	if (filePath == NULL)
	{
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
					errmsg("Failed to get file path for file ID %u in Iceberg %s", fileId, opName)));
	}

	/* Fill junk slot with file path and position */
	junk_slot->tts_values[0] = CStringGetDatum(filePath);
	junk_slot->tts_isnull[0] = false;
	junk_slot->tts_values[1] = Int64GetDatum((int64) position);
	junk_slot->tts_isnull[1] = false;

	elog(DEBUG2, "datalake_fdw: %s writing delete file=%s, pos="UINT64_FORMAT" for file ID %u",
			opName, filePath, position, fileId);
}

TupleTableSlot *dataLakeExecForeignUpdate(EState *estate, ResultRelInfo *rinfo, TupleTableSlot *slot, TupleTableSlot *planSlot)
{
	dataLakeFdwScanState	*sstate		= (dataLakeFdwScanState*)rinfo->ri_FdwState;
	dataLakeModifyState		*mstate		= sstate->modify_state;
	MemoryContext			 oldcontext;

	elog(DEBUG5, "datalake_fdw: dataLakeExecForeignUpdate starts on segment: %d", DATALAKE_SEGMENT_ID);

	prepareIcebergDeleteJunkSlot(rinfo, planSlot, "UPDATE");

	MemoryContextReset(sstate->rowcontext);
	oldcontext = MemoryContextSwitchTo(sstate->rowcontext);

	slot_getallattrs(slot);
	writeToProvider(sstate->provider, slot, 0);
	writeToProvider(mstate->us_provider, mstate->us_slot, 0);

	MemoryContextSwitchTo(oldcontext);

	elog(DEBUG5, "datalake_fdw: dataLakeExecForeignUpdate ends on segment: %d", DATALAKE_SEGMENT_ID);
	return slot;
}

/*
 * dataLakeEndForeignModify
 *		Finish an insert operation on a foreign table
 */
static void
dataLakeEndForeignModify(EState *estate,
					ResultRelInfo *resultRelInfo)
{
	elog(DEBUG5, "datalake_fdw: dataLakeEndForeignModify starts on segment: %d", DATALAKE_SEGMENT_ID);

	dataLakeFdwScanState *dataLakesstate = (dataLakeFdwScanState*)resultRelInfo->ri_FdwState;
	/*
	 * Do nothing in EXPLAIN (no ANALYZE) case.  resultRelInfo->ri_FdwState
	 * stays NULL.
	 */
	if (dataLakesstate == NULL)
	{
		return;
	}
	if (Gp_role == GP_ROLE_DISPATCH)
	{
		// master
		if (dataLakesstate->options->format == DL_ICEBERG_TABLE && FDW_ResultMetaList != NIL)
		{
			datalakeCommitExternalWrite(resultRelInfo->ri_RelationDesc, dataLakesstate, FDW_ResultMetaList);
		}
		list_free_deep(FDW_ResultMetaList);
		FDW_ResultMetaList = NIL;
		pfree(dataLakesstate);
		return;
	}
	else
	{
		endModify(resultRelInfo);
	}

	elog(DEBUG5, "datalake_fdw: dataLakeEndForeignModify ends on segment: %d", DATALAKE_SEGMENT_ID);
}

TupleTableSlot *dataLakeExecForeignDelete(EState *estate, ResultRelInfo *rinfo, TupleTableSlot *slot, TupleTableSlot *planSlot)
{
	dataLakeFdwScanState	*sstate		= (dataLakeFdwScanState*)rinfo->ri_FdwState;
	dataLakeModifyState		*mstate		= sstate->modify_state;
	MemoryContext			 oldcontext;

	elog(DEBUG5, "datalake_fdw: dataLakeExecForeignDelete starts on segment: %d", DATALAKE_SEGMENT_ID);

	prepareIcebergDeleteJunkSlot(rinfo, planSlot, "DELETE");

	MemoryContextReset(sstate->rowcontext);
	oldcontext = MemoryContextSwitchTo(sstate->rowcontext);

	writeToProvider(mstate->us_provider, mstate->us_slot, 0);

	MemoryContextSwitchTo(oldcontext);

	elog(DEBUG5, "datalake_fdw: dataLakeExecForeignDelete ends on segment: %d", DATALAKE_SEGMENT_ID);
	return slot;
}

/*
 * dataLakeIsForeignRelUpdatable
 *  Assume table is updatable regardless of settings.
 *		Determine whether a foreign table supports INSERT, UPDATE and/or
 *		DELETE.
 */
static int
dataLakeIsForeignRelUpdatable(Relation rel)
{
	elog(DEBUG5, "datalake_fdw: dataLakeIsForeignRelUpdatable starts on segment: %d", DATALAKE_SEGMENT_ID);
	int updatable = 0;
	dataLakeOptions *opts = datalakeGetOptions(RelationGetRelid(rel));
	switch (opts->format)
	{
		case DL_ICEBERG_TABLE:
			/* Iceberg supports INSERT, UPDATE, and DELETE */
			updatable = (1u << (int) CMD_INSERT) |
						(1u << (int) CMD_UPDATE) |
						(1u << (int) CMD_DELETE);
			break;
		case DL_HUDI_TABLE:
			/* Hudi is read-only */
			updatable = 0;
			break;
		default:
			/* Other formats support INSERT only */
			updatable = (1u << (int) CMD_INSERT);
			break;
	}
	datalakeFreeDatalakeOptions(opts);
	elog(DEBUG5, "datalake_fdw: dataLakeIsForeignRelUpdatable ends on segment: %d", DATALAKE_SEGMENT_ID);
	return updatable;
}

static bool dataLakeIsForeignScanParallelSafe(PlannerInfo *root, RelOptInfo *rel,
							  RangeTblEntry *rte)
{
	return false;
}

List*
buildAttnameList(dataLakeFdwScanState *sstate)
{
	TupleDesc	tupDesc = RelationGetDescr(sstate->rel);
	List	   *attnames = NIL;

	if (sstate->options->hiveOption->hivePartitionKey)
	{
		int attnum;
		int nPartitionKey = list_length(sstate->options->hiveOption->attNums);
		int nattrs = tupDesc->natts - nPartitionKey;
		for (attnum = 1; attnum <= nattrs; attnum++)
		{
			attnames = lappend(attnames,
					makeString(pstrdup(NameStr(*attnumAttName(sstate->rel, attnum)))));
		}
	}
	return attnames;
}

/*
 * Initialize the data parsing state.
 *
 * This includes format descriptions (delimiter, quote...), format type
 * (text, csv), etc...
 */
static void
InitParseStateFrom(CopyFromState cstate, Relation relation,
				   char *uri, int rejectlimit,
				   bool islimitinrows, char logerrors)
{

	if (rejectlimit == -1)
	{
		cstate->cdbsreh = NULL; /* no SREH */
		cstate->errMode = ALL_OR_NOTHING;
	}
	else
	{
		if (logerrors)
		{
			/* errors into file */
			cstate->errMode = SREH_LOG;
		}
		else
		{
			/* no error log */
			cstate->errMode = SREH_IGNORE;
		}
		cstate->cdbsreh = makeCdbSreh(rejectlimit,
									  islimitinrows,
									  uri,
									  (char *) cstate->cur_relname,
									  logerrors);

		cstate->cdbsreh->relid = RelationGetRelid(relation);
	}



	/* and 'fe_mgbuf' */
	cstate->fe_msgbuf = makeStringInfo();
	
	/*
	 * Create a temporary memory context that we can reset once per row to
	 * recover palloc'd memory.  This avoids any problems with leaks inside
	 * datatype input or output routines, and should be faster than retail
	 * pfree's anyway.
	 */
	cstate->rowcontext = AllocSetContextCreate(CurrentMemoryContext,
												"DatalakeFdwMemCxt",
												ALLOCSET_DEFAULT_MINSIZE,
												ALLOCSET_DEFAULT_INITSIZE,
												ALLOCSET_DEFAULT_MAXSIZE);
}

/*
 * Initiates a copy state for datalakeBeginForeignScan() and datalakeReScanForeignScan()
 */
static void
InitCopyState(ForeignScanState *node, dataLakeFdwScanState *sstate)
{
#if (PG_VERSION_NUM < 140000)
	CopyState	cstate;
#else
	CopyFromState	cstate;
#endif
	/* datalake not support masteronly */
	bool isMasterOnly = false;
	List* copy_options = datalakeGetCopyOptions(RelationGetRelid(sstate->rel));
	int rejectlimit = -1;
	bool islimitinrows = false;
	char logerrors = LOG_ERRORS_DISABLE;
	char* uri = NULL;
	int eflags = 0;
	datalakeGetCopyLogerrorOptions(RelationGetRelid(sstate->rel), &rejectlimit, &islimitinrows, &logerrors);
	datalakeGetUriFromOptions(RelationGetRelid(sstate->rel), &uri);
	List* attnamelist = buildAttnameList(sstate);

	/*
	 * Create CopyState from FDW options.  We always acquire all columns, so
	 * as to match the expected ScanTupleSlot signature.
	 */
	cstate = BeginCopyFrom(NULL,
						   sstate->rel,
#if (PG_VERSION_NUM >= 140000)
						   NULL,
#endif
						   NULL,
						   false,	/* is_program */
						   &CopyRead,	/* data_source_cb */
						   sstate,	/* data_source_cb_extra */
						   attnamelist, /* attnamelist */
						   (FORMAT_IS_CUSTOM(sstate->options->format) ? NIL : copy_options));	/* copy options */


	InitParseStateFrom(cstate, sstate->rel, uri, rejectlimit, islimitinrows, logerrors);
	sstate->cstate.cstate_scan = cstate;

	if (FORMAT_IS_CUSTOM(sstate->options->format))
	{
		List *uriList = NIL;
		uriList = lappend(uriList, makeString(uri));
		List* customOption = datalakeGetCustomOptions(RelationGetRelid(sstate->rel));
		datalake_to_exttable_BeginForeignScan(node, eflags, (void*)sstate, isMasterOnly, uriList, customOption);
	}
}

int
CopyRead(void *outbuf, int minlen, int maxlen, void *extra)
{
	dataLakeFdwScanState *sstate = (dataLakeFdwScanState*)extra;
	size_t n = 0;
	n = readBufferFromProvider(sstate->provider, outbuf, maxlen);
	return n;
}

void
initScanStatue(ForeignScanState *node, dataLakeFdwScanState *dataLakesstate)
{
	dataLakesstate->initcontext = AllocSetContextCreate(CurrentMemoryContext,
											   "datalakeFdwMemScanInitCxt",
											   ALLOCSET_DEFAULT_MINSIZE,
											   ALLOCSET_DEFAULT_INITSIZE,
											   ALLOCSET_DEFAULT_MAXSIZE);

	createHandler(dataLakesstate->provider, (void*)dataLakesstate);

	if (FORMAT_IS_CSV(dataLakesstate->options->format) ||
		FORMAT_IS_TEXT(dataLakesstate->options->format) ||
		FORMAT_IS_CUSTOM(dataLakesstate->options->format))
	{
		/* csv/text used copy */
		InitCopyState(node, dataLakesstate);
	}
	else
	{
		dataLakesstate->rowcontext = AllocSetContextCreate(CurrentMemoryContext,
											   "datalakeFdwMemCxt",
											   ALLOCSET_DEFAULT_MINSIZE,
											   ALLOCSET_DEFAULT_INITSIZE,
											   ALLOCSET_DEFAULT_MAXSIZE);
	}
}

void
iterateRecordBatch(dataLakeFdwScanState *dataLakesstate, VirtualTupleTableSlot *vslot)
{
	MemoryContextReset(dataLakesstate->rowcontext);
	MemoryContext oldContext = MemoryContextSwitchTo(dataLakesstate->rowcontext);
	bool found = readRecordBatch(dataLakesstate->provider, (void**)(&vslot->data));
	if (found)
	{
		ExecStoreVirtualTuple(&vslot->base);
	}
	MemoryContextSwitchTo(oldContext);
}


void
iterateScanStatus(ForeignScanState *node, dataLakeFdwScanState *dataLakesstate)
{
	bool found = false;
	if (FORMAT_IS_CUSTOM(dataLakesstate->options->format))
	{
		datalake_to_exttable_IterateForeignScan(node);
	}
	else if (FORMAT_IS_CSV(dataLakesstate->options->format) ||
			FORMAT_IS_TEXT(dataLakesstate->options->format))
	{
		found = NextCopyFrom(dataLakesstate->cstate.cstate_scan,
						NULL,
						node->ss.ss_ScanTupleSlot->tts_values,
						node->ss.ss_ScanTupleSlot->tts_isnull);
	}
	else
	{
		MemoryContextReset(dataLakesstate->rowcontext);
		MemoryContext oldContext = MemoryContextSwitchTo(dataLakesstate->rowcontext);

		/* Use new interface that fills tid for Iceberg tables */
		if (FORMAT_IS_ICEBERG(dataLakesstate->options->format))
		{
			found = readFromProviderWithTid(dataLakesstate->provider,
									(void*)node->ss.ss_ScanTupleSlot->tts_values,
									(void*)node->ss.ss_ScanTupleSlot->tts_isnull,
									(void*)&(node->ss.ss_ScanTupleSlot->tts_tid));
		}
		else
		{
			found = readFromProvider(dataLakesstate->provider,
									(void*)node->ss.ss_ScanTupleSlot->tts_values,
									(void*)node->ss.ss_ScanTupleSlot->tts_isnull);
		}

		MemoryContextSwitchTo(oldContext);
	}

	if (found)
	{
		if (FORMAT_IS_CSV(dataLakesstate->options->format) ||
			FORMAT_IS_TEXT(dataLakesstate->options->format))
		{
			setPartitionValue(dataLakesstate->provider, (void*)node->ss.ss_ScanTupleSlot->tts_values, (void*)node->ss.ss_ScanTupleSlot->tts_isnull);
			if (dataLakesstate->cstate.cstate_scan->cdbsreh)
			{
				dataLakesstate->cstate.cstate_scan->cdbsreh->processed++;
			}
		}

		/*
		 * For Iceberg UPDATE/DELETE, we need a full HeapTuple for system
		 * columns and junk attributes.
		 */
		if (FORMAT_IS_ICEBERG(dataLakesstate->options->format) &&
			datalake_iceberg_file_index_map != NULL)
		{
			TupleTableSlot *slot = node->ss.ss_ScanTupleSlot;
			HeapTuple	htup;
			htup = heap_form_tuple(slot->tts_tupleDescriptor, slot->tts_values, slot->tts_isnull);
			htup->t_self = slot->tts_tid;
			ExecStoreHeapTuple(htup, slot, false);
			return;
		}

		ExecStoreVirtualTuple(node->ss.ss_ScanTupleSlot);
	}
}

void
endScanStatus(dataLakeFdwScanState *dataLakesstate)
{
	if (dataLakesstate->cstate.cstate_scan != NULL && (FORMAT_IS_CSV(dataLakesstate->options->format) ||
			FORMAT_IS_TEXT(dataLakesstate->options->format)))
	{
#if (PG_VERSION_NUM < 140000)
		EndCopyFrom(dataLakesstate->cstate.cstate_scan);
#else
		EndCopyScan(dataLakesstate->cstate.cstate_scan);
#endif
	}

	if (dataLakesstate->provider != NULL && Gp_role != GP_ROLE_DISPATCH)
	{
		destroyHandler(dataLakesstate->provider);
	}
	datalakeFreeDatalakeOptions(dataLakesstate->options);
}

void
freeFdwPrivateList(List *fdw_private)
{
	pfree(fdw_private);
	return;
}

void
freeFdwPrivatePartitionList(List *fdw_private)
{
	List *partitionData = list_nth(fdw_private, PrivatePartitionData);
	datalakeFreePartitionList(partitionData);
	pfree(fdw_private);
}

void
freeFdwPrivate(dataLakeFdwScanState *sstate, ForeignScan *foreignScan)
{
	if (foreignScan->fdw_private)
	{
		if (sstate != NULL)
		{
			if (sstate->options->hiveOption->partitiontable)
			{
				freeFdwPrivatePartitionList(foreignScan->fdw_private);
			}
			else
			{
				freeFdwPrivateList(foreignScan->fdw_private);
				foreignScan->fdw_private = NULL;
			}
		}
		else
		{
			int private_count = list_length(foreignScan->fdw_private);
			if (private_count > PrivatePartitionFragmentLists)
			{
				freeFdwPrivatePartitionList(foreignScan->fdw_private);
			}
			else
			{
				freeFdwPrivateList(foreignScan->fdw_private);
				foreignScan->fdw_private = NULL;
			}
		}
	}
}

static void
initModify(ModifyTableState *state, ResultRelInfo *resultRelInfo)
{
	dataLakeFdwScanState* dataLakesstate = (dataLakeFdwScanState*)resultRelInfo->ri_FdwState;
	void *sstate = (void*)dataLakesstate;
	if (state->operation == CMD_UPDATE || state->operation == CMD_DELETE)
	{
		dataLakesstate->modify_state->us_provider = initProvider(dataLakesstate->options->format, DL_OP_DELETE, dataLakesstate->options->vectorization);
		createHandler(dataLakesstate->modify_state->us_provider, sstate);
	}
	if (state->operation == CMD_INSERT || state->operation == CMD_UPDATE)
	{
		dataLakesstate->provider = initProvider(dataLakesstate->options->format, DL_OP_WRITE, dataLakesstate->options->vectorization);
		createHandler(dataLakesstate->provider, sstate);
	}

	if (FORMAT_IS_CUSTOM(dataLakesstate->options->format) ||
		FORMAT_IS_CSV(dataLakesstate->options->format) ||
		FORMAT_IS_TEXT(dataLakesstate->options->format))
	{
		initCopyStateForModify(resultRelInfo);
	}
	else
	{
		dataLakesstate->rowcontext = AllocSetContextCreate(CurrentMemoryContext,
											"datalakeFdwMemCxt",
											ALLOCSET_DEFAULT_MINSIZE,
											ALLOCSET_DEFAULT_INITSIZE,
											ALLOCSET_DEFAULT_MAXSIZE);
	}
}

static void
InitParseStateTo(dataLakeFdwScanState *dataLakesstate,
				 CopyToState cstate)
{
	/* Initialize 'out_functions', like CopyTo() would. */

	TupleDesc	tupDesc = RelationGetDescr(cstate->rel);
	Form_pg_attribute attr = tupDesc->attrs;
	int			num_phys_attrs = tupDesc->natts;

	cstate->out_functions = (FmgrInfo *) palloc(num_phys_attrs * sizeof(FmgrInfo));
	ListCell   *cur;

	foreach(cur, cstate->attnumlist)
	{
		int			attnum = lfirst_int(cur);
		Oid			out_func_oid;
		bool		isvarlena;

		getTypeOutputInfo(attr[attnum - 1].atttypid,
						  &out_func_oid,
						  &isvarlena);
		fmgr_info(out_func_oid, &cstate->out_functions[attnum - 1]);
	}

	/* and 'fe_mgbuf' */
	cstate->fe_msgbuf = makeStringInfo();

	cstate->rowcontext = AllocSetContextCreate(CurrentMemoryContext,
										"datalakeFdwMemCxt",
										ALLOCSET_DEFAULT_MINSIZE,
										ALLOCSET_DEFAULT_INITSIZE,
										ALLOCSET_DEFAULT_MAXSIZE);

	dataLakesstate->cstate.cstate_modify = cstate;
}

static void
initCopyStateForModify(ResultRelInfo *resultRelInfo)
{
	List	   *copy_options;
	dataLakeFdwScanState *dataLakesstate = (dataLakeFdwScanState*)resultRelInfo->ri_FdwState;
#if (PG_VERSION_NUM < 140000)
	CopyState	cstate;
#else
	CopyToState	cstate;
#endif

	copy_options = datalakeGetCopyOptions(RelationGetRelid(dataLakesstate->rel));

#if (PG_VERSION_NUM < 140000)
	cstate = BeginCopyTo(dataLakesstate->rel,
		(FORMAT_IS_CUSTOM(dataLakesstate->options->format)) ? NIL : copy_options);
#else
	cstate = BeginCopyToModify(dataLakesstate->rel,
		(FORMAT_IS_CUSTOM(dataLakesstate->options->format)) ? NIL : copy_options);
#endif
	InitParseStateTo(dataLakesstate, cstate);
	if (FORMAT_IS_CUSTOM(dataLakesstate->options->format))
	{
		List* customOptions = datalakeGetCustomOptions(RelationGetRelid(dataLakesstate->rel));
		datalake_to_exttable_BeginForeignModify(NULL, resultRelInfo, NIL, customOptions, 0, 0);
	}
}

/*
 * Set up CopyState for writing to an foreign table.
 */
#if (PG_VERSION_NUM < 140000)
static CopyState
BeginCopyTo(Relation forrel, List *options)
#else
static CopyToState
BeginCopyToModify(Relation forrel, List *options)
#endif
{
#if (PG_VERSION_NUM < 140000)
	CopyState	cstate;
#else
	CopyToState	cstate;
#endif

	Assert(forrel->rd_rel->relkind == RELKIND_FOREIGN_TABLE);

#if (PG_VERSION_NUM <= 90500)
	cstate = BeginCopy(false, forrel, NULL, NULL, NIL, options, NULL);
#elif (PG_VERSION_NUM < 120000)
	cstate = BeginCopy(false, forrel, NULL, NULL, forrel->rd_id, NIL, options, NULL);
#elif (PG_VERSION_NUM < 140000)
	cstate = BeginCopy(NULL, false, forrel, NULL, forrel->rd_id, NIL, options, NULL);
#else
	cstate = BeginCopy(NULL, forrel, NULL, forrel->rd_id, NIL, options, NULL);
#endif
	cstate->dispatch_mode = COPY_DIRECT;

	/*
	 * We use COPY_CALLBACK to mean that the each line should be left in
	 * fe_msgbuf. There is no actual callback!
	 */
	cstate->copy_dest = COPY_CALLBACK;

	/*
	 * Some more initialization, that in the normal COPY TO codepath, is done
	 * in CopyTo() itself.
	 */
#if (PG_VERSION_NUM < 140000)
	cstate->null_print_client = cstate->null_print; /* default */
	if (cstate->need_transcoding)
		cstate->null_print_client = pg_server_to_custom(cstate->null_print,
														cstate->null_print_len,
														cstate->file_encoding,
														cstate->enc_conversion_proc);
#else
	cstate->opts.null_print_client = cstate->opts.null_print; /* default */
	if (cstate->need_transcoding)
		cstate->opts.null_print_client = pg_server_to_any(cstate->opts.null_print,
														  cstate->opts.null_print_len,
														  cstate->opts.file_encoding);
#endif
	return cstate;
}

static void
insertModify(ResultRelInfo *resultRelInfo, TupleTableSlot *slot)
{
	dataLakeFdwScanState *dataLakesstate = (dataLakeFdwScanState*)resultRelInfo->ri_FdwState;
	if (FORMAT_IS_CUSTOM(dataLakesstate->options->format))
	{
		datalake_to_exttable_ExecForeignInsert(NULL, resultRelInfo, slot, NULL);
	}
	else if (FORMAT_IS_CSV(dataLakesstate->options->format) ||
		FORMAT_IS_TEXT(dataLakesstate->options->format))
	{

#if (PG_VERSION_NUM < 140000)
		CopyState	cstate = dataLakesstate->cstate.cstate_modify;
#else
		CopyToState	cstate = dataLakesstate->cstate.cstate_modify;
#endif
		/* TEXT or CSV */
		slot_getallattrs(slot);
		CopyOneRowTo(cstate, slot);
		CopySendEndOfRow(cstate);

		StringInfo	fe_msgbuf = cstate->fe_msgbuf;
		writeToProvider(dataLakesstate->provider, fe_msgbuf->data, fe_msgbuf->len);

		/* Reset our buffer to start clean next round */
		cstate->fe_msgbuf->len = 0;
		cstate->fe_msgbuf->data[0] = '\0';
	}
	else
	{
		MemoryContext oldcontext;
		MemoryContextReset(dataLakesstate->rowcontext);
		oldcontext = MemoryContextSwitchTo(dataLakesstate->rowcontext);

		slot_getallattrs(slot);
		writeToProvider(dataLakesstate->provider, slot, 0);

		MemoryContextSwitchTo(oldcontext);
	}
}

/*
 * fileIndexMapCallback
 *		Callback function to clean up global fileIndexMap when memory context is reset.
 *		This ensures cleanup happens even in case of errors or transaction abort.
 */
static void
fileIndexMapCallback(void *arg)
{
	/* Free global fileIndexMap for Iceberg tables */
	if (datalake_iceberg_file_index_map != NULL)
	{
		elog(DEBUG2, "datalake_fdw: Cleaning up global Iceberg file index map via callback");
		icebergFreeFileIndexMap(datalake_iceberg_file_index_map);
		datalake_iceberg_file_index_map = NULL;
	}

	/* Clear the global fragment reference (memory owned by scan state) */
	datalake_iceberg_all_fragments = NULL;
}

/*
 * resultMetaListCallback
 *		Callback function to reset FDW_ResultMetaList when memory context is reset.
 *		This ensures the global pointer does not become a dangling reference
 *		in case of errors or transaction abort.
 */
static void
resultMetaListCallback(void *arg)
{
	FDW_ResultMetaList = NIL;
}

static void
endModify(ResultRelInfo *resultRelInfo)
{
	dataLakeFdwScanState *dataLakesstate = (dataLakeFdwScanState*)resultRelInfo->ri_FdwState;
	if (FORMAT_IS_CUSTOM(dataLakesstate->options->format))
	{
		datalake_to_exttable_EndForeignModify(NULL, resultRelInfo);
	}

	if (FORMAT_IS_CSV(dataLakesstate->options->format) ||
		FORMAT_IS_TEXT(dataLakesstate->options->format) ||
		FORMAT_IS_CUSTOM(dataLakesstate->options->format))
	{
#if (PG_VERSION_NUM < 140000)
		EndCopyFrom(dataLakesstate->cstate.cstate_modify);
#else
		EndCopyModify(dataLakesstate->cstate.cstate_modify);
#endif
	}

	if (dataLakesstate->provider)
	{
		destroyHandler(dataLakesstate->provider);
	}
	if (dataLakesstate->modify_state->us_provider)
	{
		destroyHandler(dataLakesstate->modify_state->us_provider);
	}

	/* Free global fileIndexMap for Iceberg tables */
	if (datalake_iceberg_file_index_map != NULL)
	{
		icebergFreeFileIndexMap(datalake_iceberg_file_index_map);
		datalake_iceberg_file_index_map = NULL;
	}

	if (dataLakesstate)
	{
		if (dataLakesstate->modify_state)
		{
			pfree(dataLakesstate->modify_state);
		}
		datalakeFreeDatalakeOptions(dataLakesstate->options);
		pfree(dataLakesstate);
	}
}


#if (PG_VERSION_NUM >= 140000)
/*
 * Clean up storage and release resources for COPY FROM.
 */
static void
EndCopyScan(CopyFromState cstate)
{
	/* No COPY FROM related resources except memory. */
	Assert(!cstate->is_program);
	Assert(cstate->filename == NULL);

	if (Gp_role == GP_ROLE_DISPATCH)
	{
		if (cstate && cstate->cdbsreh)
		{
			CdbSreh	 *cdbsreh = cstate->cdbsreh;
			uint64	total_rejected_from_qd = cdbsreh->rejectcount;
				if (total_rejected_from_qd > 0)
					ReportSrehResults(cdbsreh, total_rejected_from_qd);
		}
	}

	if (cstate != NULL && cstate->errMode != ALL_OR_NOTHING)
	{
		if (Gp_role == GP_ROLE_EXECUTE)
		{
			SendNumRows(cstate->cdbsreh->rejectcount, 0);
		}
		destroyCdbSreh(cstate->cdbsreh);
	}

	pgstat_progress_end_command();

	MemoryContextDelete(cstate->copycontext);
	pfree(cstate);
}

/*
 * Clean up storage and release resources for COPY TO.
 */
static void
EndCopyModify(CopyToState cstate)
{
	/* No COPY FROM related resources except memory. */
	Assert(!cstate->is_program);
	Assert(cstate->filename == NULL);

	pgstat_progress_end_command();

	MemoryContextDelete(cstate->copycontext);
	pfree(cstate);
}
#endif

static List *latestFragmentData = NIL;
static double latestIcebergRecordCount = 0;

static void
costDataLakeScan(ForeignPath *path, PlannerInfo *root,
				 RelOptInfo *baserel, ParamPathInfo *param_info)
{
	Cost		startup_cost = 0;
	Cost		run_cost = 0;
	Cost		cpu_per_tuple;

	/* Mark the path with the correct row estimate */
	if (param_info)
		path->path.rows = param_info->ppi_rows;
	else
		path->path.rows = baserel->rows;

	/*
	 * disk costs
	 */
	run_cost += seq_page_cost * baserel->pages;

	/* CPU costs */
	startup_cost += baserel->baserestrictcost.startup;
	cpu_per_tuple = cpu_tuple_cost + baserel->baserestrictcost.per_tuple;
	run_cost += cpu_per_tuple * baserel->tuples;

	path->path.startup_cost = startup_cost;
	path->path.total_cost = startup_cost + run_cost;
}

/*
 * Shuffle a list in-place using the Fisher-Yates algorithm.
 * Returns a new list with elements in random order.
 */
static List *
analyzeShuffleFragments(List *fragments)
{
	int			nfrags = list_length(fragments);
	ListCell   *cell;
	int			i;
	void	  **arr;
	List	   *result = NIL;

	if (nfrags <= 1)
		return fragments;

	arr = palloc(nfrags * sizeof(void *));
	i = 0;
	foreach(cell, fragments)
		arr[i++] = lfirst(cell);

	for (i = nfrags - 1; i > 0; i--)
	{
		int		j = random() % (i + 1);
		void   *tmp = arr[i];
		arr[i] = arr[j];
		arr[j] = tmp;
	}

	for (i = 0; i < nfrags; i++)
		result = lappend(result, arr[i]);

	pfree(arr);
	return result;
}

static ForeignScanState *
dataLakeAnalyzeBeginScan(Relation relation, int *total_fragments)
{
	int   i;
	int   segmentcount = DATALAKE_SEGMENT_COUNT;
	List *fragmentData;
	List *retrieved_attrs = NIL;
	List *selected_segments = NIL;
	TupleDesc tupdesc = RelationGetDescr(relation);
	ForeignScanState *node = (ForeignScanState *) palloc0(sizeof(ForeignScanState));
	dataLakeFdwScanState *state = (dataLakeFdwScanState *) palloc0(sizeof(dataLakeFdwScanState));

	state->options = datalakeGetOptions(RelationGetRelid(relation));

	int len = list_length(latestFragmentData);
	for (int i = segmentcount; i >= 1; i--)
	{
		int idx = intVal(list_nth(latestFragmentData, len - i));
		selected_segments = lappend_int(selected_segments, idx);
	}
	/* remove the last segmentcount elements */
	latestFragmentData = list_truncate(latestFragmentData, len - segmentcount);

	fragmentData = datalakeDeserializeExternalFragmentList(relation,
															NIL,
															state->options,
															latestFragmentData);

	/*
	 * For ANALYZE, shuffle fragments so we can scan a random subset
	 * and stop early once we've collected enough sample rows.
	 *
	 * For Iceberg/Hudi, fragmentData[0] is ExternalTableMetadata (not a
	 * CombinedScanTask). It must stay at position 0 because
	 * datalakeProtocolImportStart skips i==0 and casts it to
	 * ExternalTableMetadata*. Shuffling it would cause SIGSEGV when
	 * flatCombinedTasks tries to list_free a non-List pointer.
	 */
	*total_fragments = list_length(fragmentData);
	if (FORMAT_IS_ICEBERG(state->options->format) || FORMAT_IS_HUDI(state->options->format))
	{
		List *metadata = list_make1(linitial(fragmentData));
		List *tasks = list_copy_tail(fragmentData, 1);
		tasks = analyzeShuffleFragments(tasks);
		fragmentData = list_concat(metadata, tasks);
	}
	else
	{
		fragmentData = analyzeShuffleFragments(fragmentData);
	}

	state->options->readFdw = true;
	state->provider = initProvider(state->options->format, DL_OP_READ, false);
	state->rel = relation;
	state->fragments = fragmentData;
	state->selected_segments = selected_segments;
	state->scan_tupdesc = CreateTupleDescCopy(tupdesc);

	for (i = 1; i <= tupdesc->natts; i++)
	{
		Form_pg_attribute attr = TupleDescAttr(tupdesc, i - 1);

		if (attr->attisdropped)
			continue;

		retrieved_attrs = lappend_int(retrieved_attrs, i);
	}

	state->retrieved_attrs = retrieved_attrs;

	node->ss.ps.plan = palloc0(sizeof(ForeignScan));
	node->ss.ss_ScanTupleSlot = table_slot_create(relation, NULL);
	node->fdw_state = state;

	if (hasZeorSelectedPartition(state))
		return node;

	initScanStatue(node, state);
	return node;
}

static TupleTableSlot *
dataLakeAnalyzeScanNext(ForeignScanState *node)
{
	return dataLakeIterateForeignScan(node);
}

static void
dataLakeAnalyzeEndScan(ForeignScanState *node)
{
	ExecDropSingleTupleTableSlot(node->ss.ss_ScanTupleSlot);
	dataLakeEndForeignScan(node);
}

static const char base64_[] =
"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const int8 b64lookup_[128] = {
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
	52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
	-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
	15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
	-1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
	41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,
};

static unsigned
pg_base64_encode(const char *src, unsigned len, char *dst)
{
	char	   *p,
			   *lend = dst + 76;
	const char *s,
			   *end = src + len;
	int			pos = 2;
	uint32		buf = 0;

	s = src;
	p = dst;

	while (s < end)
	{
		buf |= (unsigned char) *s << (pos << 3);
		pos--;
		s++;

		/* write it out */
		if (pos < 0)
		{
			*p++ = base64_[(buf >> 18) & 0x3f];
			*p++ = base64_[(buf >> 12) & 0x3f];
			*p++ = base64_[(buf >> 6) & 0x3f];
			*p++ = base64_[buf & 0x3f];

			pos = 2;
			buf = 0;
		}
		if (p >= lend)
		{
			*p++ = '\n';
			lend = p + 76;
		}
	}
	if (pos != 2)
	{
		*p++ = base64_[(buf >> 18) & 0x3f];
		*p++ = base64_[(buf >> 12) & 0x3f];
		*p++ = (pos == 0) ? base64_[(buf >> 6) & 0x3f] : '=';
		*p++ = '=';
	}

	return p - dst;
}

static unsigned
pg_base64_decode(const char *src, unsigned len, char *dst)
{
	const char *srcend = src + len,
			   *s = src;
	char	   *p = dst;
	char		c;
	int			b = 0;
	uint32		buf = 0;
	int			pos = 0,
				end = 0;

	while (s < srcend)
	{
		c = *s++;

		if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
			continue;

		if (c == '=')
		{
			/* end sequence */
			if (!end)
			{
				if (pos == 2)
					end = 1;
				else if (pos == 3)
					end = 2;
				else
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
							 errmsg("unexpected \"=\"")));
			}
			b = 0;
		}
		else
		{
			b = -1;
			if (c > 0 && c < 127)
				b = b64lookup_[(unsigned char) c];
			if (b < 0)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("invalid symbol")));
		}
		/* add it to buffer */
		buf = (buf << 6) + b;
		pos++;
		if (pos == 4)
		{
			*p++ = (buf >> 16) & 255;
			if (end == 0 || end > 1)
				*p++ = (buf >> 8) & 255;
			if (end == 0 || end > 2)
				*p++ = buf & 255;
			buf = 0;
			pos = 0;
		}
	}

	if (pos != 0)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("invalid end sequence")));

	return p - dst;
}


static unsigned
pg_base64_enc_len(const char *src, unsigned srclen)
{
	/* 3 bytes will be converted to 4, linefeed after 76 chars */
	return (srclen + 2) * 4 / 3 + srclen / (76 * 3 / 4);
}

static unsigned
pg_base64_dec_len(const char *src, unsigned srclen)
{
	return (srclen * 3) >> 2;
}

static char *
encode_string(char *data, unsigned len)
{
	char   *result;
	int		res;
	int		resultlen;

	resultlen = pg_base64_enc_len(data, len);
	result = palloc(resultlen + 1);
	res = pg_base64_encode(data, len, result);
	if (res > resultlen)
		elog(FATAL, "overflow - base64 encode estimate too small");

	result[res] = '\0';
	return result;
}

static char *
decode_string(char *data, unsigned len, int *decodedLen)
{
	char   *result;
	int		res;
	int		resultlen;

	resultlen = pg_base64_dec_len(data, len);
	result = palloc(resultlen);
	res = pg_base64_decode(data, len, result);
	if (res > resultlen)
		elog(FATAL, "overflow - base64 decode estimate too small");

	*decodedLen = res;
	return result;
}

static int
process_sample_rows(Portal portal,
					QueryDesc  *queryDesc,
					Relation onerel,
					HeapTuple *rows,
					int targrows,
					double *totalrows,
					double *totaldeadrows)
{
	/*
	 * 'colLargeRowIndexes' is essentially an argument, but it's passed via a
	 * global variable to avoid changing the AcquireSampleRowsFunc prototype.
	 */
	Bitmapset **colLargeRowIndexes = acquire_func_colLargeRowIndexes;
	/* double     *colLargeRowLength = acquire_func_colLargeRowLength; */
	double     *colNDVBySeg = acquire_func_colNDVBySeg;
	TupleDesc	relDesc = RelationGetDescr(onerel);
	TupleDesc	funcTupleDesc;
	TupleDesc	sampleTupleDesc;
	int			sampleTuples;	/* 32 bit - assume that number of tuples will not > 2B */
	Datum	   *funcRetValues;
	bool	   *funcRetNulls;
	int			ncolumns;
	AttInMetadata *attinmeta;
	int			numLiveColumns;
	int			i;
	int			index = 0;
	TupleTableSlot *slot;
	Datum	   *dvalues;
	bool	   *dnulls;

	/*
	 * Count the number of columns, excluding dropped columns. We'll need that
	 * later.
	 */
	numLiveColumns = 0;
	for (i = 0; i < relDesc->natts; i++)
	{
		Form_pg_attribute attr = TupleDescAttr(relDesc, i);

		if (attr->attisdropped)
			continue;

		numLiveColumns++;
	}

	/*
	 * Build a modified tuple descriptor for the table.
	 *
	 * Some datatypes need special treatment, so we cannot use the relation's
	 * original tupledesc.
	 *
	 * Also create tupledesc of return record of function gp_acquire_sample_rows.
	 */
	sampleTupleDesc = CreateTupleDescCopy(relDesc);
	ncolumns = numLiveColumns + NUM_SAMPLE_FIXED_COLS;
	
	funcTupleDesc = CreateTemplateTupleDesc(ncolumns);
	TupleDescInitEntry(funcTupleDesc, (AttrNumber) 1, "", FLOAT8OID, -1, 0);
	TupleDescInitEntry(funcTupleDesc, (AttrNumber) 2, "", FLOAT8OID, -1, 0);
	TupleDescInitEntry(funcTupleDesc, (AttrNumber) 3, "", FLOAT8ARRAYOID, -1, 0);
	TupleDescInitEntry(funcTupleDesc, (AttrNumber) 4, "", FLOAT8ARRAYOID, -1, 0);

	for (i = 0; i < relDesc->natts; i++)
	{
		Form_pg_attribute attr = TupleDescAttr(relDesc, i);

		Oid			typid = gp_acquire_sample_rows_col_type(attr->atttypid);

		TupleDescAttr(sampleTupleDesc, i)->atttypid = typid;

		if (!attr->attisdropped)
		{
			TupleDescInitEntry(funcTupleDesc, (AttrNumber) NUM_SAMPLE_FIXED_COLS + 1 + index, "",
							   typid, attr->atttypmod, attr->attndims);

			index++;
		}
	}

	/* For RECORD results, make sure a typmod has been assigned */
	Assert(funcTupleDesc->tdtypeid == RECORDOID && funcTupleDesc->tdtypmod < 0);
	assign_record_type_typmod(funcTupleDesc);

	attinmeta = TupleDescGetAttInMetadata(sampleTupleDesc);

	/*
	 * Read the result set from each segment. Gather the sample rows *rows,
	 * and sum up the summary rows for grand 'totalrows' and 'totaldeadrows'.
	 */
	funcRetValues = (Datum *) palloc0(funcTupleDesc->natts * sizeof(Datum));
	funcRetNulls = (bool *) palloc0(funcTupleDesc->natts * sizeof(bool));
	dvalues = (Datum *) palloc0(relDesc->natts * sizeof(Datum));
	dnulls = (bool *) palloc0(relDesc->natts * sizeof(bool));
	sampleTuples = 0;
	*totalrows = 0;
	*totaldeadrows = 0;

	slot = MakeSingleTupleTableSlot(queryDesc->tupDesc, &TTSOpsMinimalTuple);

	for (;;)
	{
		bool		ok;
		TupleDesc	typeinfo;
		int			natts;
		Datum		attr;
		bool		isnull;
		double		this_totalrows = 0;
		double		this_totaldeadrows = 0;

		CHECK_FOR_INTERRUPTS();

		ok = tuplestore_gettupleslot(portal->holdStore, true, false, slot);

		if (!ok)
			break;

		typeinfo = slot->tts_tupleDescriptor;
		natts = typeinfo->natts;

		/* There should be only one attribute with OID RECORDOID. */
		if (1 != natts)
		{
			elog(ERROR,
				"wrong number of attributes %d when 1 expected",
				natts);
		}

		if (RECORDOID != typeinfo->attrs[0].atttypid)
		{
			elog(ERROR,
				"wrong attribute OID %d, RECORDOID %d is expected",
				typeinfo->attrs[0].atttypid, RECORDOID);

		}

		/* Make sure the tuple is fully deconstructed */
		slot_getallattrs(slot);

		/* There should be only one attribute with OID RECORDOID */
		attr = slot_getattr(slot, 1, &isnull);
		if (isnull)
		{
			elog(ERROR,
				"null value for attribute in tuple");
		}

		/* Get record from attribute and parse it */
		{
			HeapTupleHeader rec = (HeapTupleHeader) PG_DETOAST_DATUM(attr);
			Oid			tupType;
			int32		tupTypmod;
			TupleDesc	tupdesc;
			HeapTupleData tuple;

			/* Extract type info from the tuple itself */
			tupType = HeapTupleHeaderGetTypeId(rec);
			tupTypmod = HeapTupleHeaderGetTypMod(rec);
			tupdesc = lookup_rowtype_tupdesc(tupType, tupTypmod);

			/* Build a temporary HeapTuple control structure */
			tuple.t_len = HeapTupleHeaderGetDatumLength(rec);
			ItemPointerSetInvalid(&(tuple.t_self));
			tuple.t_data = rec;

			/* Break down the tuple into fields */
			heap_deform_tuple(&tuple, tupdesc, funcRetValues, funcRetNulls);

			if (!funcRetNulls[0])
			{
				/* This is a summary row. */
				ArrayType  *arrayVal;
				Datum      *colndv;
				bool       *nulls;
				int        numelems;

				Assert(!funcRetNulls[1] && !funcRetNulls[3]);

				this_totalrows = DatumGetFloat8(funcRetValues[0]);
				this_totaldeadrows = DatumGetFloat8(funcRetValues[1]);
				(*totalrows) += this_totalrows;
				(*totaldeadrows) += this_totaldeadrows;

				arrayVal = DatumGetArrayTypeP(funcRetValues[3]);
				deconstruct_array(arrayVal, FLOAT8OID, 8, true, 'd',
								  &colndv, &nulls, &numelems);
				for (i = 0; i < relDesc->natts; i++)
				{
					double this_colndv = DatumGetFloat8(colndv[i]);
					if (this_colndv < 0) {
						Assert(this_colndv >= -1);
						colNDVBySeg[i] += abs(this_colndv) * this_totalrows;
					} else {
						/* if current segment have any data, then ndv won't be 0.
						 * if current segment have no rows, ndv is 0.
						 */
						colNDVBySeg[i] += DatumGetFloat8(colndv[i]);
					}
				}
			}
			else
			{
				/* This is a sample row. */
				if (sampleTuples >= targrows)
					elog(ERROR, "too many sample rows received from gp_acquire_sample_rows");

				/* Read the 'toolarge' bitmap, if any */
				if (colLargeRowIndexes && !funcRetNulls[2])
				{
					ArrayType  *arrayVal;
					Datum	   *largelength;
					bool	   *nulls;
					int	    numelems;
					arrayVal = DatumGetArrayTypeP(funcRetValues[2]);
					deconstruct_array(arrayVal, FLOAT8OID, 8, true, 'd',
								&largelength, &nulls, &numelems);

					for (i = 0; i < relDesc->natts; i++)
					{
						Form_pg_attribute attr = TupleDescAttr(relDesc, i);

						if (attr->attisdropped)
							continue;

						if (largelength[i] != (Datum) 0)
						{
							colLargeRowIndexes[i] = bms_add_member(colLargeRowIndexes[i], sampleTuples);
							/* colLargeRowLength[i] += DatumGetFloat8(largelength[i]); */
						}
					}
				}

				/* Process the columns */
				index = 0;
				for (i = 0; i < relDesc->natts; i++)
				{
					Form_pg_attribute attr = TupleDescAttr(relDesc, i);

					if (attr->attisdropped)
					{
						dnulls[i] = true;
						continue;
					}

					dnulls[i] = funcRetNulls[NUM_SAMPLE_FIXED_COLS + index];
					dvalues[i] = funcRetValues[NUM_SAMPLE_FIXED_COLS + index];
					index++;	/* Move index to the next result set attribute */
				}

				/*
				* Form a tuple
				*/
				rows[sampleTuples] = heap_form_tuple(attinmeta->tupdesc,
													dvalues,
													dnulls);
				sampleTuples++;

				/*
				 * note: we don't set the OIDs in the sample. ANALYZE doesn't
				 * collect stats for them
				 */
			}
			ReleaseTupleDesc(tupdesc);
		}

		ExecClearTuple(slot);
	}
	ExecDropSingleTupleTableSlot(slot);
	pfree(funcRetValues);
	pfree(funcRetNulls);
	pfree(dvalues);
	pfree(dnulls);

	return sampleTuples;
}

Datum
datalake_acquire_sample_rows(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx = NULL;
	gp_acquire_sample_rows_context *ctx;
	MemoryContext oldcontext;
	Oid			relOid = PG_GETARG_OID(0);
	int32		targrows = PG_GETARG_INT32(1);
	bool        inherited = PG_GETARG_BOOL(2);
	text	   *encodedFragment = PG_GETARG_TEXT_PP(3);

	TupleDesc	relDesc;
	TupleDesc	outDesc;
	int			live_natts;

	if (targrows < 1)
		elog(ERROR, "invalid targrows argument");

	if (SRF_IS_FIRSTCALL())
	{
		Relation	onerel;
		int			attno;
		int			outattno;
		VacuumParams	params;
		RangeVar	   *this_rangevar;
		char *decodedFragment;
		int decodedLen;

		funcctx = SRF_FIRSTCALL_INIT();

		/*
		 * switch to memory context appropriate for multiple function
		 * calls
		 */
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		/* Construct the context to keep across calls. */
		ctx = (gp_acquire_sample_rows_context *) palloc0(sizeof(gp_acquire_sample_rows_context));
		ctx->targrows = targrows;
		ctx->inherited = inherited;

		if (!pg_class_ownercheck(relOid, GetUserId()))
			aclcheck_error(ACLCHECK_NOT_OWNER, OBJECT_TABLE,
						   get_rel_name(relOid));

		onerel = table_open(relOid, AccessShareLock);
		relDesc = RelationGetDescr(onerel);

		/* will be init in `analyze_rel` */
		ctx->stadistincts = (Datum *) palloc0(relDesc->natts * sizeof(Datum));

		decodedFragment = decode_string(VARDATA_ANY(encodedFragment), VARSIZE_ANY_EXHDR(encodedFragment), &decodedLen);
		latestFragmentData = (List *) deserializeNode(decodedFragment, decodedLen);
		pfree(decodedFragment);

		MemSet(&params, 0, sizeof(VacuumParams));
		params.options |= VACOPT_ANALYZE;
		params.freeze_min_age = -1;
		params.freeze_table_age = -1;
		params.multixact_freeze_min_age = -1;
		params.multixact_freeze_table_age = -1;
		params.is_wraparound = false;
		params.log_min_duration = -1;
		params.index_cleanup = VACOPTVALUE_AUTO;
		params.truncate = VACOPTVALUE_AUTO;

		this_rangevar = makeRangeVar(get_namespace_name(onerel->rd_rel->relnamespace),
									 pstrdup(RelationGetRelationName(onerel)),
									 -1);
		analyze_rel(relOid, this_rangevar, &params, NULL,
					true, GetAccessStrategy(BAS_VACUUM), ctx);

		/* Count the number of non-dropped cols */
		live_natts = 0;
		for (attno = 1; attno <= relDesc->natts; attno++)
		{
			Form_pg_attribute relatt = TupleDescAttr(relDesc, attno - 1);

			if (relatt->attisdropped)
				continue;
			live_natts++;
		}

		outDesc = CreateTemplateTupleDesc(NUM_SAMPLE_FIXED_COLS + live_natts);

		/* First, some special cols: */

		/* These two are only set in the last, summary row */
		TupleDescInitEntry(outDesc,
						   1,
						   "totalrows",
						   FLOAT8OID,
						   -1,
						   0);
		TupleDescInitEntry(outDesc,
						   2,
						   "totaldeadrows",
						   FLOAT8OID,
						   -1,
						   0);

		/* extra column to indicate oversize cols */
		TupleDescInitEntry(outDesc,
						   3,
						   "oversized_cols_length",
						   FLOAT8ARRAYOID,
						   -1,
						   0);

		/* stadistinct for each live column */
		TupleDescInitEntry(outDesc,
						   4,
						   "stadistinct_array",
						   FLOAT8ARRAYOID,
						   -1,
						   0);

		outattno = NUM_SAMPLE_FIXED_COLS + 1;
		for (attno = 1; attno <= relDesc->natts; attno++)
		{
			Form_pg_attribute relatt = TupleDescAttr(relDesc, attno - 1);
			Oid			typid;

			if (relatt->attisdropped)
				continue;

			typid = gp_acquire_sample_rows_col_type(relatt->atttypid);

			TupleDescInitEntry(outDesc,
							   outattno++,
							   NameStr(relatt->attname),
							   typid,
							   relatt->atttypmod,
							   0);
		}

		BlessTupleDesc(outDesc);
		funcctx->tuple_desc = outDesc;

		ctx->onerel = onerel;
		funcctx->user_fctx = ctx;
		ctx->outDesc = outDesc;

		ctx->index = 0;
		ctx->summary_sent = false;
		/*
		 * we only get sample data from segindex 0 for replicated table
		 */
		if (Gp_role == GP_ROLE_EXECUTE && GpPolicyIsReplicated(onerel->rd_cdbpolicy)
									   && GpIdentity.segindex > 0)
		{
			ctx->index = ctx->num_sample_rows;
			ctx->summary_sent = true;
		}

		MemoryContextSwitchTo(oldcontext);
	}

	/* stuff done on every call of the function */
	funcctx = SRF_PERCALL_SETUP();

	ctx = funcctx->user_fctx;
	relDesc = RelationGetDescr(ctx->onerel);
	outDesc = ctx->outDesc;

	Datum	   *outvalues = (Datum *) palloc(outDesc->natts * sizeof(Datum));
	bool	   *outnulls = (bool *) palloc(outDesc->natts * sizeof(bool));
	HeapTuple	res;

	/* First return all the sample rows */
	if (ctx->index < ctx->num_sample_rows)
	{
		HeapTuple	relTuple = ctx->sample_rows[ctx->index];
		int			attno;
		int			outattno;
		bool			has_toolarge = false;
		Datum	   *relvalues = (Datum *) palloc(relDesc->natts * sizeof(Datum));
		bool	   *relnulls = (bool *) palloc(relDesc->natts * sizeof(bool));
		Datum      *oversized_cols_length = (Datum *) palloc0(relDesc->natts * sizeof(Datum));

		heap_deform_tuple(relTuple, relDesc, relvalues, relnulls);

		outattno = NUM_SAMPLE_FIXED_COLS + 1;
		for (attno = 1; attno <= relDesc->natts; attno++)
		{
			Form_pg_attribute relatt = TupleDescAttr(relDesc, attno - 1);
			Datum		relvalue;
			bool		relnull;

			if (relatt->attisdropped)
				continue;
			relvalue = relvalues[attno - 1];
			relnull = relnulls[attno - 1];

			/* Is this attribute "too large" to return? */
			if (relatt->attlen == -1 && !relnull)
			{
				Size		toasted_size = toast_datum_size(relvalue);

				if (toasted_size > WIDTH_THRESHOLD)
				{
					oversized_cols_length[attno - 1] = Float8GetDatum((double)toasted_size);
					has_toolarge = true;
					relvalue = (Datum) 0;
					relnull = true;
				}
			}
			outvalues[outattno - 1] = relvalue;
			outnulls[outattno - 1] = relnull;
			outattno++;
		}

		/*
		 * If any of the attributes were oversized, construct the text datum
		 * to represent the bitmap.
		 */
		if (has_toolarge)
		{
			outvalues[2] = PointerGetDatum(construct_array(oversized_cols_length, relDesc->natts,
														FLOAT8OID, 8, true, 'd'));
			outnulls[2] = false;
		}
		else
		{
			outvalues[2] = (Datum) 0;
			outnulls[2] = true;
		}
		outvalues[0] = (Datum) 0;
		outnulls[0] = true;
		outvalues[1] = (Datum) 0;
		outnulls[1] = true;

		outvalues[3] = (Datum) 0;
		outnulls[3] = true;

		res = heap_form_tuple(outDesc, outvalues, outnulls);

		ctx->index++;

		SIMPLE_FAULT_INJECTOR("returned_sample_row");

		SRF_RETURN_NEXT(funcctx, HeapTupleGetDatum(res));
	}
	else if (!ctx->summary_sent)
	{
		/* Done returning the sample. Return the summary row, and we're done. */
		int			outattno;

		outvalues[0] = Float8GetDatum(ctx->totalrows);
		outnulls[0] = false;
		outvalues[1] = Float8GetDatum(ctx->totaldeadrows);
		outnulls[1] = false;

		outvalues[2] = (Datum) 0;
		outnulls[2] = true;

		outvalues[3] = PointerGetDatum(construct_array(ctx->stadistincts, relDesc->natts,
													   FLOAT8OID, 8, true, 'd'));
		outnulls[3] = false;

		for (outattno = NUM_SAMPLE_FIXED_COLS + 1; outattno <= outDesc->natts; outattno++)
		{
			outvalues[outattno - 1] = (Datum) 0;
			outnulls[outattno - 1] = true;
		}

		res = heap_form_tuple(outDesc, outvalues, outnulls);

		ctx->summary_sent = true;

		SRF_RETURN_NEXT(funcctx, HeapTupleGetDatum(res));
	}

	table_close(ctx->onerel, AccessShareLock);

	pfree(ctx);
	funcctx->user_fctx = NULL;

	SRF_RETURN_DONE(funcctx);

}

/*
 * Build a querydesc for a sql, set "dest" to portal->holdStore
 */
static QueryDesc *build_querydesc(Portal portal, char *sql)
{
	List	   *raw_parsetree_list;
	RawStmt	   *parsetree;
	List	   *querytree_list;
	List	   *plantree_list;
	PlannedStmt *plan_stmt;
	DestReceiver *destReceiver;
	QueryDesc  *queryDesc = NULL;
	destReceiver = CreateDestReceiver(DestTuplestore);
	SetTuplestoreDestReceiverParams(destReceiver,
									portal->holdStore,
									portal->holdContext,
									false,
									NULL,
									NULL);

	/*
	 * Parse the SQL string into a list of raw parse trees.
	 */
	raw_parsetree_list = pg_parse_query(sql);

	/*
	 * Do parse analysis, rule rewrite, planning, and execution for each raw
	 * parsetree.
	 */

	/* There is only one element in list due to simple select. */
	Assert(list_length(raw_parsetree_list) == 1);
	parsetree = (RawStmt *) linitial(raw_parsetree_list);

	querytree_list = pg_analyze_and_rewrite(parsetree,
											sql,
											NULL,
											0,
											NULL);
	plantree_list = pg_plan_queries(querytree_list, sql, 0, NULL);

	/* There is only one statement in list due to simple select. */
	Assert(list_length(plantree_list) == 1);
	plan_stmt = (PlannedStmt *) linitial(plantree_list);

	queryDesc = CreateQueryDesc(plan_stmt,
								sql,
								GetActiveSnapshot(),
								InvalidSnapshot,
								destReceiver,
								NULL,
								NULL,
								INSTRUMENT_NONE);



	list_free_deep(querytree_list);
	list_free_deep(raw_parsetree_list);

	return queryDesc;
}

static int
acquire_sample_rows_dispatcher(Relation relation, bool inh, int elevel,
							   HeapTuple *rows, int targrows,
							   double *totalrows, double *totaldeadrows)
{
	StringInfoData str;
	int			perseg_targrows;
	int			sampleTuples;	/* 32 bit - assume that number of tuples will not > 2B */
	char	   *sql;
	Portal		portal;
	dataLakeOptions *options;
	char *sFragment;
	char *encodedFragment;
	int sFragmentLen;
	int sFragmentUnCompressedLen;
	List *sFragmentList;
	QueryDesc  *queryDesc = NULL;

	Assert(targrows > 0.0);

	/*
	 * Step1: Construct SQL command to dispatch to segments.
	 *
	 * Acquire an evenly-sized sample from each segment.
	 *
	 * XXX: If there's a significant bias between the segments, i.e. some
	 * segments have a lot more rows than others, the sample will biased, too.
	 * Would be nice to improve that, but it's not clear how. We could issue
	 * another query to get the table size from each segment first, and use
	 * those to weigh the sample size to get from each segment. But that'd
	 * require an extra round-trip, which is also not good. The caller
	 * actually already did that, to get the total relation size, but it
	 * doesn't pass that down to us, let alone the per-segment sizes.
	 */
	perseg_targrows = targrows / relation->rd_cdbpolicy->numsegments;

	options = datalakeGetOptions(RelationGetRelid(relation));

	sFragmentList = list_make2(makeString("dummy"), makeString("dummy"));
	sFragmentList = list_concat(sFragmentList, latestFragmentData);
	sFragment = serializeNode((Node *) sFragmentList, &sFragmentLen, &sFragmentUnCompressedLen);
	encodedFragment = encode_string(sFragment, sFragmentLen);
	pfree(sFragment);

	/*
	 * Did not use 'select * from pg_catalog.gp_acquire_sample_rows(...) as (..);'
	 * here. Because it requires to specify columns explicitly which leads to
	 * permission check on each columns. This is not consistent with GPDB5 and
	 * may result in different behaviour under different acl configuration.
	 */
	initStringInfo(&str);
	appendStringInfo(&str, "select datalake_acquire_sample_rows(%u::oid, %d, '%s'::boolean, '%s'::text);",
					 RelationGetRelid(relation),
					 perseg_targrows,
					 inh ? "t" : "f",
					 encodedFragment);
	pfree(encodedFragment);

	/*
	 * Step2: Execute the constructed SQL.
	 *
	 * Do not use SPI here, because there might be a large number of wide rows
	 * returned and stored in memory, SPI cannot spill data to disk which may
	 * lead to OOM easily.
	 *
	 * Do not use SPI cusror either, because we should use SPI_cursor_fetch to fetch
	 * results in batches, which may have bad performance.
	 *
	 * Use ExecutorStart|ExecutorRun|ExecutorEnd to execute a plan and store results
	 * into tuplestore could handle this situation well.
	 *
	 * Execute the given query and store the results into portal->holdStore to
	 * avoid memory error.
	 */
	elog(elevel, "Executing SQL: %s", str.data);
	sql = str.data;
	/* Create a new portal to run the query in */
	portal = CreateNewPortal();
	/* Don't display the portal in pg_cursors, it is for internal use only */
	portal->visible = false;
	/* use a tuplestore to store received tuples to avoid out of memory error */
	PortalCreateHoldStore(portal);
	queryDesc = build_querydesc(portal, sql);

	/* Call ExecutorStart to prepare the plan for execution */
	ExecutorStart(queryDesc, 0);

	/* Run the plan  */
	ExecutorRun(queryDesc, ForwardScanDirection, 0L, true);

	/* Wait for completion of all qExec processes. */
	if (queryDesc->estate->dispatcherState
		&& queryDesc->estate->dispatcherState->primaryResults)
	{
		cdbdisp_checkDispatchResult(queryDesc->estate->dispatcherState, DISPATCH_WAIT_NONE);
	}

	ExecutorFinish(queryDesc);
	/*
	 * Step3: process results.
	 */
	sampleTuples = process_sample_rows(portal, queryDesc, relation, rows,
									targrows, totalrows, totaldeadrows);

	ExecutorEnd(queryDesc);
	FreeQueryDesc(queryDesc);
	PortalDrop(portal, false);

	return sampleTuples;
}

/*
 * Collect sample rows from the result of query.
 *	 - Use all tuples in sample until target # of samples are collected.
 *	 - Subsequently, replace already-sampled tuples randomly.
 */
static void
analyze_row_processor(TupleTableSlot *slot, DataLakeAnalyzeState *astate)
{
	int			targrows = astate->targrows;
	int			pos;			/* array index to store tuple in */
	MemoryContext oldcontext;

	/* Always increment sample row counter. */
	astate->samplerows += 1;

	/*
	 * Determine the slot where this sample row should be stored.  Set pos to
	 * negative value to indicate the row should be skipped.
	 */
	if (astate->numrows < targrows)
	{
		/* First targrows rows are always included into the sample */
		pos = astate->numrows++;
	}
	else
	{
		/*
		 * Now we start replacing tuples in the sample until we reach the end
		 * of the relation.  Same algorithm as in acquire_sample_rows in
		 * analyze.c; see Jeff Vitter's paper.
		 */
		if (astate->rowstoskip < 0)
			astate->rowstoskip = reservoir_get_next_S(&astate->rstate, astate->samplerows, targrows);

		if (astate->rowstoskip <= 0)
		{
			/* Choose a random reservoir element to replace. */
			pos = (int) (targrows * sampler_random_fract(astate->rstate.randstate));
			Assert(pos >= 0 && pos < targrows);
			heap_freetuple(astate->rows[pos]);
		}
		else
		{
			/* Skip this tuple. */
			pos = -1;
		}

		astate->rowstoskip -= 1;
	}

	if (pos >= 0)
	{
		/*
		 * Create sample tuple from current result row, and store it in the
		 * position determined above.  The tuple has to be created in anl_cxt.
		 */
		oldcontext = MemoryContextSwitchTo(astate->anl_cxt);

		astate->rows[pos] = ExecCopySlotHeapTuple(slot);

		MemoryContextSwitchTo(oldcontext);
	}
}


/*
 * Acquire a random sample of rows from foreign table managed by datalake-fdw.
 *
 * We fetch the whole table from the remote side and pick out some sample rows.
 *
 * Selected rows are returned in the caller-allocated array rows[],
 * which must have at least targrows entries.
 * The actual number of rows selected is returned as the function result.
 * We also count the total number of rows in the table and return it into
 * *totalrows.  Note that *totaldeadrows is always set to 0.
 *
 * Note that the returned list of rows is not always in order by physical
 * position in the table.  Therefore, correlation estimates derived later
 * may be meaningless, but it's OK because we don't use the estimates
 * currently (the planner only pays attention to correlation for indexscans).
 */
static int
segmentAcquireSampleRowsFunc(Relation relation, int elevel,
							  HeapTuple *rows, int targRows,
							  double *totalRows,
							  double *totalDeadRows)
{
	DataLakeAnalyzeState astate;
	ForeignScanState *state;
	TupleTableSlot *slot;
	int			total_fragments;

	/* Initialize workspace state */
	astate.rows = rows;
	astate.targrows = targRows;
	astate.numrows = 0;
	astate.samplerows = 0;
	astate.rowstoskip = -1;		/* -1 means not set yet */
	reservoir_init_selection_state(&astate.rstate, targRows);

	/* Remember ANALYZE context, and create a per-tuple temp context */
	astate.anl_cxt = CurrentMemoryContext;
	astate.temp_cxt = AllocSetContextCreate(CurrentMemoryContext,
											"datalake_fdw temporary data",
											ALLOCSET_SMALL_SIZES);

	state = dataLakeAnalyzeBeginScan(relation, &total_fragments);

	/*
	 * Scan rows from shuffled fragments using reservoir sampling.
	 *
	 * We must scan significantly more rows than targRows to get accurate
	 * ndistinct estimates.  The previous approach stopped at targRows,
	 * which for target=100 meant only 30,000 rows from a single fragment.
	 * This gave PostgreSQL's stadistinct estimator far too little data
	 * diversity, causing ORCA to severely underestimate GROUP BY
	 * cardinality and choose catastrophic Streaming Partial HashAggregate
	 * plans (e.g. TPC-H Q3: 30s vs 12s).
	 *
	 * We now scan at least (total_fragments * rows_per_fragment) rows to
	 * ensure coverage across all fragments, similar to how heap ANALYZE
	 * samples random pages across the whole table.  Reservoir sampling
	 * in analyze_row_processor ensures we keep exactly targRows rows
	 * in the final sample regardless of how many we scan.
	 */
	{
		int		rows_per_fragment = Max(targRows / Max(total_fragments, 1), 1000);
		double	scan_limit = (double) total_fragments * rows_per_fragment;

		/* Scan at least 10x targRows to ensure reservoir has good diversity */
		if (scan_limit < (double) targRows * 10)
			scan_limit = (double) targRows * 10;

		/*
		 * Ensure a minimum scan of 100K rows.  Parquet files are often
		 * sorted (e.g. date_dim sorted by year), so scanning only a few
		 * thousand rows produces MCVs/histograms that miss large portions
		 * of the value range (e.g. only years 1900-1917 instead of
		 * 1900-2100).  100K rows covers most dimension tables entirely
		 * while adding negligible overhead for large tables.
		 */
		if (scan_limit < 100000.0)
			scan_limit = 100000.0;

		/*
		 * Cap scan_limit to avoid excessive ANALYZE time on very large
		 * tables (e.g. SF10000 with 6000 fragments would scan 6M rows).
		 * 1M rows is enough for accurate stadistinct on high-cardinality
		 * columns while keeping ANALYZE under a few seconds per segment.
		 */
		if (scan_limit > 1000000.0)
			scan_limit = 1000000.0;

		for (;;)
		{
			/* Allow users to cancel long query */
			CHECK_FOR_INTERRUPTS();

			slot = dataLakeAnalyzeScanNext(state);
			if (TupIsNull(slot))
				break;

			analyze_row_processor(slot, &astate);

			if (astate.samplerows >= scan_limit)
				break;
		}
	}

	dataLakeAnalyzeEndScan(state);

	/* We assume that we have no dead tuple. */
	*totalDeadRows = 0.0;

	/*
	 * Estimate total row count.  samplerows is the number of rows scanned
	 * (which may be much larger than targRows due to our extended scanning),
	 * while numrows is the number of rows kept in the reservoir (at most
	 * targRows).
	 */
	*totalRows = astate.samplerows;

	ereport(elevel,
			(errmsg("\"%s\": scanned %.0f rows across %d fragments, %d rows in sample",
					RelationGetRelationName(relation),
					astate.samplerows, total_fragments, astate.numrows)));

	return astate.numrows;
}


static int
dataLakeAcquireSampleRowsFunc(Relation relation, int elevel,
							  HeapTuple *rows, int targRows,
							  double *totalRows,
							  double *totalDeadRows)
{
	/*
	 * On the coordinator (DISPATCH or UTILITY mode), dispatch the sample
	 * collection to segments.  GP_ROLE_UTILITY occurs when analyze_rel is
	 * called internally (e.g. from datalake_acquire_sample_rows which runs
	 * in utility mode on the coordinator).
	 */
	if (Gp_role == GP_ROLE_DISPATCH || Gp_role == GP_ROLE_UTILITY)
	{
		int numrows;

		/* Fetch sample from the segments. */
		numrows = acquire_sample_rows_dispatcher(relation, false, elevel,
												 rows, targRows,
												 totalRows, totalDeadRows);

		/*
		 * For Iceberg tables, override totalRows with the precise record
		 * count from Iceberg snapshot metadata. The per-segment sample-based
		 * estimate is wildly inaccurate because each segment only reports
		 * the rows it sampled, not the table total.
		 */
		if (latestIcebergRecordCount > 0)
			*totalRows = latestIcebergRecordCount;

		return numrows;
	}

	return segmentAcquireSampleRowsFunc(relation, elevel, rows,
										targRows, totalRows, totalDeadRows);
}

/*
 * dataLakeAnalyzeForeignTable
 *		Test whether analyzing this foreign table is supported
 */
static bool
dataLakeAnalyzeForeignTable(Relation relation,
							AcquireSampleRowsFunc *func,
							BlockNumber *totalPages)
{
	int64_t totalSize = 0;
	dataLakeOptions *options;
	IcebergTableStatistics *statistics = NULL;

	/* Return the row-analysis function pointer */
	*func = dataLakeAcquireSampleRowsFunc;
	int segmentcount = DATALAKE_SEGMENT_COUNT;
	options = datalakeGetOptions(RelationGetRelid(relation));

	if (Gp_role == GP_ROLE_DISPATCH || Gp_role == GP_ROLE_UTILITY)
	{
		latestFragmentData = datalakeGetExternalFragmentList(relation, NULL, options, &totalSize);
		/* master does not process any fragments */
		List *random_segments = datalakeSelectRandomSegments(segmentcount, external_table_limit_segment_num);
		/* put the random segments into the list */
		latestFragmentData = list_concat(latestFragmentData, random_segments);
		if (options->format == DL_ICEBERG_TABLE)
		{
			statistics = datalakeGetTableStatistics(RelationGetRelid(relation), options);
			totalSize = statistics->bytesInDataFile;
			latestIcebergRecordCount = (double) statistics->recordCount;
		}
		else
		{
			latestIcebergRecordCount = 0;
		}
		if (statistics) {
			pfree(statistics);
		}
	}

	/*
	 * Convert size to pages.  Must return at least 1 so that we can tell
	 * later on that pg_class.relpages is not default.
	 */
	*totalPages = (totalSize + (BLCKSZ - 1)) / BLCKSZ;
	if (*totalPages < 1)
		*totalPages = 1;

	return true;
}
