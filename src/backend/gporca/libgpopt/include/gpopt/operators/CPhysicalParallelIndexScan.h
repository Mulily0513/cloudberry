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
 * CPhysicalParallelIndexScan.h
 *
 * IDENTIFICATION
 *	  src/backend/gporca/libgpopt/include/gpopt/operators/CPhysicalParallelIndexScan.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef GPOPT_CPhysicalParallelIndexScan_H
#define GPOPT_CPhysicalParallelIndexScan_H

#include "gpos/base.h"

#include "gpopt/operators/CPhysicalIndexScan.h"

namespace gpopt
{
//---------------------------------------------------------------------------
//	@class:
//		CPhysicalParallelIndexScan
//
//	@doc:
//		Base class for physical parallel index scan operators
//
//---------------------------------------------------------------------------
class CPhysicalParallelIndexScan : public CPhysicalIndexScan
{
private:
	// number of parallel workers
	ULONG m_ulParallelWorkers;

	// worker-level distribution spec
	CDistributionSpec *m_pdsWorkerDistribution;

public:
	CPhysicalParallelIndexScan(const CPhysicalParallelIndexScan &) = delete;

	// ctors
	CPhysicalParallelIndexScan(CMemoryPool *mp, CIndexDescriptor *pindexdesc,
							   CTableDescriptor *ptabdesc, ULONG ulOriginOpId,
							   const CName *pnameAlias, CColRefArray *colref_array,
							   COrderSpec *pos, ULONG ulUnindexedPredColCount,
							   EIndexScanDirection scan_direction, ULONG ulParallelWorkers);

	// dtor
	~CPhysicalParallelIndexScan() override;

	// ident accessors
	EOperatorId
	Eopid() const override
	{
		return EopPhysicalParallelIndexScan;
	}

	// operator name
	const CHAR *
	SzId() const override
	{
		return "CPhysicalParallelIndexScan";
	}

	// number of parallel workers
	ULONG UlParallelWorkers() const
	{
		return m_ulParallelWorkers;
	}

	// operator specific hash function
	ULONG HashValue() const override;

	// match function
	BOOL Matches(COperator *pop) const override;

	CRewindabilitySpec *
	PrsDerive(CMemoryPool *mp,
			  CExpressionHandle &  // exprhdl
	) const override
	{
		return GPOS_NEW(mp)
			CRewindabilitySpec(CRewindabilitySpec::ErtNone,
							   CRewindabilitySpec::EmhtNoMotion);
	}

	// conversion function
	static CPhysicalParallelIndexScan *
	PopConvert(COperator *pop)
	{
		GPOS_ASSERT(nullptr != pop);
		GPOS_ASSERT(EopPhysicalParallelIndexScan == pop->Eopid());

		return dynamic_cast<CPhysicalParallelIndexScan *>(pop);
	}

	// debug print
	IOstream &OsPrint(IOstream &) const override;

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
	// Reject if parent requires REWINDABLE (e.g., for NL Join inner child)
	BOOL FValidContext(CMemoryPool *mp, COptimizationContext *poc,
					   COptimizationContextArray *pdrgpocChild) const override;

};	// class CPhysicalParallelIndexScan

}  // namespace gpopt

#endif	// !GPOPT_CPhysicalParallelIndexScan_H

// EOF
