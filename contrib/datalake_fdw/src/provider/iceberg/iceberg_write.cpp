#include "iceberg_write.h"

extern "C" {
#include "access/table.h"
#include "utils/lsyscache.h"
#include <catalog/namespace.h>
#include <access/heapam.h>
#include "src/common/fileMetadata.h"
}

namespace Datalake {
namespace Internal {


void icebergWrite::createHandler(void *sstate)
{
	ss = (dataLakeFdwScanState*)sstate;
    gopherConfig *conf = datalakeCreateGopherConfig((void*)(ss->options->gopher));
    fileStream = datalakeCreateFileSystem(conf);
    datalakeFreeGopherConfig(conf);
	prefix = (char*)lfirst(list_head(ss->fragments));
	initWriteOption();
    file_name = generateWriteFileName(prefix, "", "parquet");
	append_file_prefix = generateWriteFilePrefix(ss->options);
	file_writer = std::make_unique<parquetFileWriter>();
	file_writer->init(sstate, option);
}

std::string icebergWrite::generateWriteFilePrefix(dataLakeOptions *opt)
{
	std::stringstream stream;
	if (PROTOCOL_IS_HDFS(opt->protocol))
	{
		stream << opt->gopher->gopherType << "://" << opt->gopher->hdfs_namenode_host << ":" << opt->gopher->hdfs_namenode_port;
	}
	else if (PROTOCOL_IS_OSS(opt->protocol)) {
		stream << opt->gopher->gopherType << "://" << opt->gopher->bucket << "/";
	}
	else
	{
		elog(ERROR, "Datalake foreign table Error, gopher type %s is not supported for iceberg.", opt->gopher->gopherType);
	}
	return stream.str();
}

void icebergWrite::appendFileMeta()
{
	MemoryContext oldContext = MemoryContextSwitchTo(CurrentMemoryContext->parent);
	FileFragment *meta = (FileFragment*)palloc0(sizeof(FileFragment));
	meta->filePath = pstrdup((append_file_prefix + file_name).c_str());
	meta->fileSize = file_writer->getWrittenBytes();
	meta->format = PARQUET;
	meta->recordCount = tuple_num;
	meta->content = DATA;
	meta->type = T_FileFragment;
	fileMetas = lappend(fileMetas, (void*)meta);
	MemoryContextSwitchTo(oldContext);
}

void icebergWrite::initWriteOption()
{
	option.writeFileSize = ss->options->fileSizeLimit;
	option.compression = ss->options->compress;
}

int64_t icebergWrite::write(const void* buf, int64_t length)
{
    if (file_writer->isOpen() && option.writeFileSize > 0 && file_writer->getWrittenBytes() + length > option.writeFileSize)
	{
		file_writer->closeParquetWriter();
		appendFileMeta();
		sliceIdx += 1;
	}

	if (!file_writer->isOpen())
	{
		file_name = generateWriteFileName(prefix, "", "parquet");
        file_writer->createParquetWriter(fileStream, file_name);
		tuple_num = 0;
	}
	int64_t len = file_writer->write(buf, length);
	tuple_num++;
	return len;
}

void icebergWrite::destroyHandler()
{
    if (file_writer->isOpen())
    {
        file_writer->closeParquetWriter();
        appendFileMeta();
    }
    datalakeDestroyFileSystem(fileStream);
    fileStream = NULL;

	if (fileMetas != NIL)
	{
		ListCell *lc = NULL;
		int i;
		foreach_with_count (lc, fileMetas, i)
		{
			FileFragment *meta = (FileFragment *)lfirst(lc);
			bytea *msg = FDW_serializeMeta(meta);
			FDW_SendMeta(msg);
			pfree(msg);
			pfree(meta->filePath);
		}
		list_free_deep(fileMetas);
	}
}
}
}