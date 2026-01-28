//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2026 Hashdata, Inc.
//
//	@filename:
//		CPhysicalPartitionTopK.cpp
//
//	@doc:
//		Implementation of physical PartitionTopK operator
//
//---------------------------------------------------------------------------

#include "gpopt/operators/CPhysicalPartitionTopK.h"

#include "gpos/base.h"

#include "gpopt/base/CCTEReq.h"
#include "gpopt/base/CDistributionSpecAny.h"
#include "gpopt/base/CDistributionSpecHashed.h"
#include "gpopt/base/COptCtxt.h"
#include "gpopt/base/CReqdPropPlan.h"
#include "gpopt/operators/CExpressionHandle.h"

using namespace gpopt;


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalPartitionTopK::CPhysicalPartitionTopK
//
//	@doc:
//		Ctor
//
//---------------------------------------------------------------------------
CPhysicalPartitionTopK::CPhysicalPartitionTopK(CMemoryPool *mp,
											   CColRefArray *pdrgpcrPartition,
											   COrderSpec *pos, INT nTop)
	: CPhysical(mp),
	  m_pdrgpcrPartition(pdrgpcrPartition),
	  m_pos(pos),
	  m_nTop(nTop)
{
	GPOS_ASSERT(nullptr != pdrgpcrPartition);
	GPOS_ASSERT(nullptr != pos);
	GPOS_ASSERT(0 < nTop);

	m_pdrgpcrPartition->AddRef();
	m_pos->AddRef();
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalPartitionTopK::~CPhysicalPartitionTopK
//
//	@doc:
//		Dtor
//
//---------------------------------------------------------------------------
CPhysicalPartitionTopK::~CPhysicalPartitionTopK()
{
	CRefCount::SafeRelease(m_pdrgpcrPartition);
	CRefCount::SafeRelease(m_pos);
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalPartitionTopK::PcrsRequired
//
//	@doc:
//		Compute required output columns of the n-th child
//
//---------------------------------------------------------------------------
CColRefSet *
CPhysicalPartitionTopK::PcrsRequired(CMemoryPool *mp,
									 CExpressionHandle &exprhdl,
									 CColRefSet *pcrsRequired,
									 ULONG
#ifdef GPOS_DEBUG
										 child_index
#endif
									 ,
									 CDrvdPropArray *,	// pdrgpdpCtxt
									 ULONG				// ulOptReq
)
{
	GPOS_ASSERT(0 == child_index);

	// required columns = columns needed by parent + partition cols + order cols
	CColRefSet *pcrs = GPOS_NEW(mp) CColRefSet(mp);
	pcrs->Include(pcrsRequired);

	// add partition columns
	pcrs->Include(m_pdrgpcrPartition);

	// add order columns
	CColRefSet *pcrsSort = m_pos->PcrsUsed(mp);
	pcrs->Include(pcrsSort);
	pcrsSort->Release();

	// intersect with child's output columns
	CColRefSet *pcrsChildOutput = exprhdl.DeriveOutputColumns(0 /*child_index*/);
	pcrs->Intersection(pcrsChildOutput);

	return pcrs;
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalPartitionTopK::PdsRequired
//
//	@doc:
//		Compute required distribution of the n-th child
//
//---------------------------------------------------------------------------
CDistributionSpec *
CPhysicalPartitionTopK::PdsRequired(CMemoryPool *mp, CExpressionHandle &exprhdl,
									CDistributionSpec *pdsRequired,
									ULONG child_index,
									CDrvdPropArray *,  // pdrgpdpCtxt
									ULONG			   // ulOptReq
) const
{
	GPOS_ASSERT(0 == child_index);

	// NEVER return CDistributionSpecAny for child requirement!
	// EXACTLY like CPhysicalSort: pass through parent's distribution requirement
	return PdsPassThru(mp, exprhdl, pdsRequired, child_index);
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalPartitionTopK::PrsRequired
//
//	@doc:
//		Compute required rewindability of the n-th child
//
//---------------------------------------------------------------------------
CRewindabilitySpec *
CPhysicalPartitionTopK::PrsRequired(CMemoryPool *mp,
									CExpressionHandle &exprhdl,	 // exprhdl
									CRewindabilitySpec *,		 // prsRequired
									ULONG
#ifdef GPOS_DEBUG
										child_index
#endif
									,
									CDrvdPropArray *,  // pdrgpdpCtxt
									ULONG			   // ulOptReq
) const
{
	GPOS_ASSERT(0 == child_index);

	if (exprhdl.HasOuterRefs(0))
	{
		return GPOS_NEW(mp)
			CRewindabilitySpec(CRewindabilitySpec::ErtRescannable,
							   CRewindabilitySpec::EmhtNoMotion);
	}
	else
	{
		return GPOS_NEW(mp) CRewindabilitySpec(
			CRewindabilitySpec::ErtNone, CRewindabilitySpec::EmhtNoMotion);
	}
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalPartitionTopK::FProvidesReqdCols
//
//	@doc:
//		Check if required columns are included in output columns
//
//---------------------------------------------------------------------------
BOOL
CPhysicalPartitionTopK::FProvidesReqdCols(CExpressionHandle &exprhdl,
										  CColRefSet *pcrsRequired,
										  ULONG	 // ulOptReq
) const
{
	GPOS_ASSERT(nullptr != pcrsRequired);
	return FUnaryProvidesReqdCols(exprhdl, pcrsRequired);
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalPartitionTopK::PosDerive
//
//	@doc:
//		Derive order property
//
//---------------------------------------------------------------------------
COrderSpec *
CPhysicalPartitionTopK::PosDerive(CMemoryPool *mp, CExpressionHandle &) const
{
	// We don't guarantee any output order.
	return GPOS_NEW(mp) COrderSpec(mp);
}

//---------------------------------------------------------------------------
//	@function:
//		CPhysicalPartitionTopK::PrsDerive
//
//	@doc:
//		Derive rewindability property
//
//---------------------------------------------------------------------------
CRewindabilitySpec *
CPhysicalPartitionTopK::PrsDerive(CMemoryPool *mp,
								  CExpressionHandle &  // exprhdl
) const
{
	// PartitionTopK is not rewindable
	return GPOS_NEW(mp) CRewindabilitySpec(CRewindabilitySpec::ErtNone,
										   CRewindabilitySpec::EmhtNoMotion);
}

//---------------------------------------------------------------------------
//	@function:
//		CPhysicalPartitionTopK::PcteRequired
//
//	@doc:
//		Compute required CTE map of the n-th child
//
//---------------------------------------------------------------------------


CCTEReq *
CPhysicalPartitionTopK::PcteRequired(CMemoryPool *,		   //mp,
									 CExpressionHandle &,  //exprhdl,
									 CCTEReq *pcter,
									 ULONG
#ifdef GPOS_DEBUG
										 child_index
#endif
									 ,
									 CDrvdPropArray *,	//pdrgpdpCtxt,
									 ULONG				//ulOptReq
) const
{
	GPOS_ASSERT(0 == child_index);
	return PcterPushThru(pcter);
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalPartitionTopK::PosRequired
//
//	@doc:
//		Compute required sort order of the n-th child
//
//---------------------------------------------------------------------------
COrderSpec *
CPhysicalPartitionTopK::PosRequired(CMemoryPool *mp,
									CExpressionHandle &,  // exprhdl
									COrderSpec *,		  // posRequired
									ULONG
#ifdef GPOS_DEBUG
										child_index
#endif
									,
									CDrvdPropArray *,  // pdrgpdpCtxt
									ULONG			   // ulOptReq
) const
{
	GPOS_ASSERT(0 == child_index);

	// PartitionTopK performs its own sorting within each partition,
	// so it does not require the input to be pre-sorted.
	// Therefore, we request no particular order from the child.
	return GPOS_NEW(mp) COrderSpec(mp);
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalPartitionTopK::PdsDerive
//
//	@doc:
//		Derive distribution property
//
//---------------------------------------------------------------------------
CDistributionSpec *
CPhysicalPartitionTopK::PdsDerive(CMemoryPool *,
								  CExpressionHandle &exprhdl) const
{
	// PartitionTopK is a streaming operator that does not change data distribution
	// So we derive distribution from the first (and only) child
	return PdsDerivePassThruOuter(exprhdl);
}

//---------------------------------------------------------------------------
//	@function:
//		CPhysicalPartitionTopK::PppsRequired
//
//	@doc:
//		Compute required partition propagation spec for the n-th child
//
//---------------------------------------------------------------------------
CPartitionPropagationSpec *
CPhysicalPartitionTopK::PppsRequired(CMemoryPool *mp,
									 CExpressionHandle &exprhdl,
									 CPartitionPropagationSpec *pppsRequired,
									 ULONG child_index,
									 CDrvdPropArray *,	// pdrgpdpCtxt
									 ULONG				// ulOptReq
) const
{
	GPOS_ASSERT(0 == child_index);
	GPOS_ASSERT(nullptr != pppsRequired);

	// Pass through partition propagation requests to the child.
	// Only allow consumers that are relevant to the child's partition info.
	return CPhysical::PppsRequired(mp, exprhdl, pppsRequired, child_index,
								   nullptr, 0);
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalPartitionTopK::PppsDerive
//
//	@doc:
//		Derive partition propagation spec
//
//---------------------------------------------------------------------------
CPartitionPropagationSpec *
CPhysicalPartitionTopK::PppsDerive(CMemoryPool *mp,
								   CExpressionHandle &exprhdl) const
{
	// PartitionTopK does not introduce new partition consumers or propagators.
	// It simply passes through the child's partition propagation spec.
	return CPhysical::PppsDerive(mp, exprhdl);
}

//---------------------------------------------------------------------------
//	@function:
//		CPhysicalPartitionTopK::FValidContext
//
//	@doc:
//		Check if optimization context is valid
//
//---------------------------------------------------------------------------
BOOL
CPhysicalPartitionTopK::FValidContext(
	CMemoryPool *,			 // mp
	COptimizationContext *,	 // poc
	COptimizationContextArray *pdrgpocChild) const
{
	// PartitionTopK must have exactly one child
	return (1 == pdrgpocChild->Size());
}

//---------------------------------------------------------------------------
//	@function:
//		CPhysicalPartitionTopK::EpetOrder
//
//	@doc:
//		Return order enforcing type for this operator
//
//---------------------------------------------------------------------------
CEnfdProp::EPropEnforcingType
CPhysicalPartitionTopK::EpetOrder(CExpressionHandle &,	// exprhdl
								  const CEnfdOrder *	// peo
) const
{
	// This operator does not enforce any global sort order.
	// If a specific order is required by the parent, it must be enforced separately.
	return CEnfdProp::EpetRequired;
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalSort::EpetDistribution
//
//	@doc:
//		Return the enforcing type for distribution property based on this operator
//
//---------------------------------------------------------------------------
CEnfdProp::EPropEnforcingType
CPhysicalPartitionTopK::EpetDistribution(CExpressionHandle & /*exprhdl*/,
										 const CEnfdDistribution *
#ifdef GPOS_DEBUG
											 ped
#endif	// GPOS_DEBUG
) const
{
	GPOS_ASSERT(nullptr != ped);

	// distribution enforcers have already been added
	return CEnfdProp::EpetUnnecessary;
}

//---------------------------------------------------------------------------
//	@function:
//		CPhysicalPartitionTopK::EpetRewindability
//
//	@doc:
//		Return rewindability enforcing type for this operator
//
//---------------------------------------------------------------------------
CEnfdProp::EPropEnforcingType
CPhysicalPartitionTopK::EpetRewindability(CExpressionHandle &,		  // exprhdl
										  const CEnfdRewindability *  // per
) const
{
	// This operator is not rewindable. If rewindability is required,
	// a spool must be added above or below.
	return CEnfdProp::EpetRequired;
}

//---------------------------------------------------------------------------
//	@function:
//		CPhysicalPartitionTopK::Matches
//
//	@doc:
//		Match function for physical PartitionTopK operators
//
//---------------------------------------------------------------------------
BOOL
CPhysicalPartitionTopK::Matches(COperator *pop) const
{
	GPOS_ASSERT(nullptr != pop);

	if (Eopid() != pop->Eopid())
	{
		return false;
	}

	CPhysicalPartitionTopK *popPTopK = CPhysicalPartitionTopK::PopConvert(pop);

	// Compare top-k value
	if (m_nTop != popPTopK->m_nTop)
	{
		return false;
	}

	// Compare partition columns: must be same number and equivalent by position
	if (!m_pdrgpcrPartition->Equals(popPTopK->m_pdrgpcrPartition))
	{
		return false;
	}

	// Compare order spec
	return m_pos->Matches(popPTopK->m_pos);
}

//---------------------------------------------------------------------------
//	@function:
//		CPhysicalPartitionTopK::HashValue
//
//	@doc:
//		Compute hash value based on partition columns, order spec, and top-k count.
//
//---------------------------------------------------------------------------
ULONG
CPhysicalPartitionTopK::HashValue() const
{
	ULONG ulHash = gpos::CombineHashes(Eopid(), m_nTop);
	ulHash =
		gpos::CombineHashes(ulHash, CUtils::UlHashColArray(m_pdrgpcrPartition));
	ulHash = gpos::CombineHashes(ulHash, m_pos->HashValue());
	return ulHash;
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalPartitionTopK::OsPrint
//
//	@doc:
//		Debug print
//
//---------------------------------------------------------------------------
IOstream &
CPhysicalPartitionTopK::OsPrint(IOstream &os) const
{
	os << SzId() << " TopK=" << m_nTop << " Order: ";
	return m_pos->OsPrint(os);
}

// EOF