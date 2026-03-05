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
 * CPhysicalParallelBitmapTableScan.h
 *
 * IDENTIFICATION
 *	  src/backend/gporca/libgpopt/include/gpopt/operators/CPhysicalParallelBitmapTableScan.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef GPOPT_CPhysicalParallelBitmapTableScan_H
#define GPOPT_CPhysicalParallelBitmapTableScan_H

#include "gpos/base.h"

#include "gpopt/operators/CPhysicalBitmapTableScan.h"

namespace gpopt
{
//---------------------------------------------------------------------------
//	@class:
//		CPhysicalParallelBitmapTableScan
//
//	@doc:
//		Parallel bitmap table scan operator
//
//---------------------------------------------------------------------------
class CPhysicalParallelBitmapTableScan : public CPhysicalBitmapTableScan
{
private:
	// number of parallel workers
	ULONG m_ulParallelWorkers;

	// worker-level distribution spec
	CDistributionSpec *m_pdsWorkerDistribution;

	// private copy ctor
	CPhysicalParallelBitmapTableScan(const CPhysicalParallelBitmapTableScan &) = delete;

public:
	// ctor
	CPhysicalParallelBitmapTableScan(CMemoryPool *mp, CTableDescriptor *ptabdesc,
									  ULONG ulOriginOpId, const CName *pnameTableAlias,
									  CColRefArray *pdrgpcrOutput,
									  ULONG ulParallelWorkers);

	// dtor
	~CPhysicalParallelBitmapTableScan() override;

	// ident accessors
	EOperatorId
	Eopid() const override
	{
		return EopPhysicalParallelBitmapTableScan;
	}

	// return a string for operator name
	const CHAR *
	SzId() const override
	{
		return "CPhysicalParallelBitmapTableScan";
	}

	// number of parallel workers
	ULONG UlParallelWorkers() const
	{
		return m_ulParallelWorkers;
	}

	// operator specific hash function
	ULONG HashValue() const override;

	// match function
	BOOL Matches(COperator *) const override;

	// debug print
	IOstream &OsPrint(IOstream &) const override;

	// conversion function
	static CPhysicalParallelBitmapTableScan *
	PopConvert(COperator *pop)
	{
		GPOS_ASSERT(nullptr != pop);
		return dynamic_cast<CPhysicalParallelBitmapTableScan *>(pop);
	}

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
	CDistributionSpec *PdsDerive(CMemoryPool *mp, CExpressionHandle &exprhdl) const override;

	// return distribution property enforcing type for this operator
	CEnfdProp::EPropEnforcingType EpetDistribution(
		CExpressionHandle &exprhdl,
		const CEnfdDistribution *ped) const override;

	// return rewindability property enforcing type for this operator
	CEnfdProp::EPropEnforcingType EpetRewindability(
		CExpressionHandle &exprhdl,
		const CEnfdRewindability *per) const override;

	// check if optimization contexts is valid
	BOOL FValidContext(CMemoryPool *mp, COptimizationContext *poc,
					   COptimizationContextArray *pdrgpocChild) const override;

};	// class CPhysicalParallelBitmapTableScan

}  // namespace gpopt

#endif	// !GPOPT_CPhysicalParallelBitmapTableScan_H

// EOF
