//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (c) 2025, HashData Technology Limited.
//
//	@filename:
//		CPhysicalParallelDynamicTableScan.h
//
//	@doc:
//		Parallel dynamic table scan operator
//---------------------------------------------------------------------------
#ifndef GPOPT_CPhysicalParallelDynamicTableScan_H
#define GPOPT_CPhysicalParallelDynamicTableScan_H

#include "gpos/base.h"

#include "gpopt/operators/CPhysicalDynamicTableScan.h"

namespace gpopt
{
//---------------------------------------------------------------------------
//	@class:
//		CPhysicalParallelDynamicTableScan
//
//	@doc:
//		Parallel dynamic table scan operator.  Multiple workers within a
//		segment concurrently scan different partitions.
//
//---------------------------------------------------------------------------
class CPhysicalParallelDynamicTableScan : public CPhysicalDynamicTableScan
{
private:
	// number of parallel workers
	ULONG m_ulParallelWorkers;

	// worker-level distribution spec
	CDistributionSpec *m_pdsWorkerDistribution;

public:
	CPhysicalParallelDynamicTableScan(
		const CPhysicalParallelDynamicTableScan &) = delete;

	// ctor
	CPhysicalParallelDynamicTableScan(
		CMemoryPool *mp, const CName *pnameAlias,
		CTableDescriptor *ptabdesc, ULONG ulOriginOpId, ULONG scan_id,
		CColRefArray *pdrgpcrOutput, CColRef2dArray *pdrgpdrgpcrParts,
		IMdIdArray *partition_mdids,
		ColRefToUlongMapArray *root_col_mapping_per_part,
		ULONG ulParallelWorkers);

	// dtor
	~CPhysicalParallelDynamicTableScan() override;

	// ident accessors
	EOperatorId
	Eopid() const override
	{
		return EopPhysicalParallelDynamicTableScan;
	}

	// return a string for operator name
	const CHAR *
	SzId() const override
	{
		return "CPhysicalParallelDynamicTableScan";
	}

	// number of parallel workers
	ULONG
	UlParallelWorkers() const
	{
		return m_ulParallelWorkers;
	}

	// match function
	BOOL Matches(COperator *) const override;

	// hash function
	ULONG HashValue() const override;

	// debug print
	IOstream &OsPrint(IOstream &os) const override;

	// conversion function
	static CPhysicalParallelDynamicTableScan *
	PopConvert(COperator *pop)
	{
		GPOS_ASSERT(nullptr != pop);
		GPOS_ASSERT(EopPhysicalParallelDynamicTableScan == pop->Eopid());

		return dynamic_cast<CPhysicalParallelDynamicTableScan *>(pop);
	}

	// derive rewindability — parallel scans are not rewindable
	CRewindabilitySpec *
	PrsDerive(CMemoryPool *mp,
			  CExpressionHandle &  // exprhdl
	) const override
	{
		return GPOS_NEW(mp)
			CRewindabilitySpec(CRewindabilitySpec::ErtNone,
							   CRewindabilitySpec::EmhtNoMotion);
	}

	// derive distribution
	CDistributionSpec *PdsDerive(CMemoryPool *mp,
								 CExpressionHandle &exprhdl) const override;

	// return distribution property enforcing type for this operator
	CEnfdProp::EPropEnforcingType EpetDistribution(
		CExpressionHandle &exprhdl,
		const CEnfdDistribution *ped) const override;

	// return rewindability property enforcing type for this operator
	CEnfdProp::EPropEnforcingType EpetRewindability(
		CExpressionHandle &exprhdl,
		const CEnfdRewindability *per) const override;

	// check if optimization context is valid
	// Reject if parent requires REWINDABLE (e.g., for NL Join inner child)
	BOOL FValidContext(CMemoryPool *mp, COptimizationContext *poc,
					   COptimizationContextArray *pdrgpocChild) const override;

	// return true if operator passes through stats obtained from children,
	// this is used when computing stats during costing
	BOOL
	FPassThruStats() const override
	{
		return false;
	}

};	// class CPhysicalParallelDynamicTableScan

}  // namespace gpopt

#endif	// !GPOPT_CPhysicalParallelDynamicTableScan_H

// EOF
