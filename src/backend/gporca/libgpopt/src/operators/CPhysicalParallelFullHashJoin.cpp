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
//		CPhysicalParallelFullHashJoin.cpp
//
//	@doc:
//		Implementation of parallel full outer hash join operator
//---------------------------------------------------------------------------

#include "gpopt/operators/CPhysicalParallelFullHashJoin.h"

#include "gpos/base.h"

#include "gpopt/base/CDistributionSpecHashed.h"
#include "gpopt/base/CDistributionSpecWorkerRandom.h"
#include "gpopt/base/CUtils.h"
#include "gpopt/operators/CExpressionHandle.h"

using namespace gpopt;


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalParallelFullHashJoin::CPhysicalParallelFullHashJoin
//
//	@doc:
//		Ctor
//
//---------------------------------------------------------------------------
CPhysicalParallelFullHashJoin::CPhysicalParallelFullHashJoin(
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
	// Base class sets 1 + NumDistrReq() (includes BroadcastWorkers slot).
	// FOJ disables BroadcastWorkers: broadcasting either side would cause
	// duplicate unmatched rows in the PHJ_BATCH_SCAN phase.
	SetDistrRequests(NumDistrReq());
	SetPartPropagateRequests(2);
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalParallelFullHashJoin::~CPhysicalParallelFullHashJoin
//
//	@doc:
//		Dtor
//
//---------------------------------------------------------------------------
CPhysicalParallelFullHashJoin::~CPhysicalParallelFullHashJoin() = default;


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalParallelFullHashJoin::PdsDerive
//
//	@doc:
//		Derive distribution
//
//		For Full Outer Join, both sides are nullable. The output includes
//		matched rows, unmatched-outer rows, and unmatched-inner rows.
//		All three categories maintain Hashed(join_keys) at the segment level,
//		with WorkerRandom distribution within each segment.
//
//		Unlike Right Outer Join which simply returns the inner distribution,
//		Full Outer Join must Combine the base Hashed specs from both sides
//		to propagate the equivalence of outer_keys and inner_keys to upper
//		operators, avoiding unnecessary Motion nodes.
//
//---------------------------------------------------------------------------
CDistributionSpec *
CPhysicalParallelFullHashJoin::PdsDerive(
	CMemoryPool *mp,
	CExpressionHandle &exprhdl
) const
{
	CDistributionSpec *pdsOuter = exprhdl.Pdpplan(0)->Pds();
	CDistributionSpec *pdsInner = exprhdl.Pdpplan(1)->Pds();

	// ===== Case 1: Inner = ReplicatedWorkers =====
	// BroadcastWorkers is disabled via SetDistrRequests; defensive handling.
	if (CDistributionSpec::EdtReplicatedWorkers == pdsInner->Edt())
	{
		pdsOuter->AddRef();
		return pdsOuter;
	}

	// ===== Case 2: Both = WorkerRandom (normal parallel FOJ path) =====
	// Extract segment-level base specs and Combine to convey
	// outer_keys <-> inner_keys equivalence.
	if (CDistributionSpec::EdtWorkerRandom == pdsOuter->Edt() &&
		CDistributionSpec::EdtWorkerRandom == pdsInner->Edt())
	{
		CDistributionSpecWorkerRandom *pdsWROuter =
			CDistributionSpecWorkerRandom::PdsConvert(pdsOuter);
		CDistributionSpecWorkerRandom *pdsWRInner =
			CDistributionSpecWorkerRandom::PdsConvert(pdsInner);

		CDistributionSpec *pdsBaseOuter = pdsWROuter->PdsSegmentBase();
		CDistributionSpec *pdsBaseInner = pdsWRInner->PdsSegmentBase();

		// Both bases are Hashed: mirror CPhysicalFullHashJoin::PdsDerive Combine logic
		if (nullptr != pdsBaseOuter && nullptr != pdsBaseInner &&
			CDistributionSpec::EdtHashed == pdsBaseOuter->Edt() &&
			CDistributionSpec::EdtHashed == pdsBaseInner->Edt())
		{
			CDistributionSpecHashed *pdsHashedOuter =
				CDistributionSpecHashed::PdsConvert(pdsBaseOuter);
			CDistributionSpecHashed *pdsHashedInner =
				CDistributionSpecHashed::PdsConvert(pdsBaseInner);
			ULONG ulWorkers = pdsWROuter->UlWorkers();

			// Self-join with matching join keys: preserve colocated nulls property
			if (FSelfJoinWithMatchingJoinKeys(mp, exprhdl))
			{
				CDistributionSpecHashed *combined =
					pdsHashedInner->Combine(mp, pdsHashedOuter);
				if (nullptr != combined)
				{
					return CDistributionSpecWorkerRandom::PdsCreateWorkerRandom(
						mp, ulWorkers, combined);
				}
			}

			// Both sides cover join keys: Combine to convey equivalence.
			// FOJ: both sides are nullable -> fNullsColocated = false
			CDistributionSpecHashed *pdsOuterCopy =
				pdsHashedOuter->Copy(mp, false /*fNullsColocated*/);

			if (pdsHashedOuter->IsCoveredBy(PdrgpexprOuterKeys()) &&
				pdsHashedInner->IsCoveredBy(PdrgpexprInnerKeys()))
			{
				CDistributionSpecHashed *pdsInnerCopy =
					pdsHashedInner->Copy(mp, false);
				CDistributionSpecHashed *combined =
					pdsOuterCopy->Combine(mp, pdsInnerCopy);
				pdsInnerCopy->Release();

				if (nullptr != combined)
				{
					pdsOuterCopy->Release();
					return CDistributionSpecWorkerRandom::PdsCreateWorkerRandom(
						mp, ulWorkers, combined);
				}
			}

			// Combine failed: wrap outer base copy
			return CDistributionSpecWorkerRandom::PdsCreateWorkerRandom(
				mp, ulWorkers, pdsOuterCopy);
		}

		// Base not both Hashed: return outer as-is
		pdsOuter->AddRef();
		return pdsOuter;
	}

	// ===== Case 3: Only outer = WorkerRandom =====
	if (CDistributionSpec::EdtWorkerRandom == pdsOuter->Edt())
	{
		pdsOuter->AddRef();
		return pdsOuter;
	}

	// ===== Case 4: Traditional distributions (Replicated, Universal, etc.) =====
	// CDistributionSpecUniversal::FSatisfies() returns true for WorkerRandom,
	// so a Universal child can pass through enforcement without a Motion.
	// Mirror CPhysicalFullHashJoin::PdsDerive: if outer is Replicated/Universal,
	// return inner; otherwise return outer.
	if (CDistributionSpec::EdtStrictReplicated == pdsOuter->Edt() ||
		CDistributionSpec::EdtUniversal == pdsOuter->Edt())
	{
		pdsInner->AddRef();
		return pdsInner;
	}

	pdsOuter->AddRef();
	return pdsOuter;
}

CPartitionPropagationSpec *
CPhysicalParallelFullHashJoin::PppsRequired(
	CMemoryPool *mp, CExpressionHandle &exprhdl,
	CPartitionPropagationSpec *pppsRequired, ULONG child_index,
	CDrvdPropArray *pdrgpdpCtxt, ULONG ulOptReq) const
{
	return PppsRequiredForJoins(mp, exprhdl, pppsRequired, child_index,
								pdrgpdpCtxt, ulOptReq);
}

CPartitionPropagationSpec *
CPhysicalParallelFullHashJoin::PppsDerive(CMemoryPool *mp,
										  CExpressionHandle &exprhdl) const
{
	return PppsDeriveForJoins(mp, exprhdl);
}

// EOF
