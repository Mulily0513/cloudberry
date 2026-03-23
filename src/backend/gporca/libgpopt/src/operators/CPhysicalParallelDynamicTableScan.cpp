//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (c) 2025, HashData Technology Limited.
//
//	@filename:
//		CPhysicalParallelDynamicTableScan.cpp
//
//	@doc:
//		Implementation of parallel dynamic table scan operator
//---------------------------------------------------------------------------

#include "gpopt/operators/CPhysicalParallelDynamicTableScan.h"

#include "gpos/base.h"

#include "gpopt/base/CDistributionSpec.h"
#include "gpopt/base/CDistributionSpecWorkerRandom.h"
#include "gpopt/base/CDrvdPropPlan.h"
#include "gpopt/base/CEnfdDistribution.h"
#include "gpopt/base/CEnfdRewindability.h"
#include "gpopt/base/COptimizationContext.h"
#include "gpopt/base/CRewindabilitySpec.h"
#include "gpopt/base/CUtils.h"
#include "gpopt/metadata/CName.h"
#include "gpopt/metadata/CTableDescriptor.h"
#include "gpopt/operators/CExpressionHandle.h"

using namespace gpopt;


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalParallelDynamicTableScan::CPhysicalParallelDynamicTableScan
//
//	@doc:
//		Ctor
//
//---------------------------------------------------------------------------
CPhysicalParallelDynamicTableScan::CPhysicalParallelDynamicTableScan(
	CMemoryPool *mp, const CName *pnameAlias, CTableDescriptor *ptabdesc,
	ULONG ulOriginOpId, ULONG scan_id, CColRefArray *pdrgpcrOutput,
	CColRef2dArray *pdrgpdrgpcrParts, IMdIdArray *partition_mdids,
	ColRefToUlongMapArray *root_col_mapping_per_part,
	ULONG ulParallelWorkers)
	: CPhysicalDynamicTableScan(mp, pnameAlias, ptabdesc, ulOriginOpId,
								scan_id, pdrgpcrOutput, pdrgpdrgpcrParts,
								partition_mdids, root_col_mapping_per_part),
	  m_ulParallelWorkers(ulParallelWorkers),
	  m_pdsWorkerDistribution(nullptr)
{
	GPOS_ASSERT(ulParallelWorkers > 0);

	// Create worker-level distribution based on table's segment distribution
	if (ulParallelWorkers > 0 && nullptr != m_pds)
	{
		m_pdsWorkerDistribution =
			CDistributionSpecWorkerRandom::PdsCreateWorkerRandom(
				mp, ulParallelWorkers, m_pds);
	}
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalParallelDynamicTableScan::~CPhysicalParallelDynamicTableScan
//
//	@doc:
//		Dtor
//
//---------------------------------------------------------------------------
CPhysicalParallelDynamicTableScan::~CPhysicalParallelDynamicTableScan()
{
	CRefCount::SafeRelease(m_pdsWorkerDistribution);
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalParallelDynamicTableScan::HashValue
//
//	@doc:
//		Combine hash values of operator id, table descriptor, and worker count
//
//---------------------------------------------------------------------------
ULONG
CPhysicalParallelDynamicTableScan::HashValue() const
{
	ULONG ulHash = gpos::CombineHashes(
		COperator::HashValue(),
		gpos::CombineHashes(gpos::HashValue(&m_ulParallelWorkers),
							Ptabdesc()->MDId()->HashValue()));
	ulHash =
		gpos::CombineHashes(ulHash, CUtils::UlHashColArray(PdrgpcrOutput()));

	return ulHash;
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalParallelDynamicTableScan::Matches
//
//	@doc:
//		match operator
//
//---------------------------------------------------------------------------
BOOL
CPhysicalParallelDynamicTableScan::Matches(COperator *pop) const
{
	if (Eopid() != pop->Eopid())
	{
		return false;
	}

	CPhysicalParallelDynamicTableScan *popScan =
		CPhysicalParallelDynamicTableScan::PopConvert(pop);

	return m_ulParallelWorkers == popScan->m_ulParallelWorkers &&
		   CUtils::FMatchDynamicScan(this, pop);
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalParallelDynamicTableScan::OsPrint
//
//	@doc:
//		debug print
//
//---------------------------------------------------------------------------
IOstream &
CPhysicalParallelDynamicTableScan::OsPrint(IOstream &os) const
{
	os << SzId() << " ";
	os << "Table: (";
	Ptabdesc()->Name().OsPrint(os);
	os << ") ";
	os << "ParallelWorkers: " << m_ulParallelWorkers;

	return os;
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalParallelDynamicTableScan::PdsDerive
//
//	@doc:
//		Derive distribution for parallel dynamic table scan
//
//---------------------------------------------------------------------------
CDistributionSpec *
CPhysicalParallelDynamicTableScan::PdsDerive(CMemoryPool *mp,
											  CExpressionHandle &exprhdl) const
{
	// If we have a pre-computed worker distribution, use it
	if (nullptr != m_pdsWorkerDistribution)
	{
		m_pdsWorkerDistribution->AddRef();
		return m_pdsWorkerDistribution;
	}

	// Otherwise, derive from the base physical scan
	return CPhysicalScan::PdsDerive(mp, exprhdl);
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalParallelDynamicTableScan::EpetDistribution
//
//	@doc:
//		Return the enforcing type for distribution property based on this
//		operator
//
//---------------------------------------------------------------------------
CEnfdProp::EPropEnforcingType
CPhysicalParallelDynamicTableScan::EpetDistribution(
	CExpressionHandle & /*exprhdl*/, const CEnfdDistribution *ped) const
{
	GPOS_ASSERT(nullptr != ped);

	// Check if worker-level distribution can satisfy the requirement
	if (nullptr != m_pdsWorkerDistribution &&
		ped->FCompatible(m_pdsWorkerDistribution))
	{
		return CEnfdProp::EpetUnnecessary;
	}

	// Neither distribution satisfies the requirement
	return CEnfdProp::EpetRequired;
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalParallelDynamicTableScan::EpetRewindability
//
//	@doc:
//		Return rewindability property enforcing type for this operator.
//		Parallel scans cannot be rewound, so always require a spool.
//
//---------------------------------------------------------------------------
CEnfdProp::EPropEnforcingType
CPhysicalParallelDynamicTableScan::EpetRewindability(
	CExpressionHandle &exprhdl, const CEnfdRewindability *per) const
{
	GPOS_ASSERT(nullptr != per);

	CRewindabilitySpec *prs = CDrvdPropPlan::Pdpplan(exprhdl.Pdp())->Prs();
	if (per->FCompatible(prs))
	{
		return CEnfdProp::EpetUnnecessary;
	}

	// Cannot satisfy the rewindability requirement
	// Check if requirement originates from NL Join
	if (per->PrsRequired()->IsOriginNLJoin())
	{
		// Prohibit — NL Join cannot work with parallel scan
		return CEnfdProp::EpetProhibited;
	}

	// For other contexts, allow enforcement with Spool
	return CEnfdProp::EpetRequired;
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalParallelDynamicTableScan::FValidContext
//
//	@doc:
//		Check if optimization context is valid.
//		Reject if parent requires REWINDABLE (e.g., for NL Join inner child)
//		because ParallelDynamicTableScan derives NONE (not rewindable).
//
//---------------------------------------------------------------------------
BOOL
CPhysicalParallelDynamicTableScan::FValidContext(
	CMemoryPool *, COptimizationContext *poc,
	COptimizationContextArray *) const
{
	GPOS_ASSERT(nullptr != poc);

	CReqdPropPlan *prpp = poc->Prpp();
	CRewindabilitySpec *prsRequired = prpp->Per()->PrsRequired();

	if (prsRequired->IsOriginNLJoin())
	{
		return false;
	}

	return true;
}

// EOF
