#include "parquetOutputStream.h"

gopherWriteFileSystem::~gopherWriteFileSystem()
{

}

bool gopherWriteFileSystem::OpenFile(const std::string& path, bool append)
{
    filePath_ = path;
    closed_ = false;
    pos_ = 0;
    bytes_written = 0;
    datalakeOpenFile(stream_, filePath_.c_str(), O_WRONLY);
    return true;
}

::parquet_arrow::Status gopherWriteFileSystem::Close()
{
    if (closed_)
    {
        // file maybe already closed
        return ::parquet_arrow::Status::OK();
    }
    closed_ = true;
    datalakeCloseFile(stream_);
    return ::parquet_arrow::Status::OK();
}

::parquet_arrow::Status gopherWriteFileSystem::Write(const void* data, int64_t nbytes)
{
    datalakeWriteFile(stream_, (void*)data, nbytes);
    pos_ += nbytes;
    bytes_written += nbytes;
    return ::parquet_arrow::Status::OK();
}