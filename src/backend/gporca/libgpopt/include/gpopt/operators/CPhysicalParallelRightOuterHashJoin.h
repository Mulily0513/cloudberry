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
//		CPhysicalParallelRightOuterHashJoin.h
//
//	@doc:
//		Parallel right outer hash join operator
//---------------------------------------------------------------------------
#ifndef GPOPT_CPhysicalParallelRightOuterHashJoin_H
#define GPOPT_CPhysicalParallelRightOuterHashJoin_H

#include "gpos/base.h"

#include "gpopt/operators/CPhysicalParallelHashJoin.h"

namespace gpopt
{
//---------------------------------------------------------------------------
//	@class:
//		CPhysicalParallelRightOuterHashJoin
//
//	@doc:
//		Parallel right outer hash join operator
//
//---------------------------------------------------------------------------
class CPhysicalParallelRightOuterHashJoin : public CPhysicalParallelHashJoin
{
public:
	CPhysicalParallelRightOuterHashJoin(const CPhysicalParallelRightOuterHashJoin &) = delete;

	// ctor
	CPhysicalParallelRightOuterHashJoin(
		CMemoryPool *mp,
		CExpressionArray *pdrgpexprOuterKeys,
		CExpressionArray *pdrgpexprInnerKeys,
		IMdIdArray *hash_opfamilies,
		BOOL is_null_aware = true,
		CXform::EXformId origin_xform = CXform::ExfSentinel
	);

	// dtor
	~CPhysicalParallelRightOuterHashJoin() override;

	// ident accessors
	EOperatorId Eopid() const override
	{
		return EopPhysicalParallelRightOuterHashJoin;
	}

	// return a string for operator name
	const CHAR *SzId() const override
	{
		return "CPhysicalParallelRightOuterHashJoin";
	}

	// derive distribution - for right outer join, must preserve the right
	// (inner/build) side since that is the non-nullable side
	CDistributionSpec *PdsDerive(CMemoryPool *mp,
								 CExpressionHandle &exprhdl) const override;

	CPartitionPropagationSpec *PppsRequired(
		CMemoryPool *mp, CExpressionHandle &exprhdl,
		CPartitionPropagationSpec *pppsRequired, ULONG child_index,
		CDrvdPropArray *pdrgpdpCtxt, ULONG ulOptReq) const override;

	CPartitionPropagationSpec *PppsDerive(
		CMemoryPool *mp, CExpressionHandle &exprhdl) const override;

	// conversion function
	static CPhysicalParallelRightOuterHashJoin *PopConvert(COperator *pop)
	{
		GPOS_ASSERT(nullptr != pop);
		GPOS_ASSERT(EopPhysicalParallelRightOuterHashJoin == pop->Eopid());

		return dynamic_cast<CPhysicalParallelRightOuterHashJoin *>(pop);
	}

};	// class CPhysicalParallelRightOuterHashJoin

}  // namespace gpopt

#endif	// !GPOPT_CPhysicalParallelRightOuterHashJoin_H

// EOF
