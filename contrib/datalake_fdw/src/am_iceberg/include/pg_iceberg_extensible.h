/*-------------------------------------------------------------------------
 *
 * pg_iceberg_extensible.h
 *	  Plugin-side ExtensibleNode subtypes used to carry Iceberg AM payloads
 *	  across QD↔QE without touching kernel Node infrastructure.
 *
 * IDENTIFICATION
 *	  contrib/datalake_fdw/src/am_iceberg/include/pg_iceberg_extensible.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef PG_ICEBERG_EXTENSIBLE_H
#define PG_ICEBERG_EXTENSIBLE_H

#include "postgres.h"
#include "nodes/extensible.h"
#include "nodes/pg_list.h"

/* extnodename used by ExtensibleNodeMethods registration. */
#define PG_ICEBERG_VACUUM_DISPATCH_NODE "PgIcebergVacuumDispatch"

/*
 * PgIcebergVacuumDispatchNode
 *
 * QD packs the Iceberg-specific rewrite task list into this node and ships
 * it to QEs via CdbDispatchUtilityStatement().  On the QE side, the node
 * is intercepted by datalake_ProcessUtility and dispatched to
 * pg_iceberg_handle_extensible_utility(), which opens the relation and
 * runs pg_iceberg_execute_rewrite().
 */
typedef struct PgIcebergVacuumDispatchNode
{
	ExtensibleNode	node;
	Oid				relId;		/* target relation */
	List		   *tasks;		/* AM private task list (formerly vacuum_private) */
} PgIcebergVacuumDispatchNode;

extern void pg_iceberg_register_extensible_nodes(void);
extern bool pg_iceberg_handle_extensible_utility(Node *parsetree);

#endif							/* PG_ICEBERG_EXTENSIBLE_H */
