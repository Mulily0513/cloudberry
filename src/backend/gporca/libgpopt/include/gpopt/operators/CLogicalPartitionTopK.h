//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2026 Hashdata, Inc.
//
//	@filename:
//		CLogicalPartitionTopK.h
//
//	@doc:
//		Logical operator for Top-K within window partitions
//
//---------------------------------------------------------------------------

#ifndef GPOPT_CLogicalPartitionTopK_H
#define GPOPT_CLogicalPartitionTopK_H

#include "gpos/base.h"

#include "gpopt/operators/CLogical.h"

namespace gpopt
{
class CLogicalPartitionTopK : public CLogical
{
private:
	CColRefArray *m_pdrgpcrPartitionBy;
	COrderSpec *m_pos;
	INT m_nTop;

public:
	// Pattern constructor (used in Xform patterns)
	explicit CLogicalPartitionTopK(CMemoryPool *mp);

	// ctor
	CLogicalPartitionTopK(CMemoryPool *mp, CColRefArray *partition_by,
						  COrderSpec *pos, INT nTop);

	// dtor
	virtual ~CLogicalPartitionTopK() override;

	// identification
	virtual EOperatorId
	Eopid() const override
	{
		return EopLogicalPartitionTopK;
	}
	virtual const CHAR *
	SzId() const override
	{
		return "CLogicalPartitionTopK";
	}

	// matching and hashing
	BOOL Matches(COperator *pop) const override;
	ULONG HashValue() const override;

	// derived properties
	CColRefSet *DeriveOutputColumns(CMemoryPool *mp,
									CExpressionHandle &exprhdl) override;
	CPartInfo *DerivePartitionInfo(CMemoryPool *mp,
								   CExpressionHandle &exprhdl) const override;
	CPropConstraint *DerivePropertyConstraint(
		CMemoryPool *mp, CExpressionHandle &exprhdl) const override;

	// stats
	IStatistics *PstatsDerive(CMemoryPool *mp, CExpressionHandle &exprhdl,
							  IStatisticsArray *stats_ctxt) const override;
	virtual EStatPromise Esp(CExpressionHandle &) const override;
	virtual CColRefSet *PcrsStat(CMemoryPool *mp, CExpressionHandle &exprhdl,
								 CColRefSet *pcrsInput,
								 ULONG child_index) const override;

	// xform candidates
	CXformSet *PxfsCandidates(CMemoryPool *mp) const override;

	// input order sensitivity
	BOOL FInputOrderSensitive() const override;

	// column remapping
	COperator *PopCopyWithRemappedColumns(CMemoryPool *mp,
										  UlongToColRefMap *colref_mapping,
										  BOOL must_exist) override;

	// accessors
	CColRefArray *
	GetPartitionKeys() const
	{
		return m_pdrgpcrPartitionBy;
	}
	COrderSpec *
	GetOrderSpec() const
	{
		return m_pos;
	}
	INT
	GetTopN() const
	{
		return m_nTop;
	}

	// debug print
	IOstream &OsPrint(IOstream &os) const override;

	// conversion function
	static CLogicalPartitionTopK *
	PopConvert(COperator *pop)
	{
		GPOS_ASSERT(nullptr != pop);
		GPOS_ASSERT(EopLogicalPartitionTopK == pop->Eopid());
		return dynamic_cast<CLogicalPartitionTopK *>(pop);
	}

};	// class CLogicalPartitionTopK
}  // namespace gpopt
#endif	// GPOPT_CLogicalPartitionTopK_H