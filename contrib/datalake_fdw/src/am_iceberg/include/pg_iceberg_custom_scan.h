/*-------------------------------------------------------------------------
 *
 * pg_iceberg_custom_scan.h
 *	  CustomScan provider that replaces plain SeqScan on Iceberg tables.
 *
 * IDENTIFICATION
 *	  contrib/datalake_fdw/src/am_iceberg/include/pg_iceberg_custom_scan.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef __PG_ICEBERG_CUSTOM_SCAN_H__
#define __PG_ICEBERG_CUSTOM_SCAN_H__

#include "postgres.h"

/*
 * Registers the CustomScanMethods and installs the planner_hook that
 * rewrites SeqScan on Iceberg tables into Iceberg CustomScan.
 *
 * Must be called once from _PG_init.
 */
extern void pg_iceberg_install_custom_scan(void);

#endif							/* __PG_ICEBERG_CUSTOM_SCAN_H__ */
