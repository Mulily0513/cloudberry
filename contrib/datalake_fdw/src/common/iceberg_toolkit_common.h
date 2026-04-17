/*
 * iceberg_toolkit_common.h
 *      Common functions and utilities for Iceberg toolkit FDWs
 */

#ifndef ICEBERG_TOOLKIT_COMMON_H
#define ICEBERG_TOOLKIT_COMMON_H

#include "postgres.h"
#include "foreign/fdwapi.h"
#include "lib/stringinfo.h"
#include "../iceberg_catalog_fdw/iceberg_catalog_fdw.h"

/* FDW name constants */
#define ICEBERG_CATALOG_FDW_NAME "iceberg_catalog_fdw"
#define ICEBERG_VOLUME_FDW_NAME "iceberg_volume_fdw"

/*
 * FDW routine helpers
 */
extern FdwRoutine* iceberg_get_catalog_fdw_routine(void);
extern FdwRoutine* iceberg_get_volume_fdw_routine(void);

/*
 * Schema building helpers
 */
extern IcebergTableSchema* iceberg_build_schema_from_table_name(const char* tableName);
extern IcebergColumnDef* iceberg_make_column_def(const char* name, Oid dataType,
                                                 int32 typemod, bool nullable, const char* comment);

/*
 * JSON response formatting helpers
 */
extern void iceberg_format_success_response(StringInfo result, const char* operation,
                                           const char* nameSpace, const char* tableName,
                                           const char* additionalData);
extern void iceberg_format_error_response(StringInfo result, const char* operation,
                                         const char* nameSpace, const char* tableName,
                                         IcebergCatalogFdwState* fdwState);

/*
 * FDW state setup helpers
 */
extern void iceberg_setup_catalog_fdw_state(IcebergCatalogFdwState* fdwState,
                                           IcebergCatalogOperation operation,
                                           const char* nameSpace, const char* tableName,
                                           const char* catalogServer, const char* catalogTable,
                                           const char* volumeServer, const char* volumeTable);

/*
 * High-level catalog operation wrapper
 */
extern IcebergCatalogFdwState* iceberg_execute_catalog_operation(
    IcebergCatalogOperation operation,
    const char* nameSpace,
    const char* tableName,
    const char* catalogServer,
    const char* catalogTable,
    const char* volumeServer,
    const char* volumeTable,
    const char* appendJson);

/*
 * Plan file groups operation for vacuum
 */
extern IcebergCatalogFdwState* iceberg_execute_plan_file_groups_operation(
	const char* nameSpace, const char* tableName,
	const char* catalogServer, const char* catalogTable,
	const char* volumeServer, const char* volumeTable,
	int minInputFiles, int targetFileSizeMb);

#endif /* ICEBERG_TOOLKIT_COMMON_H */
