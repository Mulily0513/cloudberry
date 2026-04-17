/*
 * iceberg_toolkit_volume_fdw.h
 *      Header file for Iceberg volume FDW toolkit functions
 */

#ifndef ICEBERG_TOOLKIT_VOLUME_FDW_H
#define ICEBERG_TOOLKIT_VOLUME_FDW_H

#include "postgres.h"
#include "fmgr.h"

/*
 * Function declarations
 */
extern Datum iceberg_toolkit_volume_fdw(PG_FUNCTION_ARGS);

#endif /* ICEBERG_TOOLKIT_VOLUME_FDW_H */
