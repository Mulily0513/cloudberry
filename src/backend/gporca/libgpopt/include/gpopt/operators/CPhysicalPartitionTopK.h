//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2026 Hashdata, Inc.
//
//	@filename:
//		CPhysicalPartitionTopK.h
//
//	@doc:
//		Physical operator for Top-N per window partition
//
//---------------------------------------------------------------------------

#ifndef GPOPT_CPhysicalPartitionTopK_H
#define GPOPT_CPhysicalPartitionTopK_H

#include "gpos/base.h"

#include "gpopt/base/CColRef.h"
#include "gpopt/base/COrderSpec.h"
#include "gpopt/operators/CPhysical.h"

namespace gpopt
{

//---------------------------------------------------------------------------
//	@class:
//		CPhysicalPartitionTopK
//
//	@doc:
//		Physical PartitionTopK operator that limits output to top N rows
//		within each partition defined by partition columns and order spec.
//
//---------------------------------------------------------------------------
class CPhysicalPartitionTopK : public CPhysical
{
private:
	// partition columns
	CColRefArray *m_pdrgpcrPartition;

	// order specification
	COrderSpec *m_pos;

	// number of rows to keep per partition
	INT m_nTop;

	// private copy ctor
	CPhysicalPartitionTopK(const CPhysicalPartitionTopK &) = delete;

public:
	// ctor
	CPhysicalPartitionTopK(CMemoryPool *mp, CColRefArray *pdrgpcrPartition,
						   COrderSpec *pos, INT nTop);

	// dtor
	~CPhysicalPartitionTopK() override;

	// ident accessors
	EOperatorId
	Eopid() const override
	{
		return EopPhysicalPartitionTopK;
	}

	const CHAR *
	SzId() const override
	{
		return "CPhysicalPartitionTopK";
	}

	// accessors
	CColRefArray *
	PdrgpcrPartition() const
	{
		return m_pdrgpcrPartition;
	}

	COrderSpec *
	Pos() const
	{
		return m_pos;
	}

	INT
	N() const
	{
		return m_nTop;
	}

	// COperator overrides
	BOOL Matches(COperator *pop) const override;

	// hash function
	ULONG HashValue() const override;

	BOOL
	FInputOrderSensitive() const override
	{
		return true;
	}

	//-------------------------------------------------------------------------------------
	// Required Plan Properties
	//-------------------------------------------------------------------------------------

	// compute required output columns of the n-th child
	CColRefSet *PcrsRequired(CMemoryPool *mp, CExpressionHandle &exprhdl,
							 CColRefSet *pcrsRequired, ULONG child_index,
							 CDrvdPropArray *pdrgpdpCtxt,
							 ULONG ulOptReq) override;

	// compute required ctes of the n-th child
	CCTEReq *PcteRequired(CMemoryPool *mp, CExpressionHandle &exprhdl,
						  CCTEReq *pcter, ULONG child_index,
						  CDrvdPropArray *pdrgpdpCtxt,
						  ULONG ulOptReq) const override;

	// compute required sort order of the n-th child
	COrderSpec *PosRequired(CMemoryPool *mp, CExpressionHandle &exprhdl,
							COrderSpec *posRequired, ULONG child_index,
							CDrvdPropArray *pdrgpdpCtxt,
							ULONG ulOptReq) const override;

	// compute required distribution of the n-th child
	CDistributionSpec *PdsRequired(CMemoryPool *mp, CExpressionHandle &exprhdl,
								   CDistributionSpec *pdsRequired,
								   ULONG child_index,
								   CDrvdPropArray *pdrgpdpCtxt,
								   ULONG ulOptReq) const override;

	// compute required rewindability of the n-th child
	CRewindabilitySpec *PrsRequired(CMemoryPool *mp, CExpressionHandle &exprhdl,
									CRewindabilitySpec *prsRequired,
									ULONG child_index,
									CDrvdPropArray *pdrgpdpCtxt,
									ULONG ulOptReq) const override;

	// check if required columns are included in output columns
	BOOL FProvidesReqdCols(CExpressionHandle &exprhdl, CColRefSet *pcrsRequired,
						   ULONG ulOptReq) const override;

	//-------------------------------------------------------------------------------------
	// Derived Plan Properties
	//-------------------------------------------------------------------------------------

	// derive sort order
	COrderSpec *PosDerive(CMemoryPool *mp,
						  CExpressionHandle &exprhdl) const override;

	// derive distribution
	CDistributionSpec *PdsDerive(CMemoryPool *mp,
								 CExpressionHandle &exprhdl) const override;

	// derive rewindability
	CRewindabilitySpec *PrsDerive(CMemoryPool *mp,
								  CExpressionHandle &exprhdl) const override;

	// compute required partition propagation of the n-th child
	CPartitionPropagationSpec *PppsRequired(
		CMemoryPool *mp, CExpressionHandle &exprhdl,
		CPartitionPropagationSpec *pppsRequired, ULONG child_index,
		CDrvdPropArray *pdrgpdpCtxt, ULONG ulOptReq) const override;

	// derive partition propagation spec
	CPartitionPropagationSpec *PppsDerive(
		CMemoryPool *mp, CExpressionHandle &exprhdl) const override;

	// check if optimization context is valid
	BOOL FValidContext(CMemoryPool *mp, COptimizationContext *poc,
					   COptimizationContextArray *pdrgpocChild) const override;

	//-------------------------------------------------------------------------------------
	// Enforced Properties
	//-------------------------------------------------------------------------------------

	// return order property enforcing type for this operator
	CEnfdProp::EPropEnforcingType EpetOrder(
		CExpressionHandle &exprhdl, const CEnfdOrder *peo) const override;

	// return rewindability property enforcing type for this operator
	CEnfdProp::EPropEnforcingType EpetRewindability(
		CExpressionHandle &exprhdl,
		const CEnfdRewindability *per) const override;

	// return distribution property enforcing type for this operator
	CEnfdProp::EPropEnforcingType EpetDistribution(
		CExpressionHandle &exprhdl,
		const CEnfdDistribution *ped) const override;

	// return true if operator passes through stats obtained from children
	BOOL
	FPassThruStats() const override
	{
		return false;
	}

	// debug print
	IOstream &OsPrint(IOstream &os) const override;

	//-------------------------------------------------------------------------------------
	// Conversion function
	//-------------------------------------------------------------------------------------

	static CPhysicalPartitionTopK *
	PopConvert(COperator *pop)
	{
		GPOS_ASSERT(nullptr != pop);
		GPOS_ASSERT(EopPhysicalPartitionTopK == pop->Eopid());
		return dynamic_cast<CPhysicalPartitionTopK *>(pop);
	}

};	// class CPhysicalPartitionTopK

}  // namespace gpopt

#endif	// !GPOPT_CPhysicalPartitionTopK_H

// EOF