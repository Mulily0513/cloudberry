#pragma once

#include <src/provider/parquet/write/parquetFileWriter.h>
#include "src/common/rewrLogical.h"
#include "src/common/readPolicy.h"
#include "src/provider/provider.h"
#include "src/dlproxy/datalake.h"

extern "C" {
	#include "src/provider/common/utils.h"
	#include "src/provider/common/row_reader.h"
}

namespace Datalake {
namespace Internal {

class icebergWrite : public Provider
{
public:
	virtual void createHandler(void *sstate);
	virtual int64_t write(const void *values, int64_t length);
	virtual void destroyHandler();
protected:
	void initWriteOption();
	std::string generateWriteFilePrefix(dataLakeOptions *opt);

protected:
	std::string prefix;
	std::string file_name;
	std::string append_file_prefix;
	ossFileStream fileStream;
	writeOption option;
	std::unique_ptr<parquetFileWriter> file_writer;
	dataLakeFdwScanState *ss;
	int sliceIdx = 0;
	List *fileMetas = NIL;
	int64_t tuple_num = 0;

private:
	virtual void appendFileMeta();
};

}
}
