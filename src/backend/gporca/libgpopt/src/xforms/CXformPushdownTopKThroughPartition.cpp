//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2026 Hashdata, Inc.
//
//	@filename:
//		CXformPushdownTopKThroughPartition.cpp
//
//	@doc:
//		Push down a Top-N predicate on a window function through a Project
//		by introducing CLogicalPartitionTopK.
//		Supports complex inputs (e.g., GbAgg, Join) as in TPC-DS Q67.
//
//---------------------------------------------------------------------------

#include "gpopt/xforms/CXformPushdownTopKThroughPartition.h"

#include <climits>
#include "gpos/base.h"
#include "gpos/common/CAutoP.h"

#include "gpopt/base/COptCtxt.h"
#include "gpopt/base/CUtils.h"
#include "gpopt/base/CWindowOids.h"
#include "gpopt/optimizer/COptimizerConfig.h"
#include "gpopt/operators/CLogicalPartitionTopK.h"	// YOU MUST HAVE THIS!
#include "gpopt/operators/CLogicalProject.h"
#include "gpopt/operators/CLogicalSelect.h"
#include "gpopt/operators/CLogicalSequenceProject.h"
#include "gpopt/operators/CPatternLeaf.h"
#include "gpopt/operators/CScalarCmp.h"
#include "gpopt/operators/CScalarConst.h"
#include "gpopt/operators/CScalarIdent.h"
#include "gpopt/operators/CScalarProjectElement.h"
#include "gpopt/operators/CScalarProjectList.h"
#include "gpopt/operators/CScalarWindowFunc.h"
#include "naucrates/base/CDatumInt8GPDB.h"
#include "naucrates/md/CMDIdGPDB.h"
#include "naucrates/md/CMDTypeInt8GPDB.h"  // for GPDB_INT8_OID

using namespace gpopt;

//---------------------------------------------------------------------------
//	@function:
//		CXformPushdownTopKThroughPartition::CXformPushdownTopKThroughPartition
//
//	@doc:
//		Ctor: pattern is Select(SequenceProject(LeafInput, ProjList), Predicate)
//		Use CPatternLeaf for input to restrict matching to base tables,
//		SharedScans, or CTE consumers — avoiding complex subtrees like
//		Join or GbAgg. This prevents exploration explosion in queries
//		with aggregations (e.g., TPC-DS Q67) while still allowing
//		optimization on materialized subplans.
//
//---------------------------------------------------------------------------
CXformPushdownTopKThroughPartition::CXformPushdownTopKThroughPartition(
	CMemoryPool *mp)
	: CXformExploration(GPOS_NEW(mp) CExpression(
		  mp, GPOS_NEW(mp) CLogicalSelect(mp),
		  GPOS_NEW(mp) CExpression(
			  mp, GPOS_NEW(mp) CLogicalSequenceProject(mp),
			  // Input must be a leaf node (e.g., base table, SharedScan, CTEConsumer)
			  GPOS_NEW(mp) CExpression(mp, GPOS_NEW(mp) CPatternLeaf(mp)),
			  // Project list can be any scalar tree (to capture window functions)
			  GPOS_NEW(mp) CExpression(mp, GPOS_NEW(mp) CPatternTree(mp))),
		  // Predicate can be any scalar condition (filtered later by FMatchPredicate)
		  GPOS_NEW(mp) CExpression(mp, GPOS_NEW(mp) CPatternTree(mp))))
{
}

//---------------------------------------------------------------------------
//	@function:
//		CXformPushdownTopKThroughPartition::Exfp
//
//	@doc:
//		Check if the select predicate is of the form: window_func <= N,
//		and the project list contains exactly one ranking window function.
//
//---------------------------------------------------------------------------
CXform::EXformPromise
CXformPushdownTopKThroughPartition::Exfp(CExpressionHandle &exprhdl) const
{
	if (!FCanApply(exprhdl))
	{
		return CXform::ExfpNone;
	}
	return CXform::ExfpHigh;
}


//---------------------------------------------------------------------------
//	@function:
//		CXformPushdownTopKThroughPartition::FCanApply
//
//	@doc:
//		Fast check: only verify root operator is CLogicalSelect.
//		Full validation is deferred to Transform().
//
//---------------------------------------------------------------------------
BOOL
CXformPushdownTopKThroughPartition::FCanApply(CExpressionHandle &exprhdl) const
{
	COperator *pop = nullptr;

	if (nullptr != exprhdl.Pexpr())
	{
		pop = exprhdl.Pexpr()->Pop();
	}
	else if (nullptr != exprhdl.Pgexpr())
	{
		pop = exprhdl.Pgexpr()->Pop();
	}
	else
	{
		return false;
	}

	return (nullptr != pop) && (COperator::EopLogicalSelect == pop->Eopid());
}


//---------------------------------------------------------------------------
//	@function:
//		CXformPushdownTopKThroughPartition::FValidatePattern
//
//	@doc:
//		Validate that:
//		1. Outer child is Project
//		2. Project list contains EXACTLY ONE ranking window function
//		3. Predicate is (window_col <= N) or (window_col < N+1)
//
//---------------------------------------------------------------------------
BOOL
CXformPushdownTopKThroughPartition::FValidatePattern(CExpression *pexpr) const
{
	// Check if there are exactly two children: Project and Predicate
	if (2 != pexpr->Arity())
	{
		return false;
	}

	CExpression *pexprSeqPrj = (*pexpr)[0];
	CExpression *pexprPred = (*pexpr)[1];

	// Must be Project
	if (COperator::EopLogicalSequenceProject != pexprSeqPrj->Pop()->Eopid())
	{
		return false;
	}

	// Predicate must exist and not be TRUE
	if (CUtils::FScalarConstTrue(pexprPred))
	{
		return false;
	}

	// Get project list (second child of Project)
	CExpression *pexprPrjList = (*pexprSeqPrj)[1];
	if (COperator::EopScalarProjectList != pexprPrjList->Pop()->Eopid())
	{
		return false;
	}

	// Find window function(s) in project list
	CScalarWindowFunc *popWinFunc = nullptr;
	CColRef *pcrWindow = nullptr;
	ULONG ulWinFuncCount = 0;

	const ULONG arity = pexprPrjList->Arity();
	for (ULONG ul = 0; ul < arity; ul++)
	{
		CExpression *pexprElem = (*pexprPrjList)[ul];
		if (COperator::EopScalarProjectElement != pexprElem->Pop()->Eopid())
		{
			continue;
		}
		CExpression *pexprWin = (*pexprElem)[0];
		if (COperator::EopScalarWindowFunc == pexprWin->Pop()->Eopid())
		{
			ulWinFuncCount++;
			popWinFunc = CScalarWindowFunc::PopConvert(pexprWin->Pop());
			pcrWindow =
				CScalarProjectElement::PopConvert(pexprElem->Pop())->Pcr();
			// We only support exactly one window function for now
			if (ulWinFuncCount > 1)
			{
				return false;
			}
		}
	}

	if (nullptr == popWinFunc || nullptr == pcrWindow || 0 == ulWinFuncCount)
	{
		return false;
	}

	// Only support RANK() for now — match by OID, not function name string
	IMDId *mdid = popWinFunc->FuncMdId();
	CWindowOids *pwindowoids =
		COptCtxt::PoctxtFromTLS()->GetOptimizerConfig()->GetWindowOids();
	if (!mdid->Equals(pwindowoids->MDIdRank()))
	{
		return false;
	}

	// Check predicate refers to this window column and is <= / <
	if (!FMatchPredicate(pexprPred, pcrWindow))
	{
		return false;
	}

	return true;
}


//---------------------------------------------------------------------------
//	@function:
//		CXformPushdownTopKThroughPartition::FMatchPredicate
//
//	@doc:
//		Check if predicate is of the form:
//		  (ident(pcr) <= const) or (ident(pcr) < const)
//		  (const >= ident(pcr)) or (const > ident(pcr))
//
//---------------------------------------------------------------------------
BOOL
CXformPushdownTopKThroughPartition::FMatchPredicate(
	CExpression *pexprPred,
	CColRef *pcrWindow) const
{
	if (COperator::EopScalarCmp != pexprPred->Pop()->Eopid())
	{
		return false;
	}

	CScalarCmp *popCmp = CScalarCmp::PopConvert(pexprPred->Pop());
	IMDType::ECmpType cmp_type = popCmp->ParseCmpType();

	CExpression *pexprLeft = (*pexprPred)[0];
	CExpression *pexprRight = (*pexprPred)[1];

	// Normal form: window_col <= N or window_col < N
	if ((IMDType::EcmptLEq == cmp_type || IMDType::EcmptL == cmp_type) &&
		COperator::EopScalarIdent == pexprLeft->Pop()->Eopid() &&
		COperator::EopScalarConst == pexprRight->Pop()->Eopid())
	{
		const CColRef *pcr = CScalarIdent::PopConvert(pexprLeft->Pop())->Pcr();
		if (pcr == pcrWindow)
		{
			IDatum *datum =
				CScalarConst::PopConvert(pexprRight->Pop())->GetDatum();
			return !datum->IsNull() && datum->IsDatumMappableToLINT();
		}
	}

	// Commuted form: N >= window_col or N > window_col
	if ((IMDType::EcmptGEq == cmp_type || IMDType::EcmptG == cmp_type) &&
		COperator::EopScalarConst == pexprLeft->Pop()->Eopid() &&
		COperator::EopScalarIdent == pexprRight->Pop()->Eopid())
	{
		const CColRef *pcr = CScalarIdent::PopConvert(pexprRight->Pop())->Pcr();
		if (pcr == pcrWindow)
		{
			IDatum *datum =
				CScalarConst::PopConvert(pexprLeft->Pop())->GetDatum();
			return !datum->IsNull() && datum->IsDatumMappableToLINT();
		}
	}

	return false;
}


//---------------------------------------------------------------------------
//	@function:
//		CXformPushdownTopKThroughPartition::ExtractTopN
//
//	@doc:
//		Extract N from predicate:
//		  func <= N  => N;   func < N  => N-1
//		  N >= func  => N;   N > func  => N-1
//
//---------------------------------------------------------------------------
INT
CXformPushdownTopKThroughPartition::ExtractTopN(CExpression *pexprPred) const
{
	CScalarCmp *popCmp = CScalarCmp::PopConvert(pexprPred->Pop());
	IMDType::ECmpType cmp_type = popCmp->ParseCmpType();

	// Determine which child is the constant
	CExpression *pexprConst = nullptr;
	if (IMDType::EcmptLEq == cmp_type || IMDType::EcmptL == cmp_type)
	{
		// Normal form: func <= N or func < N — const is on the right
		pexprConst = (*pexprPred)[1];
	}
	else if (IMDType::EcmptGEq == cmp_type || IMDType::EcmptG == cmp_type)
	{
		// Commuted form: N >= func or N > func — const is on the left
		pexprConst = (*pexprPred)[0];
	}
	else
	{
		return -1;
	}

	CScalarConst *popConst = CScalarConst::PopConvert(pexprConst->Pop());
	IDatum *datum = popConst->GetDatum();

	if (datum->IsNull())
	{
		return -1;
	}

	// Accept any integer type that can be mapped to LINT (INT2, INT4, INT8)
	if (!datum->IsDatumMappableToLINT())
	{
		return -1;
	}

	LINT value = datum->GetLINTMapping();

	if (value <= 0)
	{
		return -1;
	}

	// Guard against INT64 -> INT32 overflow (e.g., rank() <= 9999999999)
	if (value > (LINT) INT_MAX)
	{
		return INT_MAX;
	}

	// <= and >= yield the value directly; < and > subtract 1
	if (IMDType::EcmptLEq == cmp_type || IMDType::EcmptGEq == cmp_type)
	{
		return static_cast<INT>(value);
	}
	else
	{
		// EcmptL or EcmptG
		if (value <= 1)
			return -1;
		return static_cast<INT>(value - 1);
	}
}


//---------------------------------------------------------------------------
//	@function:
//		CXformPushdownTopKThroughPartition::Transform
//
//	@doc:
//		Transform:
//			Select
//			 |-- SequenceProject(input, [..., win_func as rnk], partition, order)
//			 +-- (rnk <= N)
//		Into:
//			Select
//			 |-- SequenceProject(PartitionTopK(input, partition_cols, order_spec, N), [..., win_func as rnk], partition, order)
//			 +-- (rnk <= N)
//
//		Note: The window function's context (partition/order) is stored in
//		      CLogicalSequenceProject, NOT in CScalarWindowFunc.
//        We replace the input of SequenceProject with PartitionTopK,
//        so that the physical plan becomes:
//            WindowAgg
//              -> Partition Top-K
//                   -> input
//
//---------------------------------------------------------------------------
void
CXformPushdownTopKThroughPartition::Transform(CXformContext *pxfctxt,
											  CXformResult *pxfres,
											  CExpression *pexpr) const
{
	GPOS_ASSERT(nullptr != pxfctxt);
	GPOS_ASSERT(nullptr != pxfres);
	GPOS_ASSERT(FPromising(pxfctxt->Pmp(), this, pexpr));
	GPOS_ASSERT(FCheckPattern(pexpr));

	// Full pattern validation — moved from FCanApply
	if (!FValidatePattern(pexpr))
	{
		return;
	}

	if (GPOS_FTRACE(EopttracePrintXform))
	{
		CAutoTrace at(pxfctxt->Pmp());
		at.Os() << "Applying CXformPushdownTopKThroughPartition on:"
				<< std::endl;
		pexpr->OsPrint(at.Os());
	}

	CMemoryPool *mp = pxfctxt->Pmp();

	CExpression *pexprSelect = pexpr;
	CExpression *pexprSeqPrj = (*pexprSelect)[0];  // ← Now a SequenceProject
	CExpression *pexprInput = (*pexprSeqPrj)[0];
	CExpression *pexprScalarProjList = (*pexprSeqPrj)[1];  // ← NEW
	CExpression *pexprPred = (*pexprSelect)[1];

	const COperator::EOperatorId op_part_topk =
		COperator::EopLogicalPartitionTopK;
	if (CUtils::FHasOp(pexprInput, &op_part_topk, 1))
	{
		return;
	}

	// Extract Top-N value from predicate (e.g., rnk <= 10 → nTop = 10)
	INT nTop = ExtractTopN(pexprPred);
	if (nTop <= 0)
	{
		return;
	}

	// --- Extract PARTITION BY columns from CLogicalSequenceProject ---
	CLogicalSequenceProject *popSeqPrj =
		CLogicalSequenceProject::PopConvert(pexprSeqPrj->Pop());

	if (popSeqPrj->Pspt() == COperator::EsptypeGlobalTwoStep)
	{
		return;
	}

	CColRefArray *pdrgpcrPartition = nullptr;
	CDistributionSpec *pds = popSeqPrj->Pds();
	if (CDistributionSpec::EdtHashed == pds->Edt())
	{
		CDistributionSpecHashed *pdsHashed =
			dynamic_cast<CDistributionSpecHashed *>(pds);
		CExpressionArray *pdrgpexprPart = pdsHashed->Pdrgpexpr();

		pdrgpcrPartition = GPOS_NEW(mp) CColRefArray(mp);
		for (ULONG ul = 0; ul < pdrgpexprPart->Size(); ul++)
		{
			CExpression *pexprPartKey = (*pdrgpexprPart)[ul];
			GPOS_ASSERT(COperator::EopScalarIdent ==
						pexprPartKey->Pop()->Eopid());
			const CColRef *colref =
				CScalarIdent::PopConvert(pexprPartKey->Pop())->Pcr();
			pdrgpcrPartition->Append(const_cast<CColRef *>(colref));
		}
	}
	else
	{
		// No PARTITION BY → global window → empty partition key list
		pdrgpcrPartition = GPOS_NEW(mp) CColRefArray(mp);
	}

	// --- Extract ORDER BY specification ---
	if (!popSeqPrj->FHasOrderSpecs() || 0 == popSeqPrj->Pdrgpos()->Size())
	{
		// Should not happen if rank() has meaningful semantics, but be safe
		pdrgpcrPartition->Release();
		return;
	}
	// Assume single window frame → use first (and only) order spec
	COrderSpec *pos = (*popSeqPrj->Pdrgpos())[0];
	pos->AddRef();	// CLogicalPartitionTopK takes ownership

	// AddRef only after all early-return checks have passed
	pexprInput->AddRef();
	pexprScalarProjList->AddRef();
	pexprPred->AddRef();

	// Create the new PartitionTopK operator
	CExpression *pexprPartitionTopK = GPOS_NEW(mp) CExpression(
		mp, GPOS_NEW(mp) CLogicalPartitionTopK(mp, pdrgpcrPartition, pos, nTop),
		pexprInput);  // consumes pexprInput; no extra AddRef needed

	popSeqPrj->AddRef();
	// Reuse the original SequenceProject operator, but with new child (PartitionTopK)
	// This preserves the window function, partitioning, and ordering context.
	CExpression *pexprNewSeqPrj = GPOS_NEW(mp) CExpression(
		mp,
		popSeqPrj,	// same CLogicalSequenceProject operator
		pexprPartitionTopK,
		pexprScalarProjList);  // new input: PartitionTopK instead of Sort/Scan

	// Wrap the new SequenceProject back into the original Select
	CExpression *pexprNewSelect = GPOS_NEW(mp)
		CExpression(mp, GPOS_NEW(mp) CLogicalSelect(mp), pexprNewSeqPrj,
					pexprPred);	 // reuse predicate

	// Add transformed expression to result set
	pxfres->Add(pexprNewSelect);

	GPOS_ASSERT(nullptr != popSeqPrj->Pds());
	GPOS_ASSERT(popSeqPrj->Pdrgpos()->Size() > 0);
}

// EOF