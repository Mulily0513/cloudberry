/*-------------------------------------------------------------------------
 *
 * cagg_create.c
 *    Continuous Aggregate CREATE handling via ProcessUtility hook.
 *
 *    Intercepts CREATE MATERIALIZED VIEW ... WITH (time_series.continuous)
 *    and orchestrates: query validation, materialization table creation,
 *    three-view setup, trigger installation, and catalog registration.
 *
 * Copyright (c) 2026 HashData Inc.
 * Licensed under Apache License 2.0
 *
 * IDENTIFICATION
 *    contrib/time_series/src/cagg_create.c
 *
 *-------------------------------------------------------------------------
 */
#include "include/time_series.h"

#include "access/xact.h"
#include "catalog/namespace.h"
#include "catalog/pg_aggregate.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_type.h"
#include "commands/defrem.h"
#include "executor/spi.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "optimizer/optimizer.h"
#include "tcop/utility.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "utils/syscache.h"
#include "access/table.h"

#include "cdb/cdbvars.h"

#ifdef FAULT_INJECTOR
#include "utils/faultinjector.h"
#endif

/* Previous ProcessUtility hook */
static ProcessUtility_hook_type prev_ProcessUtility = NULL;

/*
 * Information gathered during CAGG creation.
 */
typedef struct CaggCreateInfo
{
	/* Source table */
	const char *source_schema;
	const char *source_table;
	Oid			source_relid;

	/* User view (what the user typed) */
	const char *user_view_schema;
	const char *user_view_name;

	/* time_bucket info */
	Interval   *bucket_width;
	const char *bucket_column;
	Datum		bucket_origin;		/* optional: origin parameter */
	bool		has_origin;
	Interval   *bucket_offset;		/* optional: offset parameter */
	const char *bucket_timezone;	/* optional: timezone parameter */

	/* Generated names */
	int			cagg_id;
	char		mat_table_name[NAMEDATALEN];
	char		partial_view_name[NAMEDATALEN];
	char		direct_view_name[NAMEDATALEN];

	/* Distribution keys from source table (List of cstring), NIL if random */
	List	   *dist_keys;

	/* Time column type OID */
	Oid			time_type;

	/* The original user query (deparsed for view creation) */
	const char *original_query;

	/* Parsed query */
	Query	   *query;

	/* materialized_only option */
	bool		materialized_only;

	/* WITH NO DATA */
	bool		skip_data;
} CaggCreateInfo;

/* Forward declarations */
static bool cagg_check_continuous_option(List *options, bool *materialized_only);
static void cagg_create(CreateTableAsStmt *stmt, const char *queryString);
static void cagg_validate_query(Query *query, CaggCreateInfo *info);
static bool is_time_bucket_funcexpr(FuncExpr *func);
static void cagg_extract_source_info(Query *query, CaggCreateInfo *info);
static List *cagg_get_dist_keys(Oid relid);
static void cagg_create_mat_table(CaggCreateInfo *info);
static void cagg_create_views(CaggCreateInfo *info);
static void cagg_install_trigger(CaggCreateInfo *info);
static int	cagg_register_catalog(CaggCreateInfo *info);
static bool cagg_apply_materialized_only(const char *cagg_name, bool mat_only);

/* ================================================================
 * Apply materialized_only toggle: rebuild the user view (UNION ALL
 * in real-time mode, plain passthrough in mat-only mode) and update
 * the catalog.
 *
 * Called from the ALTER VIEW hook.  Must be called within an active
 * SPI context (SPI_connect already done).
 *
 * Returns true if the view was found and (potentially) rebuilt;
 * false if cagg_name does not reference a CAGG user view.
 * ================================================================ */

static bool
cagg_apply_materialized_only(const char *cagg_name, bool mat_only)
{
	int			ret;
	bool		isnull;
	int			cagg_id;
	char	   *user_view_schema;
	char	   *user_view_name;
	char	   *mat_table_schema;
	char	   *mat_table_name;
	char	   *direct_view_schema;
	char	   *direct_view_name;
	bool		current_mo;
	char	   *bucket_alias;
	StringInfoData sql;
	MemoryContext caller_cxt = CurrentMemoryContext;
	MemoryContext oldctx;

	/* Step 1: Look up CAGG metadata (schema-qualified) */
	{
		Oid			argtypes[2] = { TEXTOID, TEXTOID };
		Datum		args[2];
		HeapTuple	tup;
		TupleDesc	desc;
		const char *dot;
		char		schema_buf[NAMEDATALEN];
		char		name_buf_local[NAMEDATALEN];

		/* Parse "schema.name" or just "name" (default schema = "public") */
		dot = strchr(cagg_name, '.');
		if (dot)
		{
			int slen = dot - cagg_name;
			if (slen >= NAMEDATALEN) slen = NAMEDATALEN - 1;
			memcpy(schema_buf, cagg_name, slen);
			schema_buf[slen] = '\0';
			strlcpy(name_buf_local, dot + 1, NAMEDATALEN);
		}
		else
		{
			strlcpy(schema_buf, "public", NAMEDATALEN);
			strlcpy(name_buf_local, cagg_name, NAMEDATALEN);
		}

		args[0] = CStringGetTextDatum(schema_buf);
		args[1] = CStringGetTextDatum(name_buf_local);
		ret = SPI_execute_with_args(
			"SELECT cagg_id, user_view_schema, user_view_name, "
			"       mat_table_schema, mat_table_name, "
			"       direct_view_schema, direct_view_name, "
			"       materialized_only "
			"FROM time_series.continuous_agg "
			"WHERE user_view_schema = $1 AND user_view_name = $2",
			2, argtypes, args, NULL, true, 1);

		if (ret != SPI_OK_SELECT || SPI_processed == 0)
			return false;		/* not a CAGG user view */

		tup = SPI_tuptable->vals[0];
		desc = SPI_tuptable->tupdesc;

		cagg_id = DatumGetInt32(SPI_getbinval(tup, desc, 1, &isnull));
		current_mo = DatumGetBool(SPI_getbinval(tup, desc, 8, &isnull));

		/* Copy strings into caller's context (survive SPI_finish/nested SPI) */
		oldctx = MemoryContextSwitchTo(caller_cxt);
		user_view_schema = pstrdup(SPI_getvalue(tup, desc, 2));
		user_view_name = pstrdup(SPI_getvalue(tup, desc, 3));
		mat_table_schema = pstrdup(SPI_getvalue(tup, desc, 4));
		mat_table_name = pstrdup(SPI_getvalue(tup, desc, 5));
		direct_view_schema = pstrdup(SPI_getvalue(tup, desc, 6));
		direct_view_name = pstrdup(SPI_getvalue(tup, desc, 7));
		MemoryContextSwitchTo(oldctx);
	}

	/* Step 2: Short-circuit if already in desired mode */
	if (current_mo == mat_only)
		return true;

	/* Step 3: Look up bucket alias (mat table's first column name) */
	{
		Oid			argtypes[2] = { NAMEOID, NAMEOID };
		Datum		args[2];

		args[0] = DirectFunctionCall1(namein,
									   CStringGetDatum(mat_table_schema));
		args[1] = DirectFunctionCall1(namein,
									   CStringGetDatum(mat_table_name));

		ret = SPI_execute_with_args(
			"SELECT a.attname FROM pg_attribute a "
			"JOIN pg_class pc ON pc.oid = a.attrelid "
			"JOIN pg_namespace n ON n.oid = pc.relnamespace "
			"WHERE n.nspname = $1 AND pc.relname = $2 "
			"AND a.attnum = 1 AND NOT a.attisdropped",
			2, argtypes, args, NULL, true, 1);

		if (ret != SPI_OK_SELECT || SPI_processed == 0)
			ereport(ERROR,
					(errmsg("cagg \"%s\": mat table first column not found",
							cagg_name)));

		oldctx = MemoryContextSwitchTo(caller_cxt);
		bucket_alias = pstrdup(SPI_getvalue(SPI_tuptable->vals[0],
											SPI_tuptable->tupdesc, 1));
		MemoryContextSwitchTo(oldctx);
	}

	/* Step 4: Build CREATE OR REPLACE VIEW SQL */
	oldctx = MemoryContextSwitchTo(caller_cxt);
	initStringInfo(&sql);
	if (mat_only)
	{
		appendStringInfo(&sql,
			"CREATE OR REPLACE VIEW %s.%s AS SELECT * FROM %s.%s",
			quote_identifier(user_view_schema),
			quote_identifier(user_view_name),
			quote_identifier(mat_table_schema),
			quote_identifier(mat_table_name));
	}
	else
	{
		appendStringInfo(&sql,
			"CREATE OR REPLACE VIEW %s.%s AS "
			"SELECT * FROM %s.%s "
			" WHERE %s < time_series.cagg_watermark(%d) "
			"UNION ALL "
			"SELECT * FROM %s.%s "
			" WHERE %s >= time_series.cagg_watermark(%d)",
			quote_identifier(user_view_schema),
			quote_identifier(user_view_name),
			quote_identifier(mat_table_schema),
			quote_identifier(mat_table_name),
			quote_identifier(bucket_alias), cagg_id,
			quote_identifier(direct_view_schema),
			quote_identifier(direct_view_name),
			quote_identifier(bucket_alias), cagg_id);
	}
	MemoryContextSwitchTo(oldctx);

	/* Step 5: Execute the CREATE OR REPLACE VIEW */
	SPI_execute(sql.data, false, 0);

	/* Step 6: Update catalog */
	{
		Oid			argtypes[2] = { BOOLOID, INT4OID };
		Datum		args[2];

		args[0] = BoolGetDatum(mat_only);
		args[1] = Int32GetDatum(cagg_id);
		SPI_execute_with_args(
			"UPDATE time_series.continuous_agg SET materialized_only = $1 "
			"WHERE cagg_id = $2",
			2, argtypes, args, NULL, false, 0);
	}

	pfree(sql.data);
	return true;
}

