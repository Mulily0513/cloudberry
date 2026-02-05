/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

//---------------------------------------------------------------------------
//	@filename:
//		CPhysicalParallelStreamAgg.h
//
//	@doc:
//		Parallel Stream Aggregate operator with explicit worker count
//---------------------------------------------------------------------------
#ifndef GPOS_CPhysicalParallelStreamAgg_H
#define GPOS_CPhysicalParallelStreamAgg_H

#include "gpos/base.h"

#include "gpopt/operators/CPhysicalStreamAgg.h"


namespace gpopt
{
//---------------------------------------------------------------------------
//	@class:
//		CPhysicalParallelStreamAgg
//
//	@doc:
//		Parallel stream-based aggregate operator with explicit worker count
//
//---------------------------------------------------------------------------
class CPhysicalParallelStreamAgg : public CPhysicalStreamAgg
{
private:
	// Helper function for computing required distribution for Local aggregates
	CDistributionSpec *PdsRequiredForLocal(CMemoryPool *mp,
										   CExpressionHandle &exprhdl,
										   CDistributionSpec *pdsRequired,
										   ULONG child_index,
										   ULONG ulOptReq) const;

	// Helper function for computing required distribution for Global aggregates
	CDistributionSpec *PdsRequiredForGlobal(CMemoryPool *mp,
											CExpressionHandle &exprhdl,
											CDistributionSpec *pdsRequired,
											ULONG child_index,
											ULONG ulOptReq) const;
public:
	CPhysicalParallelStreamAgg(const CPhysicalParallelStreamAgg &) = delete;

	// ctor
	CPhysicalParallelStreamAgg(CMemoryPool *mp, CColRefArray *colref_array,
							   CColRefArray *pdrgpcrMinimal,
							   COperator::EGbAggType egbaggtype,
							   BOOL fGeneratesDuplicates,
							   CColRefArray *pdrgpcrArgDQA, BOOL fMultiStage,
							   BOOL isAggFromSplitDQA,
							   CLogicalGbAgg::EAggStage aggStage,
							   BOOL isAggPushdown,
							   BOOL should_enforce_distribution,
							   ULONG ulParallelWorkers);

	// dtor
	~CPhysicalParallelStreamAgg() override;

	// ident accessors
	EOperatorId
	Eopid() const override
	{
		return EopPhysicalParallelStreamAgg;
	}

	// return a string for operator name
	const CHAR *
	SzId() const override
	{
		return "CPhysicalParallelStreamAgg";
	}

	// conversion function
	static CPhysicalParallelStreamAgg *
	PopConvert(COperator *pop)
	{
		GPOS_ASSERT(nullptr != pop);
		GPOS_ASSERT(EopPhysicalParallelStreamAgg == pop->Eopid());

		return dynamic_cast<CPhysicalParallelStreamAgg *>(pop);
	}

	// match function
	BOOL Matches(COperator *pop) const override;

	// hash function
	ULONG HashValue() const override;

	// compute required distribution of the n-th child
	CDistributionSpec *PdsRequired(CMemoryPool *mp, CExpressionHandle &exprhdl,
								   CDistributionSpec *pdsRequired,
								   ULONG child_index, CDrvdPropArray *pdrgpdpCtxt,
								   ULONG ulOptReq) const override;
	BOOL FValidContext(CMemoryPool *mp, COptimizationContext *poc,
					   COptimizationContextArray *pdrgpocChild) const override;

};	// class CPhysicalParallelStreamAgg

}  // namespace gpopt

#endif	// !GPOS_CPhysicalParallelStreamAgg_H

// EOF
