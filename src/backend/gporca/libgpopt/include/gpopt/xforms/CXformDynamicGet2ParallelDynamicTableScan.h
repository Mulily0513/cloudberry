//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (c) 2025, HashData Technology Limited.
//
//	@filename:
//		CXformDynamicGet2ParallelDynamicTableScan.h
//
//	@doc:
//		Transform DynamicGet to ParallelDynamicTableScan
//---------------------------------------------------------------------------
#ifndef GPOPT_CXformDynamicGet2ParallelDynamicTableScan_H
#define GPOPT_CXformDynamicGet2ParallelDynamicTableScan_H

#include "gpos/base.h"

#include "gpopt/operators/CLogicalDynamicGet.h"
#include "gpopt/xforms/CXformImplementation.h"

namespace gpopt
{
using namespace gpos;

//---------------------------------------------------------------------------
//	@class:
//		CXformDynamicGet2ParallelDynamicTableScan
//
//	@doc:
//		Transform DynamicGet to ParallelDynamicTableScan
//
//---------------------------------------------------------------------------
class CXformDynamicGet2ParallelDynamicTableScan : public CXformImplementation
{
private:
public:
	CXformDynamicGet2ParallelDynamicTableScan(
		const CXformDynamicGet2ParallelDynamicTableScan &) = delete;

	// ctor
	explicit CXformDynamicGet2ParallelDynamicTableScan(CMemoryPool *mp);

	// dtor
	~CXformDynamicGet2ParallelDynamicTableScan() override = default;

	// ident accessors
	EXformId
	Exfid() const override
	{
		return ExfDynamicGet2ParallelDynamicTableScan;
	}

	// return a string for xform name
	const CHAR *
	SzId() const override
	{
		return "CXformDynamicGet2ParallelDynamicTableScan";
	}

	// compute xform promise for a given expression handle
	EXformPromise Exfp(CExpressionHandle &exprhdl) const override;

	// actual transform
	void Transform(CXformContext *pxfctxt, CXformResult *pxfres,
				   CExpression *pexpr) const override;

};	// class CXformDynamicGet2ParallelDynamicTableScan

}  // namespace gpopt


#endif	// !GPOPT_CXformDynamicGet2ParallelDynamicTableScan_H

// EOF
