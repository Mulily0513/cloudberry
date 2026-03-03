// #include "postgres.h"
#include "common.h"
#include "base_reader.h"

extern "C"
{
#include "src/datalake_def.h"
#include "access/tupdesc.h"
#include "utils.h"
#include "gopher/gopher.h"
}

BaseFileReader::BaseFileReader(MemoryContext rowContext)
	: curGroup_(-1), curRow_(0), numRows_(0), rowContext_(rowContext)
{}

BaseFileReader::~BaseFileReader()
{}

void BaseFileReader::populateRecord(DatalakeInternalRecord *record)
{
	bool   isNull;
	size_t size = typeMap_.size();

	decodeRecord();

	for (size_t attr = 0; attr < size; attr++)
	{
		TypeInfo &typInfo = typeMap_[attr];
		if (typInfo.columnIndex_ < 0 || typInfo.fileTypeId_ == InvalidOid)
		{
			record->nulls[attr] = true;
			continue;
		}

		isNull = false;
		record->values[attr] = readPrimitive(typInfo, isNull);
		record->nulls[attr] = isNull;
	}

	record->position = rowPositions_[curGroup_] + curRow_;
}

bool
BaseFileReader::next(DatalakeInternalRecord *record)
{
	MemoryContext oldContext;

	if (curRow_ >= numRows_)
	{
		do
		{
			if (!readNextRowGroup())
				return false;
		}
		while (!numRows_);
	}

	MemoryContextReset(rowContext_);

	oldContext = MemoryContextSwitchTo(rowContext_);
	populateRecord(record);
	MemoryContextSwitchTo(oldContext);

	curRow_++;

	return true;
}

int64
BaseFileReader::transformTimestamp(int64 timestamp, TIMEUNIT timeUnit)
{
	switch (timeUnit)
	{
		case TIMEUNIT_MILLIS:
			return timestamp / 1000;
		case TIMEUNIT_MICROS:
			return timestamp / 1000000;
		case TIMEUNIT_NANOS:
			return timestamp / 1000000000;
		default:
			throw Error("Unknown timestamp precision");
	}
}
