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

#include "gpopt/operators/CPhysicalParallelUnionAll.h"

#include "gpopt/base/CCostContext.h"
#include "gpopt/base/CDistributionSpecNonSingleton.h"
#include "gpopt/base/CDistributionSpecRandom.h"
#include "gpopt/base/COptimizationContext.h"
#include "gpopt/base/CUtils.h"
#include "gpopt/operators/CExpressionHandle.h"
#include "gpopt/operators/CPhysicalMotion.h"

namespace gpopt
{

CPhysicalParallelUnionAll::CPhysicalParallelUnionAll(
	CMemoryPool *mp, CColRefArray *pdrgpcrOutput,
	CColRef2dArray *pdrgpdrgpcrInput, ULONG ulParallelWorkers)
	: CPhysicalUnionAll(mp, pdrgpcrOutput, pdrgpdrgpcrInput),
	  m_ulParallelWorkers(ulParallelWorkers)
{
	GPOS_ASSERT(ulParallelWorkers > 0);
	SetDistrRequests(1 /*ulDistrReq*/);
	GPOS_ASSERT(0 < UlDistrRequests());
}

COperator::EOperatorId
CPhysicalParallelUnionAll::Eopid() const
{
	return EopPhysicalParallelUnionAll;
}

const CHAR *
CPhysicalParallelUnionAll::SzId() const
{
	return "CPhysicalParallelUnionAll";
}

CDistributionSpec *
CPhysicalParallelUnionAll::PdsRequired(CMemoryPool *mp, CExpressionHandle &,
									   CDistributionSpec *,
									   ULONG child_index GPOS_UNUSED,
									   CDrvdPropArray *,
									   ULONG ulOptReq GPOS_UNUSED) const
{
	GPOS_ASSERT(nullptr != PdrgpdrgpcrInput());
	GPOS_ASSERT(child_index < PdrgpdrgpcrInput()->Size());
	GPOS_ASSERT(1 > ulOptReq);

	return GPOS_NEW(mp) CDistributionSpecNonSingleton(false /*fAllowReplicated*/);
}

CEnfdDistribution::EDistributionMatching
CPhysicalParallelUnionAll::Edm(CReqdPropPlan *,	   // prppInput
							   ULONG,			   // child_index
							   CDrvdPropArray *,   // pdrgpdpCtxt
							   ULONG			   // ulOptReq
)
{
	return CEnfdDistribution::EdmSatisfy;
}

CDistributionSpec *
CPhysicalParallelUnionAll::PdsDerive(CMemoryPool *mp,
									 CExpressionHandle &exprhdl) const
{
	return CPhysicalUnionAll::PdsDerive(mp, exprhdl);
}

//---------------------------------------------------------------------------
//	@function:
//		FHasIncompatibleOps
//
//	@doc:
//		Recursively check if a physical plan contains operators incompatible
//		with Parallel Union All:
//		- Motion operators (except duplicate-sensitive random motions)
//		- Parallel hash joins (require barrier sync, can cause deadlock)
//
//---------------------------------------------------------------------------
static BOOL
FHasIncompatibleOps(CCostContext *pcc)
{
	GPOS_CHECK_STACK_SIZE;

	if (nullptr == pcc)
	{
		return false;
	}

	CGroupExpression *pgexpr = pcc->Pgexpr();
	if (nullptr == pgexpr)
	{
		return false;
	}

	COperator *pop = pgexpr->Pop();

	// Check for motion operators
	if (CUtils::FPhysicalMotion(pop))
	{
		// Allow duplicate-sensitive random motions (used for UNION ALL with constants)
		CDistributionSpec::EDistributionType edt =
			CPhysicalMotion::PopConvert(pop)->Pds()->Edt();
		if (CDistributionSpec::EdtRandom == edt ||
			CDistributionSpec::EdtStrictRandom == edt)
		{
			const CDistributionSpecRandom *pdsRandom =
				dynamic_cast<const CDistributionSpecRandom *>(
					CPhysicalMotion::PopConvert(pop)->Pds());
			if (nullptr == pdsRandom || !pdsRandom->IsDuplicateSensitive())
			{
				return true;
			}
			// Duplicate-sensitive random motion is safe, continue checking children
		}
		else
		{
			return true;
		}
	}

	// Check for parallel hash join (barrier sync can deadlock under Parallel Append)
	if (CUtils::FParallelHashJoin(pop))
	{
		return true;
	}

	// Recursively check children
	if (pop->FPhysical())
	{
		COptimizationContextArray *pdrgpoc = pcc->Pdrgpoc();
		if (nullptr != pdrgpoc)
		{
			const ULONG arity = pdrgpoc->Size();
			for (ULONG ul = 0; ul < arity; ul++)
			{
				COptimizationContext *pocChild = (*pdrgpoc)[ul];
				if (nullptr != pocChild &&
					FHasIncompatibleOps(pocChild->PccBest()))
				{
					return true;
				}
			}
		}
	}

	return false;
}

//---------------------------------------------------------------------------
//	@function:
//		CPhysicalParallelUnionAll::FValidContext
//
//	@doc:
//		Check if the optimization context is valid for Parallel Union All.
//		Rejects plans containing incompatible operators in child subtrees.
//
//---------------------------------------------------------------------------
BOOL
CPhysicalParallelUnionAll::FValidContext(CMemoryPool *,
										 COptimizationContext *,
										 COptimizationContextArray *pdrgpocChild) const
{
	const ULONG arity = pdrgpocChild->Size();
	for (ULONG ul = 0; ul < arity; ul++)
	{
		COptimizationContext *pocChild = (*pdrgpocChild)[ul];
		CCostContext *pccChildBest = pocChild->PccBest();

		if (nullptr != pccChildBest && FHasIncompatibleOps(pccChildBest))
		{
			return false;
		}
	}

	return true;
}

CPhysicalParallelUnionAll *
CPhysicalParallelUnionAll::PopConvert(COperator *pop)
{
	GPOS_ASSERT(nullptr != pop);
	GPOS_ASSERT(EopPhysicalParallelUnionAll == pop->Eopid());

	return dynamic_cast<CPhysicalParallelUnionAll *>(pop);
}

}  // namespace gpopt
