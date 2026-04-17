/*-------------------------------------------------------------------------
 *
 * pg_iceberg_am_handler.h
 * 		Routines for pg_iceberg AM handler.
 *
 * IDENTIFICATION
 *	  contrib/datalake_fdw/src/am_iceberg/include/pg_iceberg_am_handler.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef __PG_ICEBERG_AM_HANDLER_H__
#define __PG_ICEBERG_AM_HANDLER_H__

#include "utils/relcache.h"
#include "pg_iceberg_am.h"

extern bool is_iceberg_rel(Relation rel);

/* DML lifecycle management (called from external DDL/DML hooks) */
extern void pg_iceberg_ext_dml_init(Relation rel, CmdType operation);
extern void pg_iceberg_ext_dml_fini(Relation rel, CmdType operation);

#endif /* __PG_ICEBERG_AM_HANDLER_H__ */
