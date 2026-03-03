#pragma once

#include "src/provider/parquet/write/parquetFileWriter.h"
#include "src/common/rewrLogical.h"
#include "src/common/readPolicy.h"
#include "src/provider/provider.h"
#include "src/dlproxy/datalake.h"
#include "src/provider/iceberg/iceberg_write.h"
#include "src/provider/iceberg/metadata_column.h"

extern "C" {
#include "src/provider/common/utils.h"
#include "src/provider/common/row_reader.h"
}

namespace Datalake {
namespace Internal {

class icebergPosDeleteWrite : public icebergWrite
{
public:
	static inline std::vector<MetadataField> POS_DELETE_SCHEMA{MetadataColumns::DELETE_FILE_PATH, MetadataColumns::DELETE_FILE_POS};
	virtual void createHandler(void *sstate);
private:
	void appendFileMeta();
};

}
}