/* ================================================================
 * ProcessUtility Hook
 * ================================================================ */

static void
ts_cagg_process_utility(PlannedStmt *pstmt,
						const char *queryString,
						bool readOnlyTree,
						ProcessUtilityContext context,
						ParamListInfo params,
						QueryEnvironment *queryEnv,
						DestReceiver *dest,
						QueryCompletion *qc)
{
	Node *parsetree = pstmt->utilityStmt;

	/*
	 * Intercept CREATE MATERIALIZED VIEW ... WITH (time_series.continuous).
	 */
	if (IsA(parsetree, CreateTableAsStmt))
	{
		CreateTableAsStmt *stmt = (CreateTableAsStmt *) parsetree;

		if (stmt->objtype == OBJECT_MATVIEW)
		{
			bool materialized_only = false;

			if (cagg_check_continuous_option(stmt->into->options,
											 &materialized_only))
			{
				/*
				 * Only execute CAGG logic on the coordinator (QD).
				 * On segments the DDL is dispatched by the coordinator.
				 */
				if (Gp_role != GP_ROLE_EXECUTE)
				{
					/*
					 * Extract ONLY this statement from queryString.
					 * In multi-statement batches (psql -c "s1; s2; s3"),
					 * queryString contains ALL statements.  Using it
					 * directly would include unrelated SQL (e.g. INSERT)
					 * in the partial view definition → infinite recursion.
					 *
					 * pstmt->stmt_location/stmt_len give this statement's
					 * boundaries within queryString.
					 */
					const char *stmt_sql;
					int stmt_loc = pstmt->stmt_location;
					int stmt_len = pstmt->stmt_len;

					if (stmt_loc >= 0)
					{
						if (stmt_len > 0)
							stmt_sql = pnstrdup(queryString + stmt_loc, stmt_len);
						else
							stmt_sql = pstrdup(queryString + stmt_loc);
					}
					else
					{
						stmt_sql = queryString;
					}

					cagg_create(stmt, stmt_sql);
					return;
				}
			}
		}
	}

	/*
	 * Intercept TRUNCATE on CAGG source tables.
	 *
	 * CBDB supports neither STATEMENT triggers nor event triggers for
	 * TRUNCATE, so the ROW-level invalidation trigger cannot detect it.
	 * We catch TRUNCATE here in the ProcessUtility hook (runs on QD
	 * before the actual TRUNCATE) and write a full-range L1 entry
	 * {-infinity, +infinity} via SPI.  Then pass through to the standard
	 * handler which does the actual TRUNCATE.  Both happen in the same
	 * transaction — if TRUNCATE fails, the L1 write also rolls back.
	 */
	if (IsA(parsetree, TruncateStmt) && Gp_role != GP_ROLE_EXECUTE)
	{
		TruncateStmt *stmt = (TruncateStmt *) parsetree;
		ListCell   *lc;

		foreach(lc, stmt->relations)
		{
			RangeVar   *rv = lfirst_node(RangeVar, lc);
			Oid			relid = RangeVarGetRelid(rv, NoLock, true);

			if (OidIsValid(relid))
			{
				Oid		argtypes[1] = { OIDOID };
				Datum	args[1];
				int		ret;

				args[0] = ObjectIdGetDatum(relid);

				SPI_connect();
				ret = SPI_execute_with_args(
					"SELECT 1 FROM time_series.continuous_agg "
					"WHERE source_table_oid = $1 LIMIT 1",
					1, argtypes, args, NULL, true, 1);

				if (ret == SPI_OK_SELECT && SPI_processed > 0)
				{
					/* This table has a CAGG — write full-range L1 */
					SPI_execute_with_args(
						"INSERT INTO time_series.cagg_invalidation_log "
						"(source_table_oid, lowest_modified, greatest_modified) "
						"VALUES ($1, '-infinity'::timestamptz, "
						"'infinity'::timestamptz)",
						1, argtypes, args, NULL, false, 0);

					/*
					 * Reset watermark and threshold to -infinity.
					 *
					 * Without this, the real-time view's mat branch
					 * serves stale data (watermark still advanced past
					 * the now-empty range) while the live branch returns
					 * nothing (source is empty).  Resetting ensures the
					 * next query goes through the live branch entirely.
					 */
					SPI_execute_with_args(
						"UPDATE time_series.cagg_watermark "
						"SET watermark = '-infinity'::timestamptz "
						"WHERE cagg_id IN ("
						"  SELECT cagg_id FROM time_series.continuous_agg "
						"  WHERE source_table_oid = $1)",
						1, argtypes, args, NULL, false, 0);

					SPI_execute_with_args(
						"UPDATE time_series.cagg_invalidation_threshold "
						"SET threshold = '-infinity'::timestamptz "
						"WHERE source_table_oid = $1",
						1, argtypes, args, NULL, false, 0);
				}
				SPI_finish();
			}
		}
		/* Fall through to execute the actual TRUNCATE below */
	}

	/*
	 * Block TRUNCATE on CAGG materialization tables.
	 *
	 * Matches TimescaleDB behavior: "cannot TRUNCATE a hypertable
	 * underlying a continuous aggregate".  Truncating the mat table
	 * without resetting watermark causes permanent data loss that
	 * no REFRESH can recover.
	 */
	if (IsA(parsetree, TruncateStmt) && Gp_role != GP_ROLE_EXECUTE)
	{
		TruncateStmt *stmt = (TruncateStmt *) parsetree;
		ListCell   *lc;

		foreach(lc, stmt->relations)
		{
			RangeVar   *rv = lfirst_node(RangeVar, lc);
			Oid			relid = RangeVarGetRelid(rv, NoLock, true);

			if (OidIsValid(relid))
			{
				Datum	args[1];
				int		ret;
				char   *relname = get_rel_name(relid);

				/* Check if relname matches _mat_*_N pattern in time_series schema */
				if (relname && strncmp(relname, "_mat_", 5) == 0)
				{
					MemoryContext caller_cxt = CurrentMemoryContext;
					char   *cagg_name = NULL;

					args[0] = CStringGetTextDatum(relname);

					SPI_connect();
					ret = SPI_execute_with_args(
						"SELECT user_view_name FROM time_series.continuous_agg "
						"WHERE mat_table_name = $1 LIMIT 1",
						1, (Oid[]){ TEXTOID }, args, NULL, true, 1);

					if (ret == SPI_OK_SELECT && SPI_processed > 0)
					{
						char *raw = SPI_getvalue(
							SPI_tuptable->vals[0],
							SPI_tuptable->tupdesc, 1);
						/* Copy to caller context before SPI_finish releases SPI context */
						MemoryContextSwitchTo(caller_cxt);
						cagg_name = raw ? pstrdup(raw) : pstrdup("unknown");
						MemoryContextSwitchTo(SPI_tuptable->tuptabcxt);
					}
					SPI_finish();

					if (cagg_name != NULL)
						ereport(ERROR,
								(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
								 errmsg("cannot TRUNCATE a materialization table "
										"underlying a continuous aggregate"),
								 errhint("TRUNCATE the source table instead, "
										 "or drop and re-create the continuous "
										 "aggregate \"%s\".", cagg_name)));
				}
			}
		}
	}

	/*
	 * Intercept ALTER VIEW cv SET (time_series.materialized_only = bool).
	 *
	 * PG parses namespaced options `ns.name = val` into DefElem with
	 * defnamespace set.  At execute time, PG's transformRelOptions rejects
	 * unknown options on regular views.  We intercept here (before execute)
	 * and:
	 *   1. Locate our option in the DefElem list for AT_SetRelOptions.
	 *   2. Call set_materialized_only(name, bool) via SPI.
	 *   3. Strip our option from the list so PG's validator doesn't choke.
	 *   4. If no other options remain, drop the AT_SetRelOptions cmd.
	 *   5. If no commands remain, skip the standard handler entirely.
	 */
	if (IsA(parsetree, AlterTableStmt) && Gp_role != GP_ROLE_EXECUTE)
	{
		AlterTableStmt *stmt = (AlterTableStmt *) parsetree;

		if (stmt->objtype == OBJECT_VIEW && stmt->relation != NULL)
		{
			Oid			view_oid = RangeVarGetRelid(stmt->relation,
													NoLock, true);

			if (OidIsValid(view_oid))
			{
				ListCell   *cmd_cell;
				List	   *new_cmds = NIL;
				bool		handled = false;
				char	   *view_name = stmt->relation->relname;

				foreach(cmd_cell, stmt->cmds)
				{
					AlterTableCmd *cmd = lfirst_node(AlterTableCmd, cmd_cell);

					if (cmd->subtype == AT_SetRelOptions &&
						cmd->def != NULL && IsA(cmd->def, List))
					{
						List	   *options = (List *) cmd->def;
						List	   *remaining = NIL;
						ListCell   *opt_cell;

						foreach(opt_cell, options)
						{
							DefElem *de = lfirst_node(DefElem, opt_cell);
							bool	is_ours = (de->defnamespace != NULL &&
									strcmp(de->defnamespace, "time_series") == 0 &&
									strcmp(de->defname, "materialized_only") == 0);
							bool	is_cagg = false;
							bool	mo_value = false;

							if (is_ours)
							{
								mo_value = defGetBoolean(de);

								SPI_connect();
								/* cagg_apply_materialized_only does the full
								 * lookup-and-rebuild; returns true if the view
								 * is a CAGG user view. */
								is_cagg = cagg_apply_materialized_only(
									view_name, mo_value);
								SPI_finish();
							}

							/* List manipulation outside SPI context to avoid
							 * allocating remaining list nodes in SPI's temp
							 * memory context, which gets freed on SPI_finish. */
							if (is_ours && is_cagg)
								handled = true;
							else
								remaining = lappend(remaining, de);
						}

						if (remaining != NIL)
						{
							cmd->def = (Node *) remaining;
							new_cmds = lappend(new_cmds, cmd);
						}
						/* else: drop this cmd entirely */
					}
					else
					{
						new_cmds = lappend(new_cmds, cmd);
					}
				}

				if (handled)
				{
					stmt->cmds = new_cmds;
					/* If nothing left to do, skip standard handler */
					if (new_cmds == NIL)
						return;
				}
			}
		}
	}

	/* Pass through to the previous hook or standard processing */
	if (prev_ProcessUtility)
		prev_ProcessUtility(pstmt, queryString, readOnlyTree, context,
							params, queryEnv, dest, qc);
	else
		standard_ProcessUtility(pstmt, queryString, readOnlyTree, context,
								params, queryEnv, dest, qc);

	/*
	 * POST-execution hook: sync CAGG catalog after ALTER VIEW RENAME.
	 *
	 * PG renames the view in pg_class but our continuous_agg catalog
	 * stores user_view_name as text.  We update it here AFTER the
	 * standard handler succeeds (so the pg_class rename is committed).
	 *
	 * We only care about OBJECT_VIEW renames on the QD.
	 */
	if (IsA(parsetree, RenameStmt) && Gp_role != GP_ROLE_EXECUTE)
	{
		RenameStmt *rstmt = (RenameStmt *) parsetree;

		if (rstmt->renameType == OBJECT_VIEW &&
			rstmt->relation != NULL &&
			rstmt->newname != NULL)
		{
			const char *old_name = rstmt->relation->relname;
			const char *old_schema = rstmt->relation->schemaname;
			Oid			argtypes[3] = { TEXTOID, TEXTOID, TEXTOID };
			Datum		args[3];
			int			ret;

			if (old_schema == NULL)
				old_schema = "public";

			args[0] = CStringGetTextDatum(rstmt->newname);
			args[1] = CStringGetTextDatum(old_schema);
			args[2] = CStringGetTextDatum(old_name);

			SPI_connect();
			ret = SPI_execute_with_args(
				"UPDATE time_series.continuous_agg "
				"SET user_view_name = $1 "
				"WHERE user_view_schema = $2 AND user_view_name = $3",
				3, argtypes, args, NULL, false, 0);

			if (ret == SPI_OK_UPDATE && SPI_processed > 0)
				elog(NOTICE, "continuous aggregate renamed: \"%s\" -> \"%s\"",
					 old_name, rstmt->newname);
			SPI_finish();
		}
	}
}

/* ================================================================
 * Check WITH options for time_series.continuous
 * ================================================================ */

static bool
cagg_check_continuous_option(List *options, bool *materialized_only)
{
	ListCell   *lc;
	bool		found_continuous = false;

	if (options == NIL)
		return false;

	foreach(lc, options)
	{
		DefElem *def = lfirst_node(DefElem, lc);

		if (def->defnamespace == NULL ||
			strcmp(def->defnamespace, "time_series") != 0)
			continue;

		if (strcmp(def->defname, "continuous") == 0)
			found_continuous = true;
		else if (strcmp(def->defname, "materialized_only") == 0)
			*materialized_only = defGetBoolean(def);
	}

	return found_continuous;
}

/* ================================================================
 * Main CREATE flow
 * ================================================================ */

static void
cagg_create(CreateTableAsStmt *stmt, const char *queryString)
{
	CaggCreateInfo info;
	Query		   *query;
	IntoClause	   *into = stmt->into;

	memset(&info, 0, sizeof(info));

	/* Parse the user view name */
	info.user_view_schema = into->rel->schemaname ?
							into->rel->schemaname : "public";
	info.user_view_name = into->rel->relname;

	/* Check materialized_only */
	cagg_check_continuous_option(into->options, &info.materialized_only);

	/* Check WITH NO DATA */
	info.skip_data = into->skipData;

	/*
	 * By the time we reach ProcessUtility, stmt->query has already been
	 * through parse_analyze and is a Query node.
	 */
	query = castNode(Query, stmt->query);
	info.query = query;

	/* Validate the query structure */
	cagg_validate_query(query, &info);

	/* Extract source table info */
	cagg_extract_source_info(query, &info);

	/* Get distribution keys from source table */
	info.dist_keys = cagg_get_dist_keys(info.source_relid);

	/*
	 * Extract the SELECT portion from the original query string.
	 * The queryString contains the full CREATE MATERIALIZED VIEW ... AS SELECT ...
	 * We search backwards from "SELECT" to find the right AS boundary.
	 */
	{
		const char *p = queryString;
		const char *select_start = NULL;

		/*
		 * Scan for "SELECT" keyword (case-insensitive).
		 * The first SELECT in the string is the one we want
		 * (CREATE MATERIALIZED VIEW ... AS SELECT ...).
		 */
		while (*p)
		{
			if (pg_strncasecmp(p, "SELECT", 6) == 0)
			{
				/* Make sure it's a word boundary (not inside identifier) */
				if (p == queryString || !isalnum((unsigned char) *(p - 1)))
				{
					select_start = p;
					break;
				}
			}
			p++;
		}

		if (select_start == NULL)
			ereport(ERROR,
					(errmsg("could not extract SELECT from CREATE MATERIALIZED VIEW statement")));

		/* Strip trailing semicolons, whitespace, and WITH NO DATA */
		{
			char *q = pstrdup(select_start);
			int len = strlen(q);

			/* Strip trailing whitespace and semicolons */
			while (len > 0 && (q[len - 1] == ';' || q[len - 1] == ' ' ||
							   q[len - 1] == '\n' || q[len - 1] == '\t'))
				q[--len] = '\0';

			/* Strip trailing "WITH NO DATA" (case-insensitive) */
			if (len >= 12 &&
				pg_strncasecmp(q + len - 12, "WITH NO DATA", 12) == 0)
			{
				len -= 12;
				while (len > 0 && (q[len - 1] == ' ' || q[len - 1] == '\n'))
					len--;
				q[len] = '\0';
			}

			info.original_query = q;
		}
	}

	SPI_connect();

	/* 1. Register in catalog (to get cagg_id for naming) */
	info.cagg_id = cagg_register_catalog(&info);

	/* Generate internal names */
	snprintf(info.mat_table_name, NAMEDATALEN,
			 "_mat_%s_%d", info.user_view_name, info.cagg_id);
	snprintf(info.partial_view_name, NAMEDATALEN,
			 "_partial_view_%d", info.cagg_id);
	snprintf(info.direct_view_name, NAMEDATALEN,
			 "_direct_view_%d", info.cagg_id);

	/* Update catalog with generated names */
	{
		Oid		upd_argtypes[4] = { TEXTOID, TEXTOID, TEXTOID, INT4OID };
		Datum	upd_args[4];

		upd_args[0] = CStringGetTextDatum(info.mat_table_name);
		upd_args[1] = CStringGetTextDatum(info.partial_view_name);
		upd_args[2] = CStringGetTextDatum(info.direct_view_name);
		upd_args[3] = Int32GetDatum(info.cagg_id);

		SPI_execute_with_args(
			"UPDATE time_series.continuous_agg SET "
			"mat_table_schema = 'time_series', "
			"mat_table_name = $1, "
			"partial_view_schema = 'time_series', "
			"partial_view_name = $2, "
			"direct_view_schema = 'time_series', "
			"direct_view_name = $3 "
			"WHERE cagg_id = $4",
			4, upd_argtypes, upd_args, NULL, false, 0);
	}

	/* 2. Create materialization table */
	cagg_create_mat_table(&info);

	/* 3. Create the three views */
	cagg_create_views(&info);

	/* 4. Install invalidation trigger on source table */
	SIMPLE_FAULT_INJECTOR("cagg_create_before_trigger_install");
	cagg_install_trigger(&info);

	/* 5. Initialize watermark for this CAGG — one row per segment.
	 *
	 * cagg_watermark is DISTRIBUTED RANDOMLY.  We need every segment to
	 * have a local row so the trigger's threshold computation (local heap
	 * scan) can find it.  Dispatch _cagg_init_segment_watermark() via
	 * gp_dist_random('gp_id') which runs once on each segment.
	 */
	{
		Oid		argtypes[1] = { INT4OID };
		Datum	args[1];

		args[0] = Int32GetDatum(info.cagg_id);
		SPI_execute_with_args(
			"SELECT time_series._cagg_init_segment_watermark($1) "
			"FROM gp_dist_random('gp_id')",
			1, argtypes, args, NULL, true, 0);
	}

	/*
	 * 5b. Initialize invalidation threshold for this source table.
	 *
	 * cagg_invalidation_threshold stores MAX(watermark) per source,
	 * pre-computed so the trigger only needs one heap scan.  Only create
	 * rows if this is the FIRST CAGG on this source (avoid duplicates).
	 */
	{
		Oid		argtypes[1] = { OIDOID };
		Datum	args[1];
		int		ret;

		args[0] = ObjectIdGetDatum(info.source_relid);
		ret = SPI_execute_with_args(
			"SELECT 1 FROM time_series.cagg_invalidation_threshold "
			"WHERE source_table_oid = $1 LIMIT 1",
			1, argtypes, args, NULL, true, 1);

		if (ret == SPI_OK_SELECT && SPI_processed == 0)
		{
			/* First CAGG on this source → init threshold per segment */
			SPI_execute_with_args(
				"SELECT time_series._cagg_init_segment_threshold($1) "
				"FROM gp_dist_random('gp_id')",
				1, argtypes, args, NULL, true, 0);
		}
	}

	/* 6. Register bucket function metadata */
	{
		Oid		bf_argtypes[6] = { INT4OID, INTERVALOID, OIDOID,
								   TIMESTAMPTZOID, INTERVALOID, TEXTOID };
		Datum	bf_args[6];
		char	bf_nulls[6] = { ' ', ' ', ' ', ' ', ' ', ' ' };

		bf_args[0] = Int32GetDatum(info.cagg_id);
		bf_args[1] = IntervalPGetDatum(info.bucket_width);
		bf_args[2] = ObjectIdGetDatum(info.time_type);

		if (info.has_origin)
			bf_args[3] = info.bucket_origin;
		else
			bf_nulls[3] = 'n';

		if (info.bucket_offset != NULL)
			bf_args[4] = IntervalPGetDatum(info.bucket_offset);
		else
			bf_nulls[4] = 'n';

		if (info.bucket_timezone != NULL)
			bf_args[5] = CStringGetTextDatum(info.bucket_timezone);
		else
			bf_nulls[5] = 'n';

		SPI_execute_with_args(
			"INSERT INTO time_series.cagg_bucket_function "
			"(cagg_id, bucket_width, time_type, bucket_origin, "
			"bucket_offset, bucket_timezone) "
			"VALUES ($1, $2, $3, $4, $5, $6)",
			6, bf_argtypes, bf_args, bf_nulls, false, 0);
	}

	/*
	 * cagg_invalidation_threshold is initialized in step 5b above.
	 * REFRESH updates it to MAX(watermark) so the trigger can do a
	 * single-table heap scan instead of joining continuous_agg + cagg_watermark.
	 */

	SPI_finish();

	ereport(NOTICE,
			(errmsg("continuous aggregate \"%s\" successfully created",
					info.user_view_name),
			 errdetail("Materialization table: time_series.%s",
					   info.mat_table_name)));
}

/* ================================================================
 * Query Validation
 * ================================================================ */

static void
cagg_validate_query(Query *query, CaggCreateInfo *info)
{
	ListCell   *lc;
	bool		found_bucket = false;
	int			bucket_count = 0;

	/* Must be a SELECT */
	if (query->commandType != CMD_SELECT)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("continuous aggregate query must be a SELECT")));

	/* Must have GROUP BY */
	if (query->groupClause == NIL)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("continuous aggregate query must have a GROUP BY clause with time_bucket")));

	/*
	 * Reject JOIN, CTE, and subquery FROM-items by scanning the rtable BEFORE
	 * any code that calls get_attname() on the rtable.  A CTE RTE has relid=0,
	 * so doing this later would crash with "cache lookup failed for attribute".
	 * Looking only at jointree->fromlist length is not enough: an explicit
	 * SQL JOIN (`FROM a JOIN b ON ...`) is parsed as a single JoinExpr in
	 * fromlist (length 1) but adds an RTE_JOIN entry to the rtable.
	 */
	{
		ListCell   *rtlc;
		int			rel_count = 0;

		foreach(rtlc, query->rtable)
		{
			RangeTblEntry *rte = (RangeTblEntry *) lfirst(rtlc);

			switch (rte->rtekind)
			{
				case RTE_RELATION:
					rel_count++;
					break;
				case RTE_JOIN:
					ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							 errmsg("continuous aggregate does not support JOIN")));
					break;
				case RTE_CTE:
					ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							 errmsg("continuous aggregate does not support WITH (CTE)")));
					break;
				case RTE_SUBQUERY:
					ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							 errmsg("continuous aggregate does not support subqueries in FROM")));
					break;
				default:
					/* RTE_FUNCTION, RTE_VALUES, etc. — not a base table */
					ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							 errmsg("continuous aggregate FROM clause must reference a base table")));
					break;
			}
		}

		if (rel_count > 1)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("continuous aggregate does not support JOIN")));
	}

	/*
	 * Must have time_bucket() in GROUP BY clause (not just SELECT list).
	 * TimescaleDB enforces this same rule — time_bucket defines the
	 * materialization granularity, so it must be a GROUP BY key.
	 */
	foreach(lc, query->groupClause)
	{
		SortGroupClause *sgc = (SortGroupClause *) lfirst(lc);
		TargetEntry *tle = get_sortgroupclause_tle(sgc, query->targetList);

		if (tle && IsA(tle->expr, FuncExpr))
		{
			FuncExpr *func = (FuncExpr *) tle->expr;

			if (is_time_bucket_funcexpr(func))
			{
				bucket_count++;

				if (bucket_count > 1)
					ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							 errmsg("continuous aggregate must have exactly one time_bucket function")));

				found_bucket = true;

				/*
				 * Extract bucket_width: first argument should be an interval
				 * constant.
				 */
				Node *width_arg = linitial(func->args);

				if (IsA(width_arg, Const))
				{
					Const *c = (Const *) width_arg;

					if (c->consttype == INTERVALOID && !c->constisnull)
						info->bucket_width = DatumGetIntervalP(c->constvalue);
				}

				if (info->bucket_width == NULL)
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
							 errmsg("time_bucket width must be a constant interval")));

				/* Reject zero or negative bucket width */
				{
					Interval *bw = info->bucket_width;
					if (bw->month == 0 && bw->day == 0 && bw->time == 0)
						ereport(ERROR,
								(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
								 errmsg("continuous aggregate bucket width must not be zero")));
					if (bw->month < 0 || bw->day < 0 || bw->time < 0)
						ereport(ERROR,
								(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
								 errmsg("continuous aggregate bucket width must be positive")));
				}

				/*
				 * Extract bucket_column: second argument should be a Var
				 * referencing the source table column.
				 */
				if (list_length(func->args) >= 2)
				{
					Node *col_arg = lsecond(func->args);

					if (IsA(col_arg, Var))
					{
						Var *var = (Var *) col_arg;
						RangeTblEntry *rte = list_nth(query->rtable,
													  var->varno - 1);
						info->bucket_column =
							pstrdup(get_attname(rte->relid, var->varattno,
												false));
						info->time_type = var->vartype;
					}
				}

				if (info->bucket_column == NULL)
					ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							 errmsg("could not determine bucket column from time_bucket() call")));

				/*
				 * Extract optional advanced parameters (3rd+ args).
				 */
				{
					ListCell *lc2;
					int		  argidx = 0;

					foreach(lc2, func->args)
					{
						Node *arg = lfirst(lc2);

						argidx++;
						if (argidx <= 2)
							continue;

						if (IsA(arg, Const))
						{
							Const *c = (Const *) arg;

							if (c->constisnull)
								continue;

							if (c->consttype == TEXTOID)
							{
								info->bucket_timezone =
									pstrdup(TextDatumGetCString(c->constvalue));
							}
							else if (c->consttype == INTERVALOID)
							{
								info->bucket_offset =
									DatumGetIntervalP(c->constvalue);
							}
							else if (c->consttype == TIMESTAMPTZOID ||
									 c->consttype == TIMESTAMPOID ||
									 c->consttype == DATEOID)
							{
								info->bucket_origin = c->constvalue;
								info->has_origin = true;
							}
						}
					}
				}
			}
		}
	}

	if (!found_bucket)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("continuous aggregate view must include a valid time_bucket function in GROUP BY")));

	/* --- Reject unsupported syntax --- */

	/* LIMIT / OFFSET */
	if (query->limitCount || query->limitOffset)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("continuous aggregate does not support LIMIT/OFFSET")));

	/* Subqueries */
	if (query->hasSubLinks)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("continuous aggregate does not support subqueries")));

	/* Window functions */
	if (query->hasWindowFuncs)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("continuous aggregate does not support window functions")));

	/* HAVING is allowed (V1 supports it) */

	/* DISTINCT / DISTINCT ON */
	if (query->distinctClause)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("continuous aggregate does not support DISTINCT")));

	/* CTE (WITH clause) */
	if (query->cteList)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("continuous aggregate does not support CTEs (WITH clause)")));

	/* GROUPING SETS / ROLLUP / CUBE */
	if (query->groupingSets)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("continuous aggregate does not support GROUPING SETS, ROLLUP, or CUBE")));

	/* UNION / EXCEPT / INTERSECT */
	if (query->setOperations)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("continuous aggregate does not support UNION, EXCEPT, or INTERSECT")));

	/* FOR UPDATE / FOR SHARE */
	if (query->rowMarks)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("continuous aggregate does not support FOR UPDATE/SHARE")));

	/* TABLESAMPLE / FROM ONLY / RLS — check each RTE */
	{
		ListCell *rlc;

		foreach(rlc, query->rtable)
		{
			RangeTblEntry *rte = lfirst_node(RangeTblEntry, rlc);

			if (rte->tablesample)
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("TABLESAMPLE is not supported in continuous aggregate")));

			/*
			 * FROM ONLY check is skipped: in CBDB regular tables have
			 * inh=false by default (not inherited), so checking !rte->inh
			 * would false-positive on every normal table. FROM ONLY is
			 * only relevant for hypertables/inheritance, which V1 doesn't
			 * use as source tables.
			 */

			if (rte->rtekind == RTE_RELATION && rte->relid != InvalidOid)
			{
				Relation	rel = table_open(rte->relid, NoLock);
				bool		has_rls = rel->rd_rel->relrowsecurity;

				table_close(rel, NoLock);
				if (has_rls)
					ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							 errmsg("row-level security is not supported by continuous aggregate")));
			}
		}
	}

	/*
	 * Reject STABLE/VOLATILE expressions outside aggregates.
	 *
	 * A STABLE function (e.g., timestamptz::timestamp) depends on session
	 * settings like timezone.  If REFRESH runs in one timezone and the user
	 * queries in another, the mat branch and live branch of the UNION ALL
	 * view would compute different values for the same data — causing data
	 * loss.  Same behavior as TimescaleDB pre-PG17.
	 *
	 * We walk each target list entry and GROUP BY expression.  Aggref nodes
	 * are skipped (aggregates are evaluated fresh by both branches).
	 */
	{
		ListCell *tlc;

		foreach(tlc, query->targetList)
		{
			TargetEntry *tle = (TargetEntry *) lfirst(tlc);

			/* Skip resjunk entries (internal to GROUP BY, not projected) */
			if (tle->resjunk)
				continue;

			/* Skip pure Aggref — aggregates are fine */
			if (IsA(tle->expr, Aggref))
				continue;

			/* For non-aggregate expressions, check for mutable functions.
			 * contain_mutable_functions() walks the expression tree and
			 * returns true if any STABLE or VOLATILE function is found.
			 * It DOES walk into Aggref args, but since we already skip
			 * pure Aggref nodes above, this only triggers for non-aggregate
			 * target entries (e.g. time_bucket(...)::timestamp). */
			if (contain_mutable_functions((Node *) tle->expr))
			{
				/* Allow time_bucket itself (IMMUTABLE), but reject if
				 * it's wrapped in a mutable function (e.g. ::timestamp) */
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("continuous aggregate SELECT expression contains "
								"a mutable (non-IMMUTABLE) function"),
						 errhint("Expressions like '::timestamp' depend on session "
								 "settings (e.g. timezone) and would produce "
								 "inconsistent results between materialized and "
								 "real-time branches. Use timestamptz instead.")));
			}
		}
	}
}

/* ================================================================
 * Helper: check if a FuncExpr is time_bucket()
 * ================================================================ */

static bool
is_time_bucket_funcexpr(FuncExpr *func)
{
	char *funcname;

	funcname = get_func_name(func->funcid);
	if (funcname == NULL)
		return false;

	return strcmp(funcname, "time_bucket") == 0;
}

/* ================================================================
 * Extract source table info from query
 * ================================================================ */

static void
cagg_extract_source_info(Query *query, CaggCreateInfo *info)
{
	RangeTblEntry *rte;

	if (list_length(query->rtable) < 1)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("continuous aggregate query must reference a table")));

	rte = linitial(query->rtable);

	if (rte->rtekind != RTE_RELATION)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("continuous aggregate source must be a table")));

	/*
	 * Only allow plain tables (RELKIND_RELATION).  Materialized views,
	 * foreign tables, partitioned tables, etc. are rejected because:
	 *   - Triggers cannot fire on matviews (no INSERT path)
	 *   - Foreign tables have no local storage for trigger-based invalidation
	 *   - Partitioned tables need special handling (V2+)
	 */
	{
		char relkind = get_rel_relkind(rte->relid);

		if (relkind != RELKIND_RELATION)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("\"%s\" is not a plain table",
							get_rel_name(rte->relid)),
					 errdetail("Continuous aggregate source must be a plain table, "
							   "not a %s.",
							   relkind == RELKIND_MATVIEW ? "materialized view" :
							   relkind == RELKIND_VIEW ? "view" :
							   relkind == RELKIND_FOREIGN_TABLE ? "foreign table" :
							   relkind == RELKIND_PARTITIONED_TABLE ? "partitioned table" :
							   "non-table relation")));
	}

	info->source_relid = rte->relid;
	info->source_schema = get_namespace_name(get_rel_namespace(rte->relid));
	info->source_table = get_rel_name(rte->relid);
}

/* ================================================================
 * Get distribution keys from source table
 *
 * Returns a List of cstring column names (allocated in the caller's
 * memory context) representing the source table's distribution key
 * columns, in distkey order.  Returns NIL if the source is distributed
 * randomly or replicated.
 * ================================================================ */

static List *
cagg_get_dist_keys(Oid relid)
{
	int			ret;
	Oid			argtypes[1] = { OIDOID };
	Datum		args[1];
	MemoryContext caller_cxt = CurrentMemoryContext;
	List	   *result = NIL;

	args[0] = ObjectIdGetDatum(relid);

	SPI_connect();
	ret = SPI_execute_with_args(
		"SELECT a.attname "
		"FROM gp_distribution_policy dp "
		"JOIN LATERAL unnest(dp.distkey) WITH ORDINALITY AS u(attnum, ord) ON TRUE "
		"JOIN pg_attribute a ON a.attrelid = dp.localoid AND a.attnum = u.attnum "
		"WHERE dp.localoid = $1 "
		"ORDER BY u.ord",
		1, argtypes, args, NULL, true, 0);

	if (ret == SPI_OK_SELECT && SPI_processed > 0)
	{
		uint64	i;

		for (i = 0; i < SPI_processed; i++)
		{
			char	   *val = SPI_getvalue(SPI_tuptable->vals[i],
										  SPI_tuptable->tupdesc, 1);
			MemoryContext oldcxt;
			char	   *copied;

			if (!val)
				continue;

			/* Copy into caller's context so both the string and the list
			 * cells survive SPI_finish(). */
			oldcxt = MemoryContextSwitchTo(caller_cxt);
			copied = pstrdup(val);
			result = lappend(result, copied);
			MemoryContextSwitchTo(oldcxt);
		}
	}

	SPI_finish();
	return result;
}

/* ================================================================
 * Create materialization table
 * ================================================================ */

static void
cagg_create_mat_table(CaggCreateInfo *info)
{
	StringInfoData sql;
	ListCell   *lc;
	bool		first = true;

	initStringInfo(&sql);

	appendStringInfo(&sql, "CREATE TABLE time_series.%s (",
					 info->mat_table_name);

	/* Build column list from query target list */
	foreach(lc, info->query->targetList)
	{
		TargetEntry *tle = lfirst_node(TargetEntry, lc);
		Oid			typid = exprType((Node *) tle->expr);
		const char *colname;

		if (tle->resjunk)
			continue;

		colname = tle->resname ? tle->resname :
				  psprintf("col_%d", tle->resno);

		if (!first)
			appendStringInfoString(&sql, ", ");
		first = false;

		appendStringInfo(&sql, "%s %s",
						 quote_identifier(colname),
						 format_type_be(typid));
	}

	appendStringInfoChar(&sql, ')');

	/*
	 * Distribution strategy for materialization table (co-location first):
	 *   Strategy 1: If ALL source distribution-key columns appear in the
	 *               target list as plain Var references, use exactly those
	 *               columns (same order).  REFRESH DML is then Motion-free
	 *               because partial aggregates can be written locally.
	 *   Strategy 2: Otherwise pick the FIRST GROUP BY column that is NOT a
	 *               time_bucket() expression.  This keeps data co-located by
	 *               tag dimension and avoids the hot-segment pathology of
	 *               hashing on time buckets.
	 *   Strategy 3: Fallback when GROUP BY contains only time_bucket().
	 *               Distribute by the bucket column — the result set is tiny
	 *               (one row per bucket) so skew is irrelevant, and downstream
	 *               hierarchical CAGGs can co-locate on bucket.
	 */
	{
		List	   *chosen_cols = NIL;	/* list of cstring column names */
		ListCell   *lc;

		/* Strategy 1: match ALL source dist keys */
		if (info->dist_keys != NIL)
		{
			bool		all_matched = true;
			List	   *matched = NIL;

			foreach(lc, info->dist_keys)
			{
				const char *srckey = (const char *) lfirst(lc);
				const char *matched_alias = NULL;
				ListCell   *tlc;

				foreach(tlc, info->query->targetList)
				{
					TargetEntry *tle = lfirst_node(TargetEntry, tlc);
					Var		   *v;
					RangeTblEntry *r;
					const char *srcname;

					if (tle->resjunk || !IsA(tle->expr, Var))
						continue;

					v = (Var *) tle->expr;
					r = list_nth(info->query->rtable, v->varno - 1);
					srcname = get_attname(r->relid, v->varattno, true);
					if (srcname && strcmp(srcname, srckey) == 0)
					{
						matched_alias = tle->resname ? tle->resname : srcname;
						break;
					}
				}

				if (matched_alias == NULL)
				{
					all_matched = false;
					break;
				}
				matched = lappend(matched, (void *) matched_alias);
			}

			if (all_matched)
				chosen_cols = matched;
		}

		/* Strategy 2: first non-time_bucket GROUP BY column */
		if (chosen_cols == NIL)
		{
			foreach(lc, info->query->groupClause)
			{
				SortGroupClause *sgc = (SortGroupClause *) lfirst(lc);
				TargetEntry *tle = get_sortgroupclause_tle(sgc,
														   info->query->targetList);
				bool	is_bucket = false;

				if (tle == NULL || tle->resname == NULL)
					continue;

				if (IsA(tle->expr, FuncExpr) &&
					is_time_bucket_funcexpr((FuncExpr *) tle->expr))
					is_bucket = true;

				if (!is_bucket)
				{
					chosen_cols = list_make1((void *) tle->resname);
					break;
				}
			}
		}

		/* Strategy 3: GROUP BY is only time_bucket — use the bucket column */
		if (chosen_cols == NIL)
		{
			TargetEntry *first_tle = linitial(info->query->targetList);
			const char *bucket_alias = first_tle->resname ?
									   first_tle->resname : "bucket";

			chosen_cols = list_make1((void *) bucket_alias);
		}

		appendStringInfoString(&sql, " DISTRIBUTED BY (");
		{
			bool	first_col = true;

			foreach(lc, chosen_cols)
			{
				const char *col = (const char *) lfirst(lc);

				if (!first_col)
					appendStringInfoString(&sql, ", ");
				first_col = false;
				appendStringInfoString(&sql, quote_identifier(col));
			}
		}
		appendStringInfoChar(&sql, ')');
	}

	SPI_execute(sql.data, false, 0);

	/*
	 * Create index on bucket column.  In the materialization table the bucket
	 * column uses the alias from the user's SELECT (typically "bucket"), not
	 * the original source column name.  Find it from the first target entry.
	 */
	{
		TargetEntry *first_tle = linitial(info->query->targetList);
		const char *idx_col = first_tle->resname ? first_tle->resname : "bucket";

		resetStringInfo(&sql);
		appendStringInfo(&sql,
			"CREATE INDEX ON time_series.%s (%s)",
			info->mat_table_name,
			quote_identifier(idx_col));
		SPI_execute(sql.data, false, 0);
	}

	pfree(sql.data);
}

/* ================================================================
 * Create the three views
 * ================================================================ */

static void
cagg_create_views(CaggCreateInfo *info)
{
	StringInfoData sql;

	initStringInfo(&sql);

	/*
	 * View 1: Partial View — same as user's SELECT, used by REFRESH.
	 * We use the original_query string (the user's SELECT).
	 */
	resetStringInfo(&sql);
	appendStringInfo(&sql,
		"CREATE VIEW time_series.%s AS %s",
		info->partial_view_name,
		info->original_query);
	SPI_execute(sql.data, false, 0);

	/*
	 * View 2: Direct View — same as user's SELECT, used for real-time branch.
	 */
	resetStringInfo(&sql);
	appendStringInfo(&sql,
		"CREATE VIEW time_series.%s AS %s",
		info->direct_view_name,
		info->original_query);
	SPI_execute(sql.data, false, 0);

	/*
	 * View 3: User View.
	 *
	 * Two modes based on materialized_only:
	 *   - false (real-time): UNION ALL of mat table (bucket < watermark)
	 *     and direct view (bucket >= watermark).  Users see newly inserted
	 *     data immediately via the live-aggregated branch, without REFRESH.
	 *   - true (materialized-only): simple passthrough over the mat table.
	 *
	 * The bucket alias is the user-chosen resname for time_bucket() in
	 * their SELECT list (first target entry).
	 */
	{
		TargetEntry *first_tle = linitial_node(TargetEntry,
											   info->query->targetList);
		const char *bucket_alias = first_tle->resname ?
								   first_tle->resname : "bucket";

		resetStringInfo(&sql);
		if (info->materialized_only)
		{
			appendStringInfo(&sql,
				"CREATE VIEW %s.%s AS SELECT * FROM time_series.%s",
				quote_identifier(info->user_view_schema),
				quote_identifier(info->user_view_name),
				info->mat_table_name);
		}
		else
		{
			appendStringInfo(&sql,
				"CREATE VIEW %s.%s AS "
				"SELECT * FROM time_series.%s "
				"WHERE %s < time_series.cagg_watermark(%d) "
				"UNION ALL "
				"SELECT * FROM time_series.%s "
				"WHERE %s >= time_series.cagg_watermark(%d)",
				quote_identifier(info->user_view_schema),
				quote_identifier(info->user_view_name),
				info->mat_table_name,
				quote_identifier(bucket_alias), info->cagg_id,
				info->direct_view_name,
				quote_identifier(bucket_alias), info->cagg_id);
		}
		SPI_execute(sql.data, false, 0);
	}

	pfree(sql.data);
}

/* ================================================================
 * Install invalidation trigger on source table
 * ================================================================ */

static void
cagg_install_trigger(CaggCreateInfo *info)
{
	StringInfoData sql;

	initStringInfo(&sql);

	/*
	 * Check if trigger already exists (shared trigger for multiple CAGGs
	 * on the same source table).
	 */
	{
		Oid		argtypes[1] = { OIDOID };
		Datum	args[1];
		int		ret;

		args[0] = ObjectIdGetDatum(info->source_relid);

		ret = SPI_execute_with_args(
			"SELECT 1 FROM pg_trigger t "
			"WHERE t.tgrelid = $1 "
			"AND t.tgname = 'ts_cagg_invalidation_trigger'",
			1, argtypes, args, NULL, true, 1);

		if (ret == SPI_OK_SELECT && SPI_processed > 0)
		{
			pfree(sql.data);
			return;		/* trigger already installed */
		}
	}

	appendStringInfo(&sql,
		"CREATE TRIGGER ts_cagg_invalidation_trigger "
		"AFTER INSERT OR UPDATE OR DELETE ON %s.%s "
		"FOR EACH ROW EXECUTE FUNCTION time_series.cagg_invalidation_trigfn()",
		quote_identifier(info->source_schema),
		quote_identifier(info->source_table));

	SPI_execute(sql.data, false, 0);

	/*
	 * TRUNCATE invalidation is handled by the ProcessUtility hook
	 * (ts_cagg_process_utility) which intercepts TruncateStmt and writes
	 * a full-range {-infinity, +infinity} L1 entry before the actual
	 * TRUNCATE executes.  No STATEMENT trigger needed.
	 */

	pfree(sql.data);
}

/* ================================================================
 * Register CAGG in catalog
 * ================================================================ */

static int
cagg_register_catalog(CaggCreateInfo *info)
{
	Oid		argtypes[8] = { TEXTOID, TEXTOID, TEXTOID, TEXTOID,
							OIDOID, INTERVALOID, NAMEOID, BOOLOID };
	Datum	args[8];
	int		ret;
	int		cagg_id;

	args[0] = CStringGetTextDatum(info->user_view_schema);
	args[1] = CStringGetTextDatum(info->user_view_name);
	args[2] = CStringGetTextDatum(info->source_schema);
	args[3] = CStringGetTextDatum(info->source_table);
	args[4] = ObjectIdGetDatum(info->source_relid);
	args[5] = IntervalPGetDatum(info->bucket_width);
	args[6] = DirectFunctionCall1(namein,
								  CStringGetDatum(info->bucket_column));
	args[7] = BoolGetDatum(info->materialized_only);

	ret = SPI_execute_with_args(
		"INSERT INTO time_series.continuous_agg "
		"(user_view_schema, user_view_name, source_table_schema, "
		"source_table_name, source_table_oid, bucket_width, "
		"bucket_column, materialized_only) "
		"VALUES ($1, $2, $3, $4, $5, $6, $7, $8) "
		"RETURNING cagg_id",
		8, argtypes, args, NULL, false, 0);

	if (ret != SPI_OK_INSERT_RETURNING || SPI_processed != 1)
		ereport(ERROR,
				(errmsg("failed to register continuous aggregate in catalog")));

	{
		bool isnull;
		cagg_id = DatumGetInt32(SPI_getbinval(SPI_tuptable->vals[0],
											  SPI_tuptable->tupdesc, 1,
											  &isnull));
	}

	return cagg_id;
}

/* ================================================================
 * Hook registration (called from _PG_init)
 * ================================================================ */

void
ht_cagg_init(void)
{
	prev_ProcessUtility = ProcessUtility_hook;
	ProcessUtility_hook = ts_cagg_process_utility;
}
