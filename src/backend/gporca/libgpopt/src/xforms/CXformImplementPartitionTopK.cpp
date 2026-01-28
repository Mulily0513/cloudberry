//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2026 Hashdata, Inc.
//
//	@filename:
//		CXformImplementPartitionTopK.cpp
//
//	@doc:
//		Implementation of logical PartitionTopK operator
//
//---------------------------------------------------------------------------

#include "gpopt/xforms/CXformImplementPartitionTopK.h"

#include "gpos/base.h"

#include "gpopt/operators/CLogicalPartitionTopK.h"
#include "gpopt/operators/CPatternLeaf.h"
#include "gpopt/operators/CPhysicalPartitionTopK.h"
#include "gpopt/xforms/CXformUtils.h"

using namespace gpopt;


//---------------------------------------------------------------------------
//	@function:
//		CXformImplementPartitionTopK::CXformImplementPartitionTopK
//
//	@doc:
//		Ctor
//
//---------------------------------------------------------------------------
CXformImplementPartitionTopK::CXformImplementPartitionTopK(CMemoryPool *mp)
	: CXformImplementation(
		  // Pattern: LogicalPartitionTopK(any child)
		  GPOS_NEW(mp) CExpression(
			  mp,
			  GPOS_NEW(mp)
				  CLogicalPartitionTopK(mp),  // ← calls PATTERN constructor!
			  GPOS_NEW(mp) CExpression(mp, GPOS_NEW(mp) CPatternLeaf(mp))))
{
}


//---------------------------------------------------------------------------
//	@function:
//		CXformImplementPartitionTopK::Exfp
//
//	@doc:
//		Compute xform promise
//
//---------------------------------------------------------------------------
CXform::EXformPromise
CXformImplementPartitionTopK::Exfp(CExpressionHandle &	// exprhdl
) const
{
	if (GPOS_FTRACE(EopttraceForcePartitionTopK))
	{
		return CXform::ExfpHigh;
	}
	return CXform::ExfpNone;
}

//---------------------------------------------------------------------------
//	@function:
//		CXformImplementPartitionTopK::Transform
//
//	@doc:
//		Actual transformation: replace logical PartitionTopK with physical one
//
//---------------------------------------------------------------------------
void
CXformImplementPartitionTopK::Transform(CXformContext *pxfctxt,
										CXformResult *pxfres,
										CExpression *pexpr) const
{
	GPOS_ASSERT(nullptr != pxfctxt);
	GPOS_ASSERT(nullptr != pxfres);
	GPOS_ASSERT(nullptr != pexpr);
	GPOS_ASSERT(COperator::EopLogicalPartitionTopK == pexpr->Pop()->Eopid());

	CMemoryPool *mp = pxfctxt->Pmp();

	// extract components from logical operator
	CLogicalPartitionTopK *popLogical =
		CLogicalPartitionTopK::PopConvert(pexpr->Pop());
	CColRefArray *pdrgpcrPart = popLogical->GetPartitionKeys();
	COrderSpec *pos = popLogical->GetOrderSpec();
	INT nTop = popLogical->GetTopN();

	// child expression
	CExpression *pexprChild = (*pexpr)[0];
	pexprChild->AddRef();

	// create physical operator
	COperator *popPhysical =
		GPOS_NEW(mp) CPhysicalPartitionTopK(mp, pdrgpcrPart, pos, nTop);

	// create new expression
	CExpression *pexprPhysical =
		GPOS_NEW(mp) CExpression(mp, popPhysical, pexprChild);

	// add to results
	pxfres->Add(pexprPhysical);
}

// EOF