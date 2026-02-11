/*-------------------------------------------------------------------------
 *
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
 *
 * CPhysicalParallelSequenceProject.h
 *
 * IDENTIFICATION
 *	  src/backend/gporca/libgpopt/include/gpopt/operators/CPhysicalParallelSequenceProject.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef GPOPT_CPhysicalParallelSequenceProject_H
#define GPOPT_CPhysicalParallelSequenceProject_H

#include "gpos/base.h"

#include "gpopt/operators/CPhysicalSequenceProject.h"

namespace gpopt
{
//---------------------------------------------------------------------------
//	@class:
//		CPhysicalParallelSequenceProject
//
//	@doc:
//		Physical Parallel Sequence Project operator for intra-segment
//		parallel window function execution
//
//---------------------------------------------------------------------------
class CPhysicalParallelSequenceProject : public CPhysicalSequenceProject
{
private:
	// number of parallel workers
	ULONG m_ulParallelWorkers;

public:
	CPhysicalParallelSequenceProject(
		const CPhysicalParallelSequenceProject &) = delete;

	// ctor
	CPhysicalParallelSequenceProject(CMemoryPool *mp, ESPType sptype,
									 CDistributionSpec *pds,
									 COrderSpecArray *pdrgpos,
									 CWindowFrameArray *pdrgpwf,
									 ULONG ulParallelWorkers);

	// dtor
	~CPhysicalParallelSequenceProject() override = default;

	// ident accessors
	EOperatorId
	Eopid() const override
	{
		return EopPhysicalParallelSequenceProject;
	}

	// operator name
	const CHAR *
	SzId() const override
	{
		return "CPhysicalParallelSequenceProject";
	}

	// parallel worker count
	ULONG
	UlParallelWorkers() const
	{
		return m_ulParallelWorkers;
	}

	// match function
	BOOL Matches(COperator *pop) const override;

	// hashing function
	ULONG HashValue() const override;

	// compute required distribution of the n-th child
	CDistributionSpec *PdsRequired(CMemoryPool *mp, CExpressionHandle &exprhdl,
								   CDistributionSpec *pdsRequired,
								   ULONG child_index,
								   CDrvdPropArray *pdrgpdpCtxt,
								   ULONG ulOptReq) const override;

	// check if optimization context is valid
	BOOL FValidContext(CMemoryPool *mp, COptimizationContext *poc,
					   COptimizationContextArray *pdrgpocChild) const override;

	// conversion function
	static CPhysicalParallelSequenceProject *
	PopConvert(COperator *pop)
	{
		GPOS_ASSERT(nullptr != pop);
		GPOS_ASSERT(EopPhysicalParallelSequenceProject == pop->Eopid());

		return dynamic_cast<CPhysicalParallelSequenceProject *>(pop);
	}

};	// class CPhysicalParallelSequenceProject

}  // namespace gpopt

#endif	// !GPOPT_CPhysicalParallelSequenceProject_H

// EOF
