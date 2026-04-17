#ifndef BASE_READER_H
#define BASE_READER_H
extern "C"
{
#include "postgres.h"
#include "utils/memutils.h"
#include "catalog/pg_type_d.h"
}

#include <vector>
#include <string_view>
#include <utility>
#include <array>

struct DatalakeInternalRecord;
struct List;

typedef enum
{
	TIMEUNIT_MILLIS,
	TIMEUNIT_MICROS,
	TIMEUNIT_NANOS,
	TIMEUNIT_UNKNOWN
} TIMEUNIT;

union ReaderValue
{
	bool    boolValue;
	int32_t int32Value;
	int64_t int64Value;
	float   floatValue;
	double  doubleValue;
};

class BaseFileReader;

/*
 * Per-column function pointer that reads one value from the underlying
 * file format, bypassing virtual dispatch and type-switch overhead.
 * Set during createMapping(); NULL for unmapped columns.
 */
typedef Datum (*ReadColumnFn)(BaseFileReader *reader, int scannerIdx, bool &isNull);

class BaseFileReader
{
protected:
	struct TypeInfo
	{
		Oid pgTypeId_;
		int typeMod_;
		Oid fileTypeId_;
		int columnIndex_;
		TIMEUNIT timeUnit_;
		int scale_;			/* Decimal scale (NUMERIC only) */
		int precision_;		/* Decimal precision (NUMERIC only) */
		int typeLength_;	/* Fixed-length byte size (BPCHAR/FLBA only) */
		ReadColumnFn readFn_;	/* Direct read function, bypasses virtual+switch */
	};

	int curGroup_;
	uint32_t curRow_;
	uint32_t numRows_;
	MemoryContext rowContext_;
	std::vector<TypeInfo> typeMap_;
	std::vector<int64_t> rowPositions_;

	virtual Datum readPrimitive(const TypeInfo &typInfo, bool &isNull) = 0;
	virtual bool readNextRowGroup() = 0;
	virtual void createMapping(List *columnDesc, bool *attrUsed) = 0;
	virtual void decodeRecord() = 0;

	void populateRecord(DatalakeInternalRecord *record);
	int64 transformTimestamp(int64 timestamp, TIMEUNIT timeUnit);

public:
	BaseFileReader(MemoryContext rowContext);
	virtual ~BaseFileReader() = 0;
	bool next(DatalakeInternalRecord *record);
	virtual void open(List *columnDesc, bool *attrUsed, int64_t startOffset, int64_t endOffset) = 0;
	virtual void close() = 0;
};

#endif // BASE_READER_H
