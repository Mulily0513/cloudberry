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
//		CPhysicalParallelRightOuterHashJoin.cpp
//
//	@doc:
//		Implementation of parallel right outer hash join operator
//---------------------------------------------------------------------------

#include "gpopt/operators/CPhysicalParallelRightOuterHashJoin.h"

#include "gpos/base.h"

#include "gpopt/base/CDistributionSpecWorkerRandom.h"
#include "gpopt/base/CUtils.h"
#include "gpopt/operators/CExpressionHandle.h"

using namespace gpopt;


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalParallelRightOuterHashJoin::CPhysicalParallelRightOuterHashJoin
//
//	@doc:
//		Ctor
//
//---------------------------------------------------------------------------
CPhysicalParallelRightOuterHashJoin::CPhysicalParallelRightOuterHashJoin(
	CMemoryPool *mp,
	CExpressionArray *pdrgpexprOuterKeys,
	CExpressionArray *pdrgpexprInnerKeys,
	IMdIdArray *hash_opfamilies,
	BOOL is_null_aware,
	CXform::EXformId origin_xform
)
	: CPhysicalParallelHashJoin(mp, pdrgpexprOuterKeys, pdrgpexprInnerKeys,
								hash_opfamilies, is_null_aware, origin_xform)
{
	SetDistrRequests(NumDistrReq());
	SetPartPropagateRequests(2);
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalParallelRightOuterHashJoin::~CPhysicalParallelRightOuterHashJoin
//
//	@doc:
//		Dtor
//
//---------------------------------------------------------------------------
CPhysicalParallelRightOuterHashJoin::~CPhysicalParallelRightOuterHashJoin() = default;


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalParallelRightOuterHashJoin::PdsDerive
//
//	@doc:
//		Derive distribution
//
//		For Right Outer Join, the non-nullable side is the right (inner/build)
//		child. During the probe phase, matched rows from the inner side are
//		emitted. During the PHJ_BATCH_SCAN phase, unmatched inner rows are
//		emitted. The output distribution follows the inner (build) child.
//
//---------------------------------------------------------------------------
CDistributionSpec *
CPhysicalParallelRightOuterHashJoin::PdsDerive(
	CMemoryPool *mp,
	CExpressionHandle &exprhdl
) const
{
	// Get distributions from children
	CDistributionSpec *pdsOuter = exprhdl.Pdpplan(0)->Pds();
	CDistributionSpec *pdsInner = exprhdl.Pdpplan(1)->Pds();

	// ========== Priority 1: Handle Parallel-specific Distributions ==========

	// Case 1: Inner child is ReplicatedWorkers (BroadcastWorkers scenario)
	// The inner table is small and replicated to all workers.
	// The outer (probe) side drives the output rows during the probe phase,
	// and the scan phase adds unmatched inner rows (which are replicated).
	// Output distribution follows the outer child's distribution.
	if (CDistributionSpec::EdtReplicatedWorkers == pdsInner->Edt())
	{
		pdsOuter->AddRef();
		return pdsOuter;
	}

	// Case 2: Both children are WorkerRandom (HashDistributeWorkers scenario)
	// For Right Outer Join, the non-nullable side is the inner (build) child.
	// Output distribution follows the inner child.
	if (CDistributionSpec::EdtWorkerRandom == pdsOuter->Edt() &&
		CDistributionSpec::EdtWorkerRandom == pdsInner->Edt())
	{
		pdsInner->AddRef();
		return pdsInner;
	}

	// Case 3: Only outer is WorkerRandom
	// The inner child's distribution is preserved for right outer join.
	if (CDistributionSpec::EdtWorkerRandom == pdsOuter->Edt())
	{
		pdsInner->AddRef();
		return pdsInner;
	}

	// ========== Priority 2: Traditional Distributions ==========
	// PdsDeriveForOuterJoin already handles the right-join swap internally
	return PdsDeriveForOuterJoin(mp, exprhdl);
}

CPartitionPropagationSpec *
CPhysicalParallelRightOuterHashJoin::PppsRequired(
	CMemoryPool *mp, CExpressionHandle &exprhdl,
	CPartitionPropagationSpec *pppsRequired, ULONG child_index,
	CDrvdPropArray *pdrgpdpCtxt, ULONG ulOptReq) const
{
	return PppsRequiredForJoins(mp, exprhdl, pppsRequired, child_index,
								pdrgpdpCtxt, ulOptReq);
}

CPartitionPropagationSpec *
CPhysicalParallelRightOuterHashJoin::PppsDerive(CMemoryPool *mp,
												CExpressionHandle &exprhdl) const
{
	return PppsDeriveForJoins(mp, exprhdl);
}

// EOF
