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
 * CPhysicalParallelIndexOnlyScan.h
 *
 * IDENTIFICATION
 *	  src/backend/gporca/libgpopt/include/gpopt/operators/CPhysicalParallelIndexOnlyScan.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef GPOPT_CPhysicalParallelIndexOnlyScan_H
#define GPOPT_CPhysicalParallelIndexOnlyScan_H

#include "gpos/base.h"

#include "gpopt/operators/CPhysicalIndexOnlyScan.h"

namespace gpopt
{
//---------------------------------------------------------------------------
//	@class:
//		CPhysicalParallelIndexOnlyScan
//
//	@doc:
//		Parallel index only scan operator
//
//---------------------------------------------------------------------------
class CPhysicalParallelIndexOnlyScan : public CPhysicalIndexOnlyScan
{
private:
	// number of parallel workers
	ULONG m_ulParallelWorkers;

	// worker-level distribution spec
	CDistributionSpec *m_pdsWorkerDistribution;

	// private copy ctor
	CPhysicalParallelIndexOnlyScan(const CPhysicalParallelIndexOnlyScan &);

public:
	// ctor
	CPhysicalParallelIndexOnlyScan(CMemoryPool *mp,
								   CIndexDescriptor *pindexdesc,
								   CTableDescriptor *ptabdesc,
								   ULONG ulOriginOpId,
								   const CName *pnameAlias,
								   CColRefArray *colref_array,
								   COrderSpec *pos,
								   EIndexScanDirection scan_direction,
								   ULONG ulParallelWorkers);

	// dtor
	~CPhysicalParallelIndexOnlyScan() override;

	// ident accessors
	EOperatorId
	Eopid() const override
	{
		return EopPhysicalParallelIndexOnlyScan;
	}

	// return a string for operator name
	const CHAR *
	SzId() const override
	{
		return "CPhysicalParallelIndexOnlyScan";
	}

	// number of parallel workers
	ULONG
	UlParallelWorkers() const
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
	static CPhysicalParallelIndexOnlyScan *
	PopConvert(COperator *pop)
	{
		GPOS_ASSERT(nullptr != pop);
		GPOS_ASSERT(EopPhysicalParallelIndexOnlyScan == pop->Eopid());

		return dynamic_cast<CPhysicalParallelIndexOnlyScan *>(pop);
	}

	// derive rewindability - parallel scan is not rewindable
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
	CDistributionSpec *PdsDerive(CMemoryPool *mp,
								 CExpressionHandle &exprhdl) const override;

	// return distribution property enforcing type for this operator
	CEnfdProp::EPropEnforcingType EpetDistribution(
		CExpressionHandle &exprhdl,
		const CEnfdDistribution *ped) const override;

	// return rewindability property enforcing type for this operator
	CEnfdProp::EPropEnforcingType EpetRewindability(
		CExpressionHandle &exprhdl,
		const CEnfdRewindability *per) const override;

	// check if optimization context is valid
	BOOL FValidContext(CMemoryPool *mp, COptimizationContext *poc,
					   COptimizationContextArray *pdrgpocChild) const override;

};	// class CPhysicalParallelIndexOnlyScan

}  // namespace gpopt

#endif	// !GPOPT_CPhysicalParallelIndexOnlyScan_H

// EOF
