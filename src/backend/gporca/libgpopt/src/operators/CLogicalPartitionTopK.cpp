//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2026 Hashdata, Inc.
//
//	@filename:
//		CLogicalPartitionTopK.cpp
//
//	@doc:
//		Implementation of logical PartitionTopK operator
//
//---------------------------------------------------------------------------

#include "gpopt/operators/CLogicalPartitionTopK.h"

#include "gpopt/base/CUtils.h"
#include "gpopt/operators/CExpressionHandle.h"
#include "gpopt/xforms/CXform.h"

using namespace gpopt;

// Pattern constructor — used only in Xform patterns (wildcard matching)
CLogicalPartitionTopK::CLogicalPartitionTopK(CMemoryPool *mp)
	: CLogical(mp), m_pdrgpcrPartitionBy(nullptr), m_pos(nullptr), m_nTop(-1)
{
	m_fPattern = true;
}

//---------------------------------------------------------------------------
//	@function:
//		CLogicalPartitionTopK::CLogicalPartitionTopK
//---------------------------------------------------------------------------
CLogicalPartitionTopK::CLogicalPartitionTopK(CMemoryPool *mp,
											 CColRefArray *partition_by,
											 COrderSpec *pos, INT nTop)
	: CLogical(mp), m_pdrgpcrPartitionBy(partition_by), m_pos(pos), m_nTop(nTop)
{
	GPOS_ASSERT(nullptr != partition_by);
	GPOS_ASSERT(nullptr != pos);
	GPOS_ASSERT(0 < nTop);

	// Register locally-used columns so column pruning preserves them
	CColRefSet *pcrsSort = m_pos->PcrsUsed(mp);
	m_pcrsLocalUsed->Include(pcrsSort);
	pcrsSort->Release();
	m_pcrsLocalUsed->Include(m_pdrgpcrPartitionBy);
}


//---------------------------------------------------------------------------
//	@function:
//		CLogicalPartitionTopK::～CLogicalPartitionTopK
//---------------------------------------------------------------------------
CLogicalPartitionTopK::~CLogicalPartitionTopK()
{
	CRefCount::SafeRelease(m_pdrgpcrPartitionBy);
	CRefCount::SafeRelease(m_pos);
}


//---------------------------------------------------------------------------
//	@function:
//		CLogicalPartitionTopK::Matches
//---------------------------------------------------------------------------
BOOL
CLogicalPartitionTopK::Matches(COperator *pop) const
{
	if (pop->Eopid() != Eopid())
	{
		return false;
	}

	/* Pattern instances have NULL members; match by operator type only */
	if (nullptr == m_pos || nullptr == m_pdrgpcrPartitionBy)
	{
		return true;
	}

	CLogicalPartitionTopK *popTopK = CLogicalPartitionTopK::PopConvert(pop);

	if (nullptr == popTopK->m_pos || nullptr == popTopK->m_pdrgpcrPartitionBy)
	{
		return true;
	}

	return m_pos->Matches(popTopK->m_pos) &&
		   m_pdrgpcrPartitionBy->Equals(popTopK->m_pdrgpcrPartitionBy) &&
		   m_nTop == popTopK->m_nTop;
}


//---------------------------------------------------------------------------
//	@function:
//		CLogicalPartitionTopK::HashValue
//---------------------------------------------------------------------------
ULONG
CLogicalPartitionTopK::HashValue() const
{
	if (nullptr == m_pos || nullptr == m_pdrgpcrPartitionBy)
	{
		return gpos::CombineHashes(Eopid(), 0);
	}

	ULONG ulHash = gpos::CombineHashes(
		m_pos->HashValue(), CUtils::UlHashColArray(m_pdrgpcrPartitionBy));
	ulHash = gpos::CombineHashes(ulHash, static_cast<ULONG>(m_nTop));
	return ulHash;
}


//---------------------------------------------------------------------------
//	@function:
//		CLogicalPartitionTopK::DeriveOutputColumns
//---------------------------------------------------------------------------
CColRefSet *
CLogicalPartitionTopK::DeriveOutputColumns(CMemoryPool *mp,
										   CExpressionHandle &exprhdl)
{
	CColRefSet *child_cols = exprhdl.DeriveOutputColumns(0);
	return GPOS_NEW(mp) CColRefSet(mp, *child_cols);
}


//---------------------------------------------------------------------------
//	@function:
//		CLogicalPartitionTopK::DerivePartitionInfo
//---------------------------------------------------------------------------
CPartInfo *
CLogicalPartitionTopK::DerivePartitionInfo(CMemoryPool *,
										   CExpressionHandle &exprhdl) const
{
	CPartInfo *ppartinfo = exprhdl.DerivePartitionInfo(0);
	GPOS_ASSERT(nullptr != ppartinfo);
	ppartinfo->AddRef();
	return ppartinfo;
}

//---------------------------------------------------------------------------
//	@function:
//		CLogicalPartitionTopK::DerivePropertyConstraint
//---------------------------------------------------------------------------
CPropConstraint *
CLogicalPartitionTopK::DerivePropertyConstraint(
	CMemoryPool *, CExpressionHandle &exprhdl) const
{
	CPropConstraint *ppc = exprhdl.DerivePropertyConstraint(0);
	if (nullptr != ppc)
	{
		ppc->AddRef();
	}
	return ppc;
}


//---------------------------------------------------------------------------
//	@function:
//		CLogicalPartitionTopK::PstatsDerive
//---------------------------------------------------------------------------
IStatistics *
CLogicalPartitionTopK::PstatsDerive(CMemoryPool *mp, CExpressionHandle &exprhdl,
									IStatisticsArray *) const
{
	IStatistics *child_stats = exprhdl.Pstats(0);
	if (nullptr != child_stats)
	{
		return child_stats->CopyStats(mp);
	}
	return nullptr;
}


//---------------------------------------------------------------------------
//	@function:
//		CLogicalPartitionTopK::Esp
//---------------------------------------------------------------------------
CLogical::EStatPromise
CLogicalPartitionTopK::Esp(CExpressionHandle &) const
{
	return CLogical::EspHigh;
}


//---------------------------------------------------------------------------
//	@function:
//		CLogicalPartitionTopK::PcrsStat
//---------------------------------------------------------------------------
CColRefSet *
CLogicalPartitionTopK::PcrsStat(CMemoryPool *mp, CExpressionHandle &exprhdl,
								CColRefSet *, ULONG) const
{
	CColRefSet *pcrs = GPOS_NEW(mp) CColRefSet(mp);
	pcrs->Include(exprhdl.DeriveOutputColumns(0));
	return pcrs;
}

//---------------------------------------------------------------------------
//	@function:
//		CLogicalPartitionTopK::PxfsCandidates
//---------------------------------------------------------------------------
CXformSet *
CLogicalPartitionTopK::PxfsCandidates(CMemoryPool *mp) const
{
	CXformSet *xform_set = GPOS_NEW(mp) CXformSet(mp);
	(void) xform_set->ExchangeSet(CXform::ExfImplementPartitionTopK);
	return xform_set;
}


//---------------------------------------------------------------------------
//	@function:
//		CLogicalPartitionTopK::FInputOrderSensitive
//---------------------------------------------------------------------------
BOOL
CLogicalPartitionTopK::FInputOrderSensitive() const
{
	return false;  // Physical implementation handles ordering internally
}


//---------------------------------------------------------------------------
//	@function:
//		CLogicalPartitionTopK::OsPrint
//
//	@doc:
//		Debug print
//
//---------------------------------------------------------------------------
IOstream &
CLogicalPartitionTopK::OsPrint(IOstream &os) const
{
	os << SzId() << " TopK=" << m_nTop;
	if (nullptr != m_pos)
	{
		os << " Order: ";
		m_pos->OsPrint(os);
	}
	return os;
}


//---------------------------------------------------------------------------
//	@function:
//		CLogicalPartitionTopK::PopCopyWithRemappedColumns
//---------------------------------------------------------------------------
COperator *
CLogicalPartitionTopK::PopCopyWithRemappedColumns(
	CMemoryPool *mp, UlongToColRefMap *colref_mapping, BOOL must_exist)
{
	CColRefArray *pdrgpcrPart = CUtils::PdrgpcrRemap(
		mp, m_pdrgpcrPartitionBy, colref_mapping, must_exist);
	COrderSpec *pos =
		m_pos->PosCopyWithRemappedColumns(mp, colref_mapping, must_exist);
	return GPOS_NEW(mp) CLogicalPartitionTopK(mp, pdrgpcrPart, pos, m_nTop);
}