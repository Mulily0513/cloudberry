#include "datalake_def.h"


char *FileFormatName[] = {
	"ORC",
	"PARQUET",
	"AVRO",
	"HFILE",
	"HLOG",
	"AVRO_FILE_BLOCK"
};

IcebergJunkInfo datalake_iceberg_junk_info[DATALAKE_ICEBERG_JUNK_NUM] = {
	{1, "file_path", CSTRINGOID},
	{2, "pos", INT8OID},
};

/* Global file index map for Iceberg update/delete operations */
IcebergFileIndexMap *datalake_iceberg_file_index_map = NULL;

/* Global reference to all Iceberg fragments for cross-segment file ID consistency */
List *datalake_iceberg_all_fragments = NULL;