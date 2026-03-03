#include <parquet/internal/arrow/util/decimal.h>
#include <parquet/internal/arrow/result.h>
#include "parquet_reader.h"
#include "common.h"
#include "gopher_random_file.h"
#include "datalake_numeric.h"

extern "C"
{
#include "postgres.h"
#include "access/tupdesc.h"
#include "utils/memutils.h"
#include "nodes/pg_list.h"
#include "utils/builtins.h"
#include "utils.h"
#include "src/datalake_def.h"
}

ParquetReader::ParquetReader(MemoryContext rowContext, char *filePath, gopherFS gopherFilesystem, dataBufferArray *buffer)
	: BaseFileReader(rowContext), numColumns_(0), filePath_(filePath), gopherFilesystem_(gopherFilesystem), buffer_(buffer)
{}

ParquetReader::~ParquetReader()
{}

TIMEUNIT
ParquetReader::getTimeUnit(const parquet::ColumnDescriptor *field)
{
	const auto &logicalType = field->logical_type();
	if (!logicalType->is_timestamp())
		return TIMEUNIT_UNKNOWN;

	const auto *timestampType = dynamic_cast<const parquet::TimestampLogicalType*>(logicalType.get());
	switch (timestampType->time_unit())
	{
		case parquet::LogicalType::TimeUnit::MILLIS:
			return TIMEUNIT_MILLIS;
		case parquet::LogicalType::TimeUnit::MICROS:
			return TIMEUNIT_MICROS;
		case parquet::LogicalType::TimeUnit::NANOS:
			return TIMEUNIT_NANOS;
		default:
			throw Error("parquet error: Unknown timestamp precision");
	}
}

void
ParquetReader::createMapping(List *columnDesc, bool *attrUsed)
{
	int       i;
	int       j;
	ListCell *lc;
	auto      schema = metadata->schema();

	numColumns_ = schema->num_columns();
	foreach_with_count(lc, columnDesc, i)
	{
		DatalakeFieldDescription *entry = (DatalakeFieldDescription *) lfirst(lc);
		TypeInfo typInfo = {entry->typeOid, entry->typeMod, InvalidOid, -1, TIMEUNIT_UNKNOWN};
		typeMap_.push_back(typInfo);

		if (!attrUsed[i])
			continue;

		for (j = 0; j < numColumns_; j++)
		{
			const parquet::ColumnDescriptor *field = schema->Column(j);
			auto fieldName = field->name();

			if (pg_strcasecmp(entry->name, fieldName.c_str()) == 0)
			{
				typeMap_[i].columnIndex_ = j;
				typeMap_[i].fileTypeId_ = mapParquetDataType(field->physical_type());
				typeMap_[i].timeUnit_ = getTimeUnit(field);
				break;
			}
		}
	}
}

bool
ParquetReader::readNextRowGroup()
{
	curGroup_++;

	if ((uint) curGroup_ >= rowGroups_.size())
		return false;

	auto rowGroupReader = reader_->RowGroup(rowGroups_[curGroup_]);
	size_t typeSize = typeMap_.size();

	scanners_.clear();
	scanners_.resize(numColumns_);
	for (size_t i = 0; i < typeSize; ++i)
	{
		TypeInfo &typInfo = typeMap_[i];

		if (typInfo.columnIndex_ >= 0)
		{
			scanners_[typInfo.columnIndex_] = parquet::Scanner::Make(rowGroupReader->Column(typInfo.columnIndex_));
		}
	}

	curRow_ = 0;
	numRows_ = rowGroupReader->metadata()->num_rows();

	return true;
}

void
ParquetReader::open(List *columnDesc, bool *attrUsed, int64 startOffset, int64 endOffset)
{
	std::string filename = convertToGopherPath(filePath_);
	reader_ = parquet::ParquetFileReader::Open(std::make_shared<GopherRandomAccessFile> (gopherFilesystem_, filename));
	metadata = reader_->metadata();
	createMapping(columnDesc, attrUsed);
	filterRowGroupByOffset(startOffset, endOffset);
}

void
ParquetReader::close()
{
	scanners_.clear();
	reader_->Close();
}

bool
ParquetReader::invalidFileOffset(int64_t startIndex, int64_t preStartIndex, int64_t preCompressedSize)
{
	bool invalid = false;

	// checking the first rowGroup
	if (preStartIndex == 0 && startIndex != 4)
	{
		invalid = true;
		return invalid;
	}

	//calculate start index for other blocks
	int64_t minStartIndex = preStartIndex + preCompressedSize;
	if (startIndex < minStartIndex)
		invalid = true;

	return invalid;
}

void
ParquetReader::filterRowGroupByOffset(int64_t startOffset, int64_t endOffset)
{
	int64_t preStartIndex = 0;
	int64_t preCompressedSize = 0;
	int64_t curRowCount = 0;

	for (int i = 0; i < metadata->num_row_groups(); i++)
	{
		int64_t totalSize = 0;
		int64_t startIndex;

		if (startOffset == -1)
		{
			rowGroups_.push_back(i);
			rowPositions_.push_back(0);
			continue;
		}

		auto rowGroup = metadata->RowGroup(i);
		startIndex = rowGroup->file_offset();

		if (invalidFileOffset(startIndex, preStartIndex, preCompressedSize))
		{
			if (preStartIndex == 0)
				startIndex = 4;
			else
				startIndex = preStartIndex + preCompressedSize;
		}

		preStartIndex = startIndex;
		preCompressedSize = rowGroup->total_compressed_size();

		for (int j = 0; j < rowGroup->num_columns(); j++)
		{
			auto col = rowGroup->ColumnChunk(j);
			totalSize += col->total_compressed_size();
		}

		int64_t midPoint = startIndex + totalSize / 2;
		if (midPoint >= startOffset && midPoint < endOffset)
		{
			rowGroups_.push_back(i);
			rowPositions_.push_back(curRowCount);
		}

		curRowCount += rowGroup->num_rows();
	}
}

Datum
ParquetReader::readPrimitive(const TypeInfo &typInfo, bool &isNull)
{
	ReaderValue d;
	auto &scanner = scanners_[typInfo.columnIndex_];

	switch (typInfo.pgTypeId_)
	{
		case BOOLOID:
		{
			((parquet::TypedScanner<parquet::BooleanType> *)scanner.get())->NextValue(&d.boolValue, &isNull);
			return BoolGetDatum(d.boolValue);
		}
		case INT4OID:
		{
			((parquet::TypedScanner<parquet::Int32Type> *)scanner.get())->NextValue(&d.int32Value, &isNull);
			return Int32GetDatum(d.int32Value);
		}
		case TIMEOID:
		case INT8OID:
		{
			((parquet::TypedScanner<parquet::Int64Type> *)scanner.get())->NextValue(&d.int64Value, &isNull);
			return Int64GetDatum(d.int64Value);
		}
		case FLOAT4OID:
		{
			((parquet::TypedScanner<parquet::FloatType> *)scanner.get())->NextValue(&d.floatValue, &isNull);
			return Float4GetDatum(d.floatValue);
		}
		case FLOAT8OID:
		{
			((parquet::TypedScanner<parquet::DoubleType> *)scanner.get())->NextValue(&d.doubleValue, &isNull);
			return Float8GetDatum(d.doubleValue);
		}
		case BPCHAROID:
		{
			parquet::FixedLenByteArray value;
			((parquet::TypedScanner<parquet::FLBAType> *)scanner.get())->NextValue(&value, &isNull);
			if (isNull)
				PG_RETURN_DATUM(0);
			int typeLen = metadata->schema()->Column(typInfo.columnIndex_)->type_length();
			if (!buffer_)
			{
				bytea *result = (bytea *) gpdbPalloc(typeLen + VARHDRSZ);
				SET_VARSIZE(result, typeLen + VARHDRSZ);
				memcpy(VARDATA(result), value.ptr, typeLen);
				return PointerGetDatum(result);
			}
			if (typeLen + VARHDRSZ > static_cast<uint32>(buffer_->getDataBuffer(typInfo.columnIndex_)->length))
			{
				buffer_->resizeDataBuffer(typInfo.columnIndex_, typeLen + VARHDRSZ);
			}
			dataBuff *colBuffer = buffer_->getDataBuffer(typInfo.columnIndex_);
			SET_VARSIZE(colBuffer->buffer, typeLen + VARHDRSZ);
			memcpy(VARDATA(colBuffer->buffer), value.ptr, typeLen);
			return PointerGetDatum(colBuffer->buffer);
		}
		case BYTEAOID:
		case TEXTOID:
		{
			parquet::ByteArray value;
			((parquet::TypedScanner<parquet::ByteArrayType> *)scanner.get())->NextValue(&value, &isNull);
			if (isNull)
				PG_RETURN_DATUM(0);

			if (!buffer_)
			{
				bytea *result = (bytea *) gpdbPalloc(value.len + VARHDRSZ);
				SET_VARSIZE(result, value.len + VARHDRSZ);
				memcpy(VARDATA(result), value.ptr, value.len);
				return PointerGetDatum(result);
			}
			if (value.len + VARHDRSZ > static_cast<uint32>(buffer_->getDataBuffer(typInfo.columnIndex_)->length))
			{
				buffer_->resizeDataBuffer(typInfo.columnIndex_, value.len + VARHDRSZ);
			}
			dataBuff *colBuffer = buffer_->getDataBuffer(typInfo.columnIndex_);
			SET_VARSIZE(colBuffer->buffer, value.len + VARHDRSZ);
			memcpy(VARDATA(colBuffer->buffer), value.ptr, value.len);
			return PointerGetDatum(colBuffer->buffer);
		}
		case UUIDOID:
		{
			parquet::ByteArray value;
			((parquet::TypedScanner<parquet::ByteArrayType> *)scanner.get())->NextValue(&value, &isNull);
			if (isNull)
				PG_RETURN_DATUM(0);

			if (!buffer_)
			{
				bytea *result = (bytea *) gpdbPalloc(value.len);
				memcpy(VARDATA(result), value.ptr, 16);
				return PointerGetDatum(result);
			}	
			dataBuff *colBuffer = buffer_->getDataBuffer(typInfo.columnIndex_);
			memcpy(colBuffer->buffer, value.ptr, 16);
			return PointerGetDatum(colBuffer->buffer);
		}
		case TIMESTAMPOID:
		case TIMESTAMPTZOID:
		{
			((parquet::TypedScanner<parquet::Int64Type> *)scanner.get())->NextValue(&d.int64Value, &isNull);
			return TimestampGetDatum(time_t_to_timestamptz(transformTimestamp(d.int64Value, typInfo.timeUnit_))); // micorsec, refer to iceberg spec
		}
		case DATEOID:
		{
			((parquet::TypedScanner<parquet::Int32Type> *)scanner.get())->NextValue(&d.int32Value, &isNull);
			return DateADTGetDatum(d.int32Value + (UNIX_EPOCH_JDATE - POSTGRES_EPOCH_JDATE));
		}
		case NUMERICOID:
			return readDecimal(scanner, typInfo, isNull);

		default:
			throw Error("unsupported column type oid: \"%u\"", typInfo.pgTypeId_);
	}

	PG_RETURN_DATUM(0);
}

Datum
ParquetReader::readDecimal(std::shared_ptr<parquet::Scanner> &scanner, const TypeInfo &typInfo, bool &isNull)
{
	ReaderValue d;
	parquet::FixedLenByteArray value;
	const parquet::ColumnDescriptor *columnDesc = metadata->schema()->Column(typInfo.columnIndex_);
	int scale = columnDesc->type_scale();
	char *out_buf = nullptr;

	if (!buffer_)
	{
		out_buf = (char *) gpdbPalloc(NUMERIC_HDRSZ + IntDigitsTraits<__int128>::digits * sizeof(NumericDigit));
	}
	else
	{
		dataBuff *res = buffer_->getDataBuffer(typInfo.columnIndex_);
		out_buf = (char *) res->buffer;
	}

	// iceberg only support int4, int8, fixed-len
	switch (typInfo.fileTypeId_)
	{
		case INT4OID:
		{
			((parquet::TypedScanner<parquet::Int32Type> *)scanner.get())->NextValue(&d.int32Value, &isNull);
			if (isNull)
				PG_RETURN_DATUM(0);
			int_to_numeric_with_scale(d.int32Value, scale, (Numeric) out_buf);
			return NumericGetDatum(out_buf);
		}
		case INT8OID:
		{
			((parquet::TypedScanner<parquet::Int64Type> *)scanner.get())->NextValue(&d.int64Value, &isNull);
			if (isNull)
				PG_RETURN_DATUM(0);
			int_to_numeric_with_scale(d.int64Value, scale, (Numeric) out_buf);
			return NumericGetDatum(out_buf);
		}
		default:
		{
			int typeLen = columnDesc->type_length();
			((parquet::TypedScanner<parquet::FLBAType> *)scanner.get())->NextValue(&value, &isNull);
			if (isNull)
				PG_RETURN_DATUM(0);
			int_to_numeric_with_scale(FLBA_to_int128(value.ptr, typeLen), scale, (Numeric) out_buf);
			return NumericGetDatum(out_buf);
		}
	}

	PG_RETURN_DATUM(0);
}

void ParquetReader::decodeRecord() {}
