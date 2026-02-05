//	Greenplum Database
//	Copyright (C) 2016 VMware, Inc. or its affiliates.

#ifndef GPOPT_CPhysicalParallelUnionAll_H
#define GPOPT_CPhysicalParallelUnionAll_H

#include "gpopt/operators/CPhysicalUnionAll.h"

namespace gpopt
{
// Operator that implements logical union all with intra-segment (worker-level)
// parallelism. This enables Union children to use parallel table scans and
// propagates worker-level distribution through the Union operator.
// See gpopt::CPhysicalSerialUnionAll for its serial sibling.
class CPhysicalParallelUnionAll : public CPhysicalUnionAll
{
private:
	// Number of parallel workers for intra-segment parallelism (must be > 0)
	ULONG m_ulParallelWorkers;

	// Derive worker-level distribution if all children have compatible worker-level distribution
	// Supports both EdtWorkerRandom and EdtHashedWorker
	// Returns nullptr if not applicable
	CDistributionSpec *PdsWorkerDerive(CMemoryPool *mp,
									   CExpressionHandle &exprhdl) const;

public:
	CPhysicalParallelUnionAll(const CPhysicalParallelUnionAll &) = delete;

	// Constructor with parallel workers (required)
	CPhysicalParallelUnionAll(CMemoryPool *mp, CColRefArray *pdrgpcrOutput,
							  CColRef2dArray *pdrgpdrgpcrInput,
							  ULONG ulParallelWorkers);

	EOperatorId Eopid() const override;

	const CHAR *SzId() const override;

	// Accessor for parallel workers count
	ULONG
	UlParallelWorkers() const
	{
		return m_ulParallelWorkers;
	}

	// Distribution requirement - accept any distribution (validation in FValidContext)
	CDistributionSpec *PdsRequired(CMemoryPool *mp, CExpressionHandle &exprhdl,
								   CDistributionSpec *pdsRequired,
								   ULONG child_index,
								   CDrvdPropArray *pdrgpdpCtxt,
								   ULONG ulOptReq) const override;

	CEnfdDistribution::EDistributionMatching Edm(
		CReqdPropPlan *,   // prppInput
		ULONG,			   // child_index
		CDrvdPropArray *,  // pdrgpdpCtxt
		ULONG			   // ulOptReq
		) override;

	// derive distribution
	CDistributionSpec *PdsDerive(CMemoryPool *mp,
								 CExpressionHandle &exprhdl) const override;

	// Check if optimization context is valid (no motion under children,
	// children must have worker-level distribution)
	BOOL FValidContext(CMemoryPool *mp, COptimizationContext *poc,
					   COptimizationContextArray *pdrgpocChild) const override;

	// Conversion function
	static CPhysicalParallelUnionAll *PopConvert(COperator *pop);

	~CPhysicalParallelUnionAll() override = default;
};
}  // namespace gpopt

#endif	//GPOPT_CPhysicalParallelUnionAll_H
