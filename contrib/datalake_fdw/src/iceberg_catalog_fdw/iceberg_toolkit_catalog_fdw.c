/*
 * iceberg_toolkit_catalog_fdw.c
 *	  Iceberg catalog FDW toolkit functions for testing
 */

#include "postgres.h"
#include "fmgr.h"
#include "utils/builtins.h"
#include "lib/stringinfo.h"
#include "cdb/cdbsreh.h"
#include "cdb/cdbvars.h"
#include "cdb/cdbsrlz.h"
#include "cdb/cdbdisp.h"
#include "catalog/pg_foreign_server.h"
#include "catalog/pg_foreign_data_wrapper.h"
#include "foreign/foreign.h"
#include "foreign/fdwapi.h"
#include "utils/syscache.h"
#include "utils/memutils.h"
#include "nodes/execnodes.h"
#include "executor/executor.h"
#include "access/table.h"
#include "utils/lsyscache.h"
#include "foreign/fdwapi.h"
#include "foreign/foreign.h"
#include "nodes/pg_list.h"
#include "nodes/makefuncs.h"
#include "optimizer/optimizer.h"
#include "optimizer/paths.h"
#include "optimizer/pathnode.h"
#include "parser/parse_relation.h"
#include "utils/rel.h"
#include "catalog/namespace.h"
#include "optimizer/planmain.h"
#include "optimizer/restrictinfo.h"
#include "optimizer/cost.h"
#include "parser/parsetree.h"
#include "parser/parse_relation.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/sampling.h"
#include "utils/typcache.h"
#include "utils/acl.h"
#include "tcop/utility.h"
#include "catalog/pg_foreign_catalog.h"
#include "iceberg_catalog_fdw.h"
#include "../common/iceberg_toolkit_common.h"
#include "../components/agent_cli/c_interface/agent_c_api.h"
#include "../components/agent_cli/c_interface/agent_cjson_builder.hpp"

#define ICEBERG_SERVER_NAME "iceberg_catalog_fdw"

/*
 * SQL functions
 */
extern Datum iceberg_toolkit_catalog_fdw(PG_FUNCTION_ARGS);
extern Datum polaris_list_catalogs(PG_FUNCTION_ARGS);
extern Datum polaris_list_namespaces(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(iceberg_toolkit_catalog_fdw);
PG_FUNCTION_INFO_V1(polaris_list_catalogs);
PG_FUNCTION_INFO_V1(polaris_list_namespaces);


/*
 * Helper function declarations
 */
extern Datum iceberg_toolkit_catalog_fdw(PG_FUNCTION_ARGS);

/**
 * help function
 */
Datum iceberg_create_table(char* foreignWrapperName, char* nameSpace, char* tableName,
     char* operationStr, char* foreignCatalogNameStr, char* catalogTableNameStr,
     char* volumeServer, char* volumeTable);

Datum iceberg_scan_operate(IcebergCatalogOperation operation, char* foreignWrapperName, char* nameSpace, char* tableName,
     char* operationStr, char* foreignCatalogNameStr, char* catalogTableNameStr,
     char* volumeServer, char* volumeTable);

Datum iceberg_append(char* foreignWrapperName, char* nameSpace, char* tableName,
     char* operationStr, char* foreignCatalogNameStr, char* catalogTableNameStr,
     char* volumeServer, char* volumeTable, char* appendJsonStr);

Datum iceberg_update(char* foreignWrapperName, char* nameSpace, char* tableName,
     char* operationStr, char* foreignCatalogNameStr, char* catalogTableNameStr,
     char* volumeServer, char* volumeTable, char* appendJsonStr);

Datum iceberg_drop_table(char* foreignWrapperName, char* nameSpace, char* tableName,
     char* operationStr, char* foreignCatalogNameStr, char* catalogTableNameStr,
     char* volumeServer, char* volumeTable);

Datum iceberg_plan_file_groups(char* foreignWrapperName, char* nameSpace, char* tableName,
	 char* operationStr, char* foreignCatalogNameStr, char* catalogTableNameStr,
	 char* volumeServer, char* volumeTable, int minInputFiles, int targetFileSizeMb);

Datum iceberg_commit_file_groups(char* foreignWrapperName, char* nameSpace, char* tableName,
	 char* operationStr, char* foreignCatalogNameStr, char* catalogTableNameStr,
	 char* volumeServer, char* volumeTable, char* appendJsonStr);

Datum iceberg_commit_append(char* foreignWrapperName, char* nameSpace, char* tableName,
	 char* operationStr, char* foreignCatalogNameStr, char* catalogTableNameStr,
	 char* volumeServer, char* volumeTable, char* appendJsonStr);

Datum iceberg_commit_update(char* foreignWrapperName, char* nameSpace, char* tableName,
	 char* operationStr, char* foreignCatalogNameStr, char* catalogTableNameStr,
	 char* volumeServer, char* volumeTable, char* appendJsonStr);

Datum iceberg_commit_delete(char* foreignWrapperName, char* nameSpace, char* tableName,
	 char* operationStr, char* foreignCatalogNameStr, char* catalogTableNameStr,
	 char* volumeServer, char* volumeTable, char* appendJsonStr);

Datum iceberg_commit_rewrite(char* foreignWrapperName, char* nameSpace, char* tableName,
	 char* operationStr, char* foreignCatalogNameStr, char* catalogTableNameStr,
	 char* volumeServer, char* volumeTable, char* appendJsonStr);

Datum iceberg_list_catalogs(char* foreignWrapperName, char* nameSpace, char* tableName,
     char* operationStr, char* foreignCatalogNameStr, char* catalogTableNameStr,
     char* volumeServer, char* volumeTable);

Datum iceberg_list_namespaces(char* foreignWrapperName, char* nameSpace, char* tableName,
     char* operationStr, char* foreignCatalogNameStr, char* catalogTableNameStr,
     char* volumeServer, char* volumeTable);

Datum iceberg_create_table(char* foreignWrapperName, char* nameSpace, char* tableName,
     char* operationStr, char* foreignCatalogNameStr, char* catalogTableNameStr,
     char* volumeServer, char* volumeTable)
{
    StringInfoData result;
    initStringInfo(&result);

    /* Execute catalog operation */
    IcebergCatalogFdwState* fdwStateResult = iceberg_execute_catalog_operation(
        ICEBERG_CREATE_TABLE, nameSpace, tableName,
        foreignCatalogNameStr, catalogTableNameStr, volumeServer, volumeTable, NULL);

    if (fdwStateResult->lastStatus == ICEBERG_SUCCESS)
    {
        if (fdwStateResult->response.responseBody)
        {
            StringInfoData responseData;
            initStringInfo(&responseData);
            appendStringInfo(&responseData, "\"response\":%s", fdwStateResult->response.responseBody);
            iceberg_format_success_response(&result, operationStr, nameSpace, tableName, responseData.data);
        }
        else
        {
            iceberg_format_success_response(&result, operationStr, nameSpace, tableName, NULL);
        }
    }
    else
    {
        iceberg_format_error_response(&result, operationStr, nameSpace, tableName, fdwStateResult);
    }

    PG_RETURN_TEXT_P(cstring_to_text(result.data));
}

Datum iceberg_scan_operate(IcebergCatalogOperation operation, char* foreignWrapperName, char* nameSpace, char* tableName,
     char* operationStr, char* foreignCatalogNameStr, char* catalogTableNameStr,
     char* volumeServer, char* volumeTable)
{
    StringInfoData result;
    initStringInfo(&result);

    /* Execute catalog operation */
    IcebergCatalogFdwState* fdwStateResult = iceberg_execute_catalog_operation(
        operation, nameSpace, tableName,
        foreignCatalogNameStr, catalogTableNameStr, volumeServer, volumeTable, NULL);

    if (fdwStateResult->lastStatus == ICEBERG_SUCCESS)
    {
        if (fdwStateResult->response.responseBody)
        {
            StringInfoData fragmentsData;
            initStringInfo(&fragmentsData);
            appendStringInfo(&fragmentsData, "\"fragments\":%s", fdwStateResult->response.responseBody);
            iceberg_format_success_response(&result, operationStr, nameSpace, tableName, fragmentsData.data);
        }
        else
        {
            iceberg_format_success_response(&result, operationStr, nameSpace, tableName, "\"fragments\":[]");
        }
    }
    else
    {
        iceberg_format_error_response(&result, operationStr, nameSpace, tableName, fdwStateResult);
    }

    PG_RETURN_TEXT_P(cstring_to_text(result.data));
}

Datum iceberg_append(char* foreignWrapperName, char* nameSpace, char* tableName,
     char* operationStr, char* foreignCatalogNameStr, char* catalogTableNameStr,
     char* volumeServer, char* volumeTable, char* appendJsonStr)
{
    StringInfoData result;
    initStringInfo(&result);

    /* Execute catalog operation */
    IcebergCatalogFdwState* fdwStateResult = iceberg_execute_catalog_operation(
        ICEBERG_APPEND, nameSpace, tableName,
        foreignCatalogNameStr, catalogTableNameStr, volumeServer, volumeTable, appendJsonStr);

    if (fdwStateResult->lastStatus == ICEBERG_SUCCESS)
    {
        if (fdwStateResult->response.responseBody)
        {
            StringInfoData fragmentsData;
            initStringInfo(&fragmentsData);
            appendStringInfo(&fragmentsData, "\"append\":%s", fdwStateResult->response.responseBody);
            iceberg_format_success_response(&result, operationStr, nameSpace, tableName, fragmentsData.data);
        }
        else
        {
            iceberg_format_success_response(&result, operationStr, nameSpace, tableName, "\"append\":[]");
        }
    }
    else
    {
        iceberg_format_error_response(&result, operationStr, nameSpace, tableName, fdwStateResult);
    }

    PG_RETURN_TEXT_P(cstring_to_text(result.data));

}

Datum iceberg_update(char* foreignWrapperName, char* nameSpace, char* tableName,
     char* operationStr, char* foreignCatalogNameStr, char* catalogTableNameStr,
     char* volumeServer, char* volumeTable, char* appendJsonStr)
{
    StringInfoData result;
    initStringInfo(&result);

    /* Execute catalog operation */
    IcebergCatalogFdwState* fdwStateResult = iceberg_execute_catalog_operation(
        ICEBERG_UPDATE, nameSpace, tableName,
        foreignCatalogNameStr, catalogTableNameStr, volumeServer, volumeTable, appendJsonStr);

    if (fdwStateResult->lastStatus == ICEBERG_SUCCESS)
    {
        if (fdwStateResult->response.responseBody)
        {
            StringInfoData fragmentsData;
            initStringInfo(&fragmentsData);
            appendStringInfo(&fragmentsData, "\"update\":%s", fdwStateResult->response.responseBody);
            iceberg_format_success_response(&result, operationStr, nameSpace, tableName, fragmentsData.data);
        }
        else
        {
            iceberg_format_success_response(&result, operationStr, nameSpace, tableName, "\"update\":[]");
        }
    }
    else
    {
        iceberg_format_error_response(&result, operationStr, nameSpace, tableName, fdwStateResult);
    }

    PG_RETURN_TEXT_P(cstring_to_text(result.data));

}

Datum iceberg_plan_file_groups(char* foreignWrapperName, char* nameSpace, char* tableName,
	 char* operationStr, char* foreignCatalogNameStr, char* catalogTableNameStr,
	 char* volumeServer, char* volumeTable, int minInputFiles, int targetFileSizeMb)
{
	StringInfoData result;
	initStringInfo(&result);

	IcebergCatalogFdwState* fdwStateResult = iceberg_execute_plan_file_groups_operation(
		nameSpace, tableName, foreignCatalogNameStr, catalogTableNameStr,
		volumeServer, volumeTable, minInputFiles, targetFileSizeMb);

	if (fdwStateResult->lastStatus == ICEBERG_SUCCESS)
	{
		if (fdwStateResult->response.responseBody)
		{
			StringInfoData fragmentsData;
			initStringInfo(&fragmentsData);
			appendStringInfo(&fragmentsData, "\"fileGroups\":%s", fdwStateResult->response.responseBody);
			iceberg_format_success_response(&result, operationStr, nameSpace, tableName, fragmentsData.data);
		}
		else
		{
			iceberg_format_success_response(&result, operationStr, nameSpace, tableName, "\"fileGroups\":[]");
		}
	}
	else
	{
		iceberg_format_error_response(&result, operationStr, nameSpace, tableName, fdwStateResult);
	}

	PG_RETURN_TEXT_P(cstring_to_text(result.data));
}

Datum iceberg_commit_file_groups(char* foreignWrapperName, char* nameSpace, char* tableName,
	 char* operationStr, char* foreignCatalogNameStr, char* catalogTableNameStr,
	 char* volumeServer, char* volumeTable, char* appendJsonStr)
{
	StringInfoData result;
	initStringInfo(&result);

	IcebergCatalogFdwState* fdwStateResult = iceberg_execute_catalog_operation(
		ICEBERG_COMMIT_FILE_GROUPS, nameSpace, tableName,
		foreignCatalogNameStr, catalogTableNameStr, volumeServer, volumeTable, appendJsonStr);

	if (fdwStateResult->lastStatus == ICEBERG_SUCCESS)
	{
		if (fdwStateResult->response.responseBody)
		{
			StringInfoData fragmentsData;
			initStringInfo(&fragmentsData);
			appendStringInfo(&fragmentsData, "\"commitFileGroups\":%s", fdwStateResult->response.responseBody);
			iceberg_format_success_response(&result, operationStr, nameSpace, tableName, fragmentsData.data);
		}
		else
		{
			iceberg_format_success_response(&result, operationStr, nameSpace, tableName, NULL);
		}
	}
	else
	{
		iceberg_format_error_response(&result, operationStr, nameSpace, tableName, fdwStateResult);
	}

	PG_RETURN_TEXT_P(cstring_to_text(result.data));
}

Datum iceberg_commit_append(char* foreignWrapperName, char* nameSpace, char* tableName,
	 char* operationStr, char* foreignCatalogNameStr, char* catalogTableNameStr,
	 char* volumeServer, char* volumeTable, char* appendJsonStr)
{
	StringInfoData result;
	initStringInfo(&result);

	IcebergCatalogFdwState* fdwStateResult = iceberg_execute_catalog_operation(
		ICEBERG_COMMIT_APPEND, nameSpace, tableName,
		foreignCatalogNameStr, catalogTableNameStr, volumeServer, volumeTable, appendJsonStr);

	if (fdwStateResult->lastStatus == ICEBERG_SUCCESS)
	{
		if (fdwStateResult->response.responseBody)
		{
			StringInfoData fragmentsData;
			initStringInfo(&fragmentsData);
			appendStringInfo(&fragmentsData, "\"commitAppend\":%s", fdwStateResult->response.responseBody);
			iceberg_format_success_response(&result, operationStr, nameSpace, tableName, fragmentsData.data);
		}
		else
		{
			iceberg_format_success_response(&result, operationStr, nameSpace, tableName, NULL);
		}
	}
	else
	{
		iceberg_format_error_response(&result, operationStr, nameSpace, tableName, fdwStateResult);
	}

	PG_RETURN_TEXT_P(cstring_to_text(result.data));
}

Datum iceberg_commit_update(char* foreignWrapperName, char* nameSpace, char* tableName,
	 char* operationStr, char* foreignCatalogNameStr, char* catalogTableNameStr,
	 char* volumeServer, char* volumeTable, char* appendJsonStr)
{
	StringInfoData result;
	initStringInfo(&result);

	IcebergCatalogFdwState* fdwStateResult = iceberg_execute_catalog_operation(
		ICEBERG_COMMIT_UPDATE, nameSpace, tableName,
		foreignCatalogNameStr, catalogTableNameStr, volumeServer, volumeTable, appendJsonStr);

	if (fdwStateResult->lastStatus == ICEBERG_SUCCESS)
	{
		if (fdwStateResult->response.responseBody)
		{
			StringInfoData fragmentsData;
			initStringInfo(&fragmentsData);
			appendStringInfo(&fragmentsData, "\"commitUpdate\":%s", fdwStateResult->response.responseBody);
			iceberg_format_success_response(&result, operationStr, nameSpace, tableName, fragmentsData.data);
		}
		else
		{
			iceberg_format_success_response(&result, operationStr, nameSpace, tableName, NULL);
		}
	}
	else
	{
		iceberg_format_error_response(&result, operationStr, nameSpace, tableName, fdwStateResult);
	}

	PG_RETURN_TEXT_P(cstring_to_text(result.data));
}

Datum iceberg_commit_delete(char* foreignWrapperName, char* nameSpace, char* tableName,
	 char* operationStr, char* foreignCatalogNameStr, char* catalogTableNameStr,
	 char* volumeServer, char* volumeTable, char* appendJsonStr)
{
	StringInfoData result;
	initStringInfo(&result);

	IcebergCatalogFdwState* fdwStateResult = iceberg_execute_catalog_operation(
		ICEBERG_COMMIT_DELETE, nameSpace, tableName,
		foreignCatalogNameStr, catalogTableNameStr, volumeServer, volumeTable, appendJsonStr);

	if (fdwStateResult->lastStatus == ICEBERG_SUCCESS)
	{
		if (fdwStateResult->response.responseBody)
		{
			StringInfoData fragmentsData;
			initStringInfo(&fragmentsData);
			appendStringInfo(&fragmentsData, "\"commitDelete\":%s", fdwStateResult->response.responseBody);
			iceberg_format_success_response(&result, operationStr, nameSpace, tableName, fragmentsData.data);
		}
		else
		{
			iceberg_format_success_response(&result, operationStr, nameSpace, tableName, NULL);
		}
	}
	else
	{
		iceberg_format_error_response(&result, operationStr, nameSpace, tableName, fdwStateResult);
	}

	PG_RETURN_TEXT_P(cstring_to_text(result.data));
}

Datum iceberg_commit_rewrite(char* foreignWrapperName, char* nameSpace, char* tableName,
	 char* operationStr, char* foreignCatalogNameStr, char* catalogTableNameStr,
	 char* volumeServer, char* volumeTable, char* appendJsonStr)
{
	StringInfoData result;
	initStringInfo(&result);

	IcebergCatalogFdwState* fdwStateResult = iceberg_execute_catalog_operation(
		ICEBERG_COMMIT_REWRITE, nameSpace, tableName,
		foreignCatalogNameStr, catalogTableNameStr, volumeServer, volumeTable, appendJsonStr);

	if (fdwStateResult->lastStatus == ICEBERG_SUCCESS)
	{
		if (fdwStateResult->response.responseBody)
		{
			StringInfoData fragmentsData;
			initStringInfo(&fragmentsData);
			appendStringInfo(&fragmentsData, "\"commitRewrite\":%s", fdwStateResult->response.responseBody);
			iceberg_format_success_response(&result, operationStr, nameSpace, tableName, fragmentsData.data);
		}
		else
		{
			iceberg_format_success_response(&result, operationStr, nameSpace, tableName, NULL);
		}
	}
	else
	{
		iceberg_format_error_response(&result, operationStr, nameSpace, tableName, fdwStateResult);
	}

	PG_RETURN_TEXT_P(cstring_to_text(result.data));
}

Datum iceberg_drop_table(char* foreignWrapperName, char* nameSpace, char* tableName,
     char* operationStr, char* foreignCatalogNameStr, char* catalogTableNameStr,
     char* volumeServer, char* volumeTable)
{
    StringInfoData result;
    initStringInfo(&result);

    /* Execute catalog operation */
    IcebergCatalogFdwState* fdwStateResult = iceberg_execute_catalog_operation(
        ICEBERG_DROPTABLE, nameSpace, tableName,
        foreignCatalogNameStr, catalogTableNameStr, volumeServer, volumeTable, NULL);

    if (fdwStateResult->lastStatus == ICEBERG_SUCCESS)
    {
        iceberg_format_success_response(&result, operationStr, nameSpace, tableName, "\"dropped\":true");
    }
    else
    {
        iceberg_format_error_response(&result, operationStr, nameSpace, tableName, fdwStateResult);
    }

    PG_RETURN_TEXT_P(cstring_to_text(result.data));
}

Datum iceberg_list_catalogs(char* foreignWrapperName, char* nameSpace, char* tableName,
     char* operationStr, char* foreignCatalogNameStr, char* catalogTableNameStr,
     char* volumeServer, char* volumeTable)
{
    StringInfoData result;
    initStringInfo(&result);

    /* Execute catalog operation - nameSpace and tableName are not used for list_catalog */
    IcebergCatalogFdwState* fdwStateResult = iceberg_execute_catalog_operation(
        ICEBERG_RESTCATALOG_LISTCATALOG, NULL, NULL,
        foreignCatalogNameStr, catalogTableNameStr, NULL, NULL, NULL);

    if (fdwStateResult->lastStatus == ICEBERG_SUCCESS)
    {
        if (fdwStateResult->response.responseBody)
        {
            StringInfoData catalogsData;
            initStringInfo(&catalogsData);
            appendStringInfo(&catalogsData, "\"catalogs\":%s", fdwStateResult->response.responseBody);
            iceberg_format_success_response(&result, operationStr, "", "", catalogsData.data);
        }
        else
        {
            iceberg_format_success_response(&result, operationStr, "", "", "\"catalogs\":[]");
        }
    }
    else
    {
        iceberg_format_error_response(&result, operationStr, "", "", fdwStateResult);
    }

    PG_RETURN_TEXT_P(cstring_to_text(result.data));
}

Datum iceberg_list_namespaces(char* foreignWrapperName, char* nameSpace, char* tableName,
     char* operationStr, char* foreignCatalogNameStr, char* catalogTableNameStr,
     char* volumeServer, char* volumeTable)
{
    StringInfoData result;
    initStringInfo(&result);

    /* Execute catalog operation - nameSpace and tableName are not used for list_namespace */
    IcebergCatalogFdwState* fdwStateResult = iceberg_execute_catalog_operation(
        ICEBERG_RESTCATALOG_LISTNAMESPACE, NULL, NULL,
        foreignCatalogNameStr, catalogTableNameStr, NULL, NULL, NULL);

    if (fdwStateResult->lastStatus == ICEBERG_SUCCESS)
    {
        if (fdwStateResult->response.responseBody)
        {
            StringInfoData namespacesData;
            initStringInfo(&namespacesData);
            appendStringInfo(&namespacesData, "\"namespaces\":%s", fdwStateResult->response.responseBody);
            iceberg_format_success_response(&result, operationStr, "", "", namespacesData.data);
        }
        else
        {
            iceberg_format_success_response(&result, operationStr, "", "", "\"namespaces\":[]");
        }
    }
    else
    {
        iceberg_format_error_response(&result, operationStr, "", "", fdwStateResult);
    }

    PG_RETURN_TEXT_P(cstring_to_text(result.data));
}

/*
 * iceberg_toolkit_catalog_fdw
 *   Test function for iceberg catalog operations using real FDW infrastructure
 */
Datum iceberg_toolkit_catalog_fdw(PG_FUNCTION_ARGS)
{
    text *operation = PG_GETARG_TEXT_PP(0);
    text *nameSpace = PG_GETARG_TEXT_PP(1);
    text *table_name = PG_GETARG_TEXT_PP(2);
    text *catalogServerName = PG_GETARG_TEXT_PP(3);
    text *catalogTableName = PG_GETARG_TEXT_PP(4);
    text *volumeServerName = PG_GETARG_TEXT_PP(5);
    text *volumeTableName = PG_GETARG_TEXT_PP(6);


    char *op_str = text_to_cstring(operation);
    char *nameSpaceStr = text_to_cstring(nameSpace);
    char *table_str = text_to_cstring(table_name);
    char *foreignCatalogNameStr = text_to_cstring(catalogServerName);
    char *catalogTableNameStr = text_to_cstring(catalogTableName);
    char *foreignVolumeServer = text_to_cstring(volumeServerName);
    char *foreignVolumeTableName = text_to_cstring(volumeTableName);
    char *appendJsonStr = "";
    if (PG_NARGS() == 8)
    {
        if (!PG_ARGISNULL(7))
        {
            appendJsonStr = text_to_cstring(PG_GETARG_TEXT_PP(7));
        }
    }

    StringInfoData result;
    initStringInfo(&result);

    if (strcmp(op_str, "create_table") == 0)
    {
        return iceberg_create_table(ICEBERG_SERVER_NAME, nameSpaceStr, table_str, op_str,
            foreignCatalogNameStr, catalogTableNameStr, foreignVolumeServer, foreignVolumeTableName);
    }
    else if (strcmp(op_str, "get_fragment") == 0)
    {
        return iceberg_scan_operate(ICEBERG_GET_FRAGMENT, ICEBERG_SERVER_NAME, nameSpaceStr, table_str, op_str,
            foreignCatalogNameStr, catalogTableNameStr, foreignVolumeServer, foreignVolumeTableName);
    }
    else if (strcmp(op_str, "append") == 0)
    {
        return iceberg_append(ICEBERG_SERVER_NAME, nameSpaceStr, table_str, op_str,
            foreignCatalogNameStr, catalogTableNameStr, foreignVolumeServer, foreignVolumeTableName, appendJsonStr);
    }
    else if (strcmp(op_str, "load_table") == 0)
    {
        return iceberg_scan_operate(ICEBERG_LOAD_TABLE, ICEBERG_SERVER_NAME, nameSpaceStr, table_str, op_str,
            foreignCatalogNameStr, catalogTableNameStr, foreignVolumeServer, foreignVolumeTableName);
    }
    else if (strcmp(op_str, "update") == 0)
    {
        return iceberg_update(ICEBERG_SERVER_NAME, nameSpaceStr, table_str, op_str,
            foreignCatalogNameStr, catalogTableNameStr, foreignVolumeServer, foreignVolumeTableName, appendJsonStr);
    }
    else if (strcmp(op_str, "drop_table") == 0)
    {
        return iceberg_drop_table(ICEBERG_SERVER_NAME, nameSpaceStr, table_str, op_str,
            foreignCatalogNameStr, catalogTableNameStr, foreignVolumeServer, foreignVolumeTableName);
    }
    else if (strcmp(op_str, "list_catalog") == 0)
    {
        return iceberg_list_catalogs(ICEBERG_SERVER_NAME, nameSpaceStr, table_str, op_str,
            foreignCatalogNameStr, catalogTableNameStr, foreignVolumeServer, foreignVolumeTableName);
    }
    else if (strcmp(op_str, "list_namespace") == 0)
    {
        return iceberg_list_namespaces(ICEBERG_SERVER_NAME, nameSpaceStr, table_str, op_str,
            foreignCatalogNameStr, catalogTableNameStr, foreignVolumeServer, foreignVolumeTableName);
    }
    else if (strcmp(op_str, "get_statistics") == 0)
    {
        return iceberg_scan_operate(ICEBERG_GET_STATISTICS, ICEBERG_SERVER_NAME, nameSpaceStr, table_str, op_str,
            foreignCatalogNameStr, catalogTableNameStr, foreignVolumeServer, foreignVolumeTableName);
    }
	else if (strcmp(op_str, "plan_file_groups") == 0)
	{
		int minInputFiles = 5;       /* default */
		int targetFileSizeMb = 512;  /* default */

		/* Parse optional parameters from appendJsonStr */
		if (appendJsonStr && appendJsonStr[0] != '\0')
		{
			agentcli_cJSON *json = agentcli_cJSON_Parse(appendJsonStr);
			if (json)
			{
				agentcli_cJSON *minFiles = agentcli_cJSON_GetObjectItem(json, "minInputFiles");
				if (minFiles && agentcli_cJSON_IsNumber(minFiles))
					minInputFiles = minFiles->valueint;
				agentcli_cJSON *targetSize = agentcli_cJSON_GetObjectItem(json, "targetFileSizeMb");
				if (targetSize && agentcli_cJSON_IsNumber(targetSize))
					targetFileSizeMb = targetSize->valueint;
				agentcli_cJSON_Delete(json);
			}
		}

		return iceberg_plan_file_groups(ICEBERG_SERVER_NAME, nameSpaceStr, table_str, op_str,
			foreignCatalogNameStr, catalogTableNameStr, foreignVolumeServer, foreignVolumeTableName,
			minInputFiles, targetFileSizeMb);
	}
	else if (strcmp(op_str, "commit_file_groups") == 0)
	{
		return iceberg_commit_file_groups(ICEBERG_SERVER_NAME, nameSpaceStr, table_str, op_str,
			foreignCatalogNameStr, catalogTableNameStr, foreignVolumeServer, foreignVolumeTableName,
			appendJsonStr);
	}
	else if (strcmp(op_str, "commitAppend") == 0)
	{
		return iceberg_commit_append(ICEBERG_SERVER_NAME, nameSpaceStr, table_str, op_str,
			foreignCatalogNameStr, catalogTableNameStr, foreignVolumeServer, foreignVolumeTableName,
			appendJsonStr);
	}
	else if (strcmp(op_str, "commitUpdate") == 0)
	{
		return iceberg_commit_update(ICEBERG_SERVER_NAME, nameSpaceStr, table_str, op_str,
			foreignCatalogNameStr, catalogTableNameStr, foreignVolumeServer, foreignVolumeTableName,
			appendJsonStr);
	}
	else if (strcmp(op_str, "commitDelete") == 0)
	{
		return iceberg_commit_delete(ICEBERG_SERVER_NAME, nameSpaceStr, table_str, op_str,
			foreignCatalogNameStr, catalogTableNameStr, foreignVolumeServer, foreignVolumeTableName,
			appendJsonStr);
	}
	else if (strcmp(op_str, "commitRewrite") == 0)
	{
		return iceberg_commit_rewrite(ICEBERG_SERVER_NAME, nameSpaceStr, table_str, op_str,
			foreignCatalogNameStr, catalogTableNameStr, foreignVolumeServer, foreignVolumeTableName,
			appendJsonStr);
	}
    else
    {
        appendStringInfo(&result, "{\"status\":\"error\",\"operation\":\"%s\",\"error\":\"unsupported operation\"}",
                    op_str);
    }

    PG_RETURN_TEXT_P(cstring_to_text(result.data));
}



/*
 * polaris_list_catalogs
 * Direct connection to Polaris to list all catalogs
 */
Datum
polaris_list_catalogs(PG_FUNCTION_ARGS)
{
    text *datalake_agent_url_text = PG_GETARG_TEXT_PP(0);
    text *polaris_url_text = PG_GETARG_TEXT_PP(1);
    text *client_id_text = PG_GETARG_TEXT_PP(2);
    text *client_secret_text = PG_GETARG_TEXT_PP(3);
    text *scope_text = PG_GETARG_TEXT_PP(4);

    char *datalake_agent_url = text_to_cstring(datalake_agent_url_text);
    char *polaris_url = text_to_cstring(polaris_url_text);
    char *client_id = text_to_cstring(client_id_text);
    char *client_secret = text_to_cstring(client_secret_text);
    char *scope = text_to_cstring(scope_text);

    StringInfoData result;
    initStringInfo(&result);

    /* Create agent handle with datalake agent URL */
    AgentCliHandle *handle = agent_cli_wrapper_create(datalake_agent_url, "", "");
    if (!handle)
        ereport(ERROR, (errcode(ERRCODE_OUT_OF_MEMORY),
                errmsg("failed to create agent CLI handle")));

    /* Build request JSON following the same structure as createListCatalogsRequestJson */
    agentcli_cJSON *request = agentcli_cJSON_CreateObject();
    agentcli_cJSON *icebergConfig = agentcli_cJSON_CreateObject();

    /* Create catalogConfig */
    agentcli_cJSON *catalogConfig = agentcli_cJSON_CreateObject();
    agentcli_cJSON_AddStringToObject(catalogConfig, "server_type", "polaris");
    agentcli_cJSON_AddStringToObject(catalogConfig, "polaris_server_url", polaris_url);
    agentcli_cJSON_AddStringToObject(catalogConfig, "client_id", client_id);
    agentcli_cJSON_AddStringToObject(catalogConfig, "client_secret", client_secret);
    agentcli_cJSON_AddStringToObject(catalogConfig, "scope", scope);

    agentcli_cJSON_AddItemToObject(icebergConfig, "IcebergCatalogConfig", catalogConfig);
    agentcli_cJSON_AddItemToObject(request, "IcebergConfig", icebergConfig);

    char *json_string = agentcli_cJSON_PrintUnformatted(request);
    agentcli_cJSON_Delete(request);

    /* Call agent to list catalogs */
    agent_cli_wrapper_list_catalogs(handle, json_string);
    free(json_string);

    /* Check for errors */
    agent_cli_wrapper_check_exec_error(handle, "polaris_list_catalogs");

    /* Format result */
    const char *response = agent_cli_wrapper_get_response(handle);
    if (response)
        appendStringInfo(&result, "{\"status\":\"success\",\"catalogs\":%s}", response);
    else
        appendStringInfo(&result, "{\"status\":\"success\",\"catalogs\":[]}");

    agent_cli_wrapper_destroy(handle);
    PG_RETURN_TEXT_P(cstring_to_text(result.data));
}

/*
 * polaris_list_namespaces
 * Direct connection to Polaris to list namespaces in a catalog
 */
Datum
polaris_list_namespaces(PG_FUNCTION_ARGS)
{
    text *datalake_agent_url_text = PG_GETARG_TEXT_PP(0);
    text *polaris_url_text = PG_GETARG_TEXT_PP(1);
    text *client_id_text = PG_GETARG_TEXT_PP(2);
    text *client_secret_text = PG_GETARG_TEXT_PP(3);
    text *catalog_name_text = PG_GETARG_TEXT_PP(4);
    text *scope_text = PG_GETARG_TEXT_PP(5);

    char *datalake_agent_url = text_to_cstring(datalake_agent_url_text);
    char *polaris_url = text_to_cstring(polaris_url_text);
    char *client_id = text_to_cstring(client_id_text);
    char *client_secret = text_to_cstring(client_secret_text);
    char *catalog_name = text_to_cstring(catalog_name_text);
    char *scope = text_to_cstring(scope_text);

    StringInfoData result;
    initStringInfo(&result);

    /* Create agent handle with datalake agent URL */
    AgentCliHandle *handle = agent_cli_wrapper_create(datalake_agent_url, "", "");
    if (!handle)
        ereport(ERROR, (errcode(ERRCODE_OUT_OF_MEMORY),
                errmsg("failed to create agent CLI handle")));

    /* Build request JSON following the same structure as createListNamespacesRequestJson */
    agentcli_cJSON *request = agentcli_cJSON_CreateObject();
    agentcli_cJSON *icebergConfig = agentcli_cJSON_CreateObject();

    /* Create catalogConfig */
    agentcli_cJSON *catalogConfig = agentcli_cJSON_CreateObject();
    agentcli_cJSON_AddStringToObject(catalogConfig, "server_type", "polaris");
    agentcli_cJSON_AddStringToObject(catalogConfig, "polaris_server_url", polaris_url);
    agentcli_cJSON_AddStringToObject(catalogConfig, "client_id", client_id);
    agentcli_cJSON_AddStringToObject(catalogConfig, "client_secret", client_secret);
    agentcli_cJSON_AddStringToObject(catalogConfig, "scope", scope);

    agentcli_cJSON_AddItemToObject(icebergConfig, "IcebergCatalogConfig", catalogConfig);
    agentcli_cJSON_AddItemToObject(request, "IcebergConfig", icebergConfig);
    agentcli_cJSON_AddStringToObject(request, "catalogName", catalog_name);

    char *json_string = agentcli_cJSON_PrintUnformatted(request);
    agentcli_cJSON_Delete(request);

    /* Call agent to list namespaces */
    agent_cli_wrapper_list_namespaces(handle, json_string);
    free(json_string);

    /* Check for errors */
    agent_cli_wrapper_check_exec_error(handle, "polaris_list_namespaces");

    /* Format result */
    const char *response = agent_cli_wrapper_get_response(handle);
    if (response)
        appendStringInfo(&result, "{\"status\":\"success\",\"namespaces\":%s}", response);
    else
        appendStringInfo(&result, "{\"status\":\"success\",\"namespaces\":[]}");

    agent_cli_wrapper_destroy(handle);
    PG_RETURN_TEXT_P(cstring_to_text(result.data));
}
