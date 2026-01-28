//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2026 Hashdata, Inc.
//
//	@filename:
//		CXformPushdownTopKThroughPartition.h
//
//	@doc:
//		Push down a Select with Top-N predicate through a Window operator
//		by introducing a CLogicalPartitionTopK.
//
//---------------------------------------------------------------------------

#ifndef GPOPT_CXformPushdownTopKThroughPartition_H
#define GPOPT_CXformPushdownTopKThroughPartition_H

#include "gpos/base.h"

#include "gpopt/xforms/CXformExploration.h"

namespace gpopt
{
using namespace gpos;

//---------------------------------------------------------------------------
//	@class:
//		CXformPushdownTopKThroughPartition
//
//	@doc:
//		Transform Select(Window(...), window_func <= N)
//		into Window(PartitionTopK(...))
//
//---------------------------------------------------------------------------
class CXformPushdownTopKThroughPartition : public CXformExploration
{
private:
	// private copy ctor
	CXformPushdownTopKThroughPartition(
		const CXformPushdownTopKThroughPartition &) = delete;

	// Validate full pattern structure (called in Transform)
	BOOL FValidatePattern(CExpression *pexpr) const;

	// Helper: check if transformation is applicable
	BOOL FCanApply(CExpressionHandle &exprhdl) const;

	// Helper: check if predicate is of form (window_col <= N) or (window_col < N+1)
	BOOL FMatchPredicate(CExpression *pexprPred, CColRef *pcrWindow) const;

	// Helper: extract N from predicate
	INT ExtractTopN(CExpression *pexprPred) const;

public:
	// ctor
	explicit CXformPushdownTopKThroughPartition(CMemoryPool *mp);

	// dtor
	~CXformPushdownTopKThroughPartition() override = default;

	// xform promise
	CXform::EXformPromise Exfp(CExpressionHandle &exprhdl) const override;

	// ident accessors
	EXformId
	Exfid() const override
	{
		return ExfPushdownTopKThroughPartition;
	}

	// return a string for xform name
	const CHAR *
	SzId() const override
	{
		return "CXformPushdownTopKThroughPartition";
	}

	// actual transform
	void Transform(CXformContext *pxfctxt, CXformResult *pxfres,
				   CExpression *pexpr) const override;

};	// class CXformPushdownTopKThroughPartition

}  // namespace gpopt

#endif	// !GPOPT_CXformPushdownTopKThroughPartition_H

// EOF