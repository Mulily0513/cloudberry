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
//		CPhysicalParallelFullHashJoin.h
//
//	@doc:
//		Parallel full outer hash join operator
//---------------------------------------------------------------------------
#ifndef GPOPT_CPhysicalParallelFullHashJoin_H
#define GPOPT_CPhysicalParallelFullHashJoin_H

#include "gpos/base.h"

#include "gpopt/operators/CPhysicalParallelHashJoin.h"

namespace gpopt
{
//---------------------------------------------------------------------------
//	@class:
//		CPhysicalParallelFullHashJoin
//
//	@doc:
//		Parallel full outer hash join operator
//
//		Full outer join outputs matched, unmatched-outer, and unmatched-inner
//		rows. Both sides are nullable. BroadcastWorkers is disabled because
//		broadcasting either side would cause duplicate unmatched rows.
//
//---------------------------------------------------------------------------
class CPhysicalParallelFullHashJoin : public CPhysicalParallelHashJoin
{
public:
	CPhysicalParallelFullHashJoin(const CPhysicalParallelFullHashJoin &) = delete;

	// ctor
	CPhysicalParallelFullHashJoin(
		CMemoryPool *mp,
		CExpressionArray *pdrgpexprOuterKeys,
		CExpressionArray *pdrgpexprInnerKeys,
		IMdIdArray *hash_opfamilies,
		BOOL is_null_aware = true,
		CXform::EXformId origin_xform = CXform::ExfSentinel
	);

	// dtor
	~CPhysicalParallelFullHashJoin() override;

	// ident accessors
	EOperatorId Eopid() const override
	{
		return EopPhysicalParallelFullHashJoin;
	}

	// return a string for operator name
	const CHAR *SzId() const override
	{
		return "CPhysicalParallelFullHashJoin";
	}

	// derive distribution: parallel FOJ-specific logic (both sides nullable)
	CDistributionSpec *PdsDerive(CMemoryPool *mp,
								 CExpressionHandle &exprhdl) const override;

	// partition propagation
	CPartitionPropagationSpec *PppsRequired(
		CMemoryPool *mp, CExpressionHandle &exprhdl,
		CPartitionPropagationSpec *pppsRequired, ULONG child_index,
		CDrvdPropArray *pdrgpdpCtxt, ULONG ulOptReq) const override;

	CPartitionPropagationSpec *PppsDerive(
		CMemoryPool *mp, CExpressionHandle &exprhdl) const override;

	// conversion function
	static CPhysicalParallelFullHashJoin *PopConvert(COperator *pop)
	{
		GPOS_ASSERT(nullptr != pop);
		GPOS_ASSERT(EopPhysicalParallelFullHashJoin == pop->Eopid());

		return dynamic_cast<CPhysicalParallelFullHashJoin *>(pop);
	}

};	// class CPhysicalParallelFullHashJoin

}  // namespace gpopt

#endif	// !GPOPT_CPhysicalParallelFullHashJoin_H

// EOF
