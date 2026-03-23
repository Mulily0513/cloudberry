//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (c) 2025, HashData Technology Limited.
//
//	@filename:
//		CXformDynamicGet2ParallelDynamicTableScan.cpp
//
//	@doc:
//		Implementation of transform
//---------------------------------------------------------------------------

#include "gpopt/xforms/CXformDynamicGet2ParallelDynamicTableScan.h"

#include "gpos/base.h"

#include "gpopt/hints/CHintUtils.h"
#include "gpopt/metadata/CTableDescriptor.h"
#include "gpopt/operators/CLogicalDynamicGet.h"
#include "gpopt/operators/CPhysicalParallelDynamicTableScan.h"
#include "gpopt/optimizer/COptimizerConfig.h"
#include "gpopt/xforms/CXformUtils.h"
#include "naucrates/md/IMDRelation.h"

// Use gpdbwrappers for parallel checks
extern int max_parallel_workers_per_gather;

// Forward declarations for gpdbwrappers functions
namespace gpdb {
	bool IsParallelModeOK(void);
}

using namespace gpopt;
using namespace gpmd;


CXformDynamicGet2ParallelDynamicTableScan::
	CXformDynamicGet2ParallelDynamicTableScan(CMemoryPool *mp)
	: CXformImplementation(
		  // pattern
		  GPOS_NEW(mp) CExpression(mp, GPOS_NEW(mp) CLogicalDynamicGet(mp)))
{
}


//---------------------------------------------------------------------------
//	@function:
//		CXformDynamicGet2ParallelDynamicTableScan::Exfp
//
//	@doc:
//		Compute xform promise for a given expression handle
//
//---------------------------------------------------------------------------
CXform::EXformPromise
CXformDynamicGet2ParallelDynamicTableScan::Exfp(
	CExpressionHandle &exprhdl) const
{
	// check that parallel mode is enabled
	if (!gpdb::IsParallelModeOK())
	{
		return CXform::ExfpNone;
	}

	// If dynamic table scan is disabled, don't generate parallel version either
	if (GPOS_FTRACE(EopttraceDisableDynamicTableScan))
	{
		return CXform::ExfpNone;
	}

	// check for parallel-incompatible operations in the query
	if (CXformUtils::FHasParallelIncompatibleOps(exprhdl))
	{
		return CXform::ExfpNone;
	}

	CLogicalDynamicGet *popGet =
		CLogicalDynamicGet::PopConvert(exprhdl.Pop());

	// Do not run if contains foreign partitions
	if (popGet->ContainsForeignParts())
	{
		return CXform::ExfpNone;
	}

	CTableDescriptor *ptabdesc = popGet->Ptabdesc();
	CMDAccessor *md_accessor = COptCtxt::PoctxtFromTLS()->Pmda();
	const IMDRelation *pmdrel =
		md_accessor->RetrieveRel(ptabdesc->MDId());

	// Do not parallelize replicated, master-only, or mixed-storage tables
	// Mixed-storage tables (regular + foreign partitions) have incompatible
	// distribution after ExpandDynamicGetWithForeignPartitions expansion
	if (pmdrel->GetRelDistribution() == IMDRelation::EreldistrReplicated ||
		pmdrel->GetRelDistribution() == IMDRelation::EreldistrMasterOnly ||
		ptabdesc->RetrieveRelStorageType() == IMDRelation::ErelstorageMixedPartitioned)
	{
		return CXform::ExfpNone;
	}

	return CXform::ExfpHigh;
}


//---------------------------------------------------------------------------
//	@function:
//		CXformDynamicGet2ParallelDynamicTableScan::Transform
//
//	@doc:
//		Actual transformation
//
//---------------------------------------------------------------------------
void
CXformDynamicGet2ParallelDynamicTableScan::Transform(
	CXformContext *pxfctxt, CXformResult *pxfres, CExpression *pexpr) const
{
	GPOS_ASSERT(nullptr != pxfctxt);
	GPOS_ASSERT(FPromising(pxfctxt->Pmp(), this, pexpr));
	GPOS_ASSERT(FCheckPattern(pexpr));

	CLogicalDynamicGet *popGet =
		CLogicalDynamicGet::PopConvert(pexpr->Pop());

	if (!CHintUtils::SatisfiesPlanHints(
			popGet,
			COptCtxt::PoctxtFromTLS()->GetOptimizerConfig()->GetPlanHint()))
	{
		return;
	}

	CMemoryPool *mp = pxfctxt->Pmp();

	// Determine parallel workers: table-level setting > GUC > default
	CTableDescriptor *ptabdesc = popGet->Ptabdesc();
	CMDAccessor *md_accessor = COptCtxt::PoctxtFromTLS()->Pmda();
	const IMDRelation *pmdrel =
		md_accessor->RetrieveRel(ptabdesc->MDId());

	ULONG ulParallelWorkers = 2;  // default
	INT table_parallel_workers = pmdrel->ParallelWorkers();
	if (table_parallel_workers > 0)
		ulParallelWorkers = (ULONG)table_parallel_workers;
	else if (max_parallel_workers_per_gather > 0)
		ulParallelWorkers = (ULONG)max_parallel_workers_per_gather;

	// Mark the optimization context as having parallel operators
	COptCtxt::PoctxtFromTLS()->SetHasParallelOperators();

	// create/extract components for alternative
	CName *pname = GPOS_NEW(mp) CName(mp, popGet->Name());

	ptabdesc->AddRef();

	CColRefArray *pdrgpcrOutput = popGet->PdrgpcrOutput();
	GPOS_ASSERT(nullptr != pdrgpcrOutput);
	pdrgpcrOutput->AddRef();

	CColRef2dArray *pdrgpdrgpcrPart = popGet->PdrgpdrgpcrPart();
	pdrgpdrgpcrPart->AddRef();

	popGet->GetPartitionMdids()->AddRef();
	popGet->GetRootColMappingPerPart()->AddRef();

	// create alternative expression
	CExpression *pexprAlt = GPOS_NEW(mp) CExpression(
		mp,
		GPOS_NEW(mp) CPhysicalParallelDynamicTableScan(
			mp, pname, ptabdesc, popGet->UlOpId(), popGet->ScanId(),
			pdrgpcrOutput, pdrgpdrgpcrPart, popGet->GetPartitionMdids(),
			popGet->GetRootColMappingPerPart(), ulParallelWorkers));

	// add alternative to transformation result
	pxfres->Add(pexprAlt);
}


// EOF
