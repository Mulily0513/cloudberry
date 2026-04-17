/*-------------------------------------------------------------------------
 *
 * pg_iceberg_rewrite_plan.h
 *    Data contracts for Iceberg VACUUM rewrite planning/execution.
 *
 * This header defines:
 *  1) C structs used by AM vacuum flow.
 *  2) Versioned JSON contract (QD->QE plan, QE->QD result).
 *
 * IDENTIFICATION
 *    contrib/datalake_fdw/src/am_iceberg/include/pg_iceberg_rewrite_plan.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef __PG_ICEBERG_REWRITE_PLAN_H__
#define __PG_ICEBERG_REWRITE_PLAN_H__

#include "postgres.h"
#include "lib/stringinfo.h"
#include "nodes/parsenodes.h"

/* ---------- API ---------- */
extern void pg_iceberg_extract_rewrite_fragments_json(const char *json,
													   char **fragments_json);
extern void pg_iceberg_extract_rewrite_result_arrays(const char *json,
													 char **added_fragments_json,
													 char **rewritten_fragments_json);
extern void pg_iceberg_rewrite_append_fragment_json(StringInfo buf,
													FileFragment *fragment,
													int64 fallback_file_size);
extern char *pg_iceberg_rewrite_build_qe_result_json(const char *added_result_json,
													  const char *rewritten_fragments_json);

#endif /* __PG_ICEBERG_REWRITE_PLAN_H__ */
