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
 * CPhysicalParallelBitmapTableScan.cpp
 *
 * IDENTIFICATION
 *	  src/backend/gporca/libgpopt/src/operators/CPhysicalParallelBitmapTableScan.cpp
 *
 *-------------------------------------------------------------------------
 */

#include "gpopt/operators/CPhysicalParallelBitmapTableScan.h"

#include "gpos/base.h"

#include "gpopt/base/CDistributionSpec.h"
#include "gpopt/base/CDistributionSpecHashed.h"
#include "gpopt/base/CDistributionSpecRandom.h"
#include "gpopt/base/CDistributionSpecWorkerRandom.h"
#include "gpopt/base/CDistributionSpecSingleton.h"
#include "gpopt/base/CUtils.h"
#include "gpopt/base/CEnfdDistribution.h"
#include "gpopt/base/CEnfdRewindability.h"
#include "gpopt/base/COptimizationContext.h"
#include "gpopt/base/CRewindabilitySpec.h"
#include "gpopt/base/CDrvdPropPlan.h"
#include "gpopt/metadata/CName.h"
#include "gpopt/metadata/CTableDescriptor.h"
#include "gpopt/operators/CExpressionHandle.h"

using namespace gpopt;

//---------------------------------------------------------------------------
//	@function:
//		CPhysicalParallelBitmapTableScan::CPhysicalParallelBitmapTableScan
//
//	@doc:
//		ctor
//
//---------------------------------------------------------------------------
CPhysicalParallelBitmapTableScan::CPhysicalParallelBitmapTableScan(
	CMemoryPool *mp, CTableDescriptor *ptabdesc, ULONG ulOriginOpId,
	const CName *pnameTableAlias, CColRefArray *pdrgpcrOutput,
	ULONG ulParallelWorkers)
	: CPhysicalBitmapTableScan(mp, ptabdesc, ulOriginOpId, pnameTableAlias, pdrgpcrOutput),
	  m_ulParallelWorkers(ulParallelWorkers),
	  m_pdsWorkerDistribution(nullptr)
{
	GPOS_ASSERT(ulParallelWorkers > 0);
	GPOS_ASSERT(nullptr != m_pds);
	m_pdsWorkerDistribution = CDistributionSpecWorkerRandom::PdsCreateWorkerRandom(mp, ulParallelWorkers, m_pds);
}

//---------------------------------------------------------------------------
//	@function:
//		CPhysicalParallelBitmapTableScan::~CPhysicalParallelBitmapTableScan
//
//	@doc:
//		dtor
//
//---------------------------------------------------------------------------
CPhysicalParallelBitmapTableScan::~CPhysicalParallelBitmapTableScan()
{
	CRefCount::SafeRelease(m_pdsWorkerDistribution);
}

//---------------------------------------------------------------------------
//	@function:
//		CPhysicalParallelBitmapTableScan::HashValue
//
//	@doc:
//		Combine pointer for table descriptor, parallel workers and Eop
//
//---------------------------------------------------------------------------
ULONG
CPhysicalParallelBitmapTableScan::HashValue() const
{
	ULONG ulHash = gpos::CombineHashes(CPhysicalBitmapTableScan::HashValue(),
									   gpos::HashValue<ULONG>(&m_ulParallelWorkers));
	return ulHash;
}

//---------------------------------------------------------------------------
//	@function:
//		CPhysicalParallelBitmapTableScan::Matches
//
//	@doc:
//		match operator
//
//---------------------------------------------------------------------------
BOOL
CPhysicalParallelBitmapTableScan::Matches(COperator *pop) const
{
	if (Eopid() != pop->Eopid())
	{
		return false;
	}

	CPhysicalParallelBitmapTableScan *popParallelBitmapScan =
		CPhysicalParallelBitmapTableScan::PopConvert(pop);

	return CUtils::FMatchBitmapScan(this, pop) &&
		   m_ulParallelWorkers == popParallelBitmapScan->UlParallelWorkers();
}

//---------------------------------------------------------------------------
//	@function:
//		CPhysicalParallelBitmapTableScan::OsPrint
//
//	@doc:
//		debug print
//
//---------------------------------------------------------------------------
IOstream &
CPhysicalParallelBitmapTableScan::OsPrint(IOstream &os) const
{
	os << SzId() << " ";
	os << ", Table Name: (";
	m_ptabdesc->Name().OsPrint(os);
	os << ")";
	os << ", Columns: [";
	CUtils::OsPrintDrgPcr(os, m_pdrgpcrOutput);
	os << "], Workers: " << m_ulParallelWorkers;

	return os;
}

//---------------------------------------------------------------------------
//	@function:
//		CPhysicalParallelBitmapTableScan::PdsDerive
//
//	@doc:
//		Derive distribution for parallel bitmap table scan
//
//---------------------------------------------------------------------------
CDistributionSpec *
CPhysicalParallelBitmapTableScan::PdsDerive(CMemoryPool *mp, CExpressionHandle &exprhdl) const
{
	if (nullptr != m_pdsWorkerDistribution)
	{
		m_pdsWorkerDistribution->AddRef();
		return m_pdsWorkerDistribution;
	}

	return CPhysicalScan::PdsDerive(mp, exprhdl);
}

//---------------------------------------------------------------------------
//	@function:
//		CPhysicalParallelBitmapTableScan::EpetDistribution
//
//	@doc:
//		Return the enforcing type for distribution property based on this
//		operator
//
//---------------------------------------------------------------------------
CEnfdProp::EPropEnforcingType
CPhysicalParallelBitmapTableScan::EpetDistribution(CExpressionHandle & /*exprhdl*/,
												   const CEnfdDistribution *ped) const
{
	GPOS_ASSERT(nullptr != ped);

	if (nullptr != m_pdsWorkerDistribution && ped->FCompatible(m_pdsWorkerDistribution))
	{
		return CEnfdProp::EpetUnnecessary;
	}

	return CEnfdProp::EpetRequired;
}

//---------------------------------------------------------------------------
//	@function:
//		CPhysicalParallelBitmapTableScan::EpetRewindability
//
//	@doc:
//		Return rewindability property enforcing type for this operator
//
//---------------------------------------------------------------------------
CEnfdProp::EPropEnforcingType
CPhysicalParallelBitmapTableScan::EpetRewindability(CExpressionHandle &exprhdl,
												   const CEnfdRewindability *per) const
{
	GPOS_ASSERT(nullptr != per);

	CRewindabilitySpec *prs = CDrvdPropPlan::Pdpplan(exprhdl.Pdp())->Prs();

	if (per->FCompatible(prs))
	{
		return CEnfdProp::EpetUnnecessary;
	}

	if (per->PrsRequired()->IsOriginNLJoin())
	{
		return CEnfdProp::EpetProhibited;
	}

	return CEnfdProp::EpetRequired;
}

//---------------------------------------------------------------------------
//	@function:
//		CPhysicalParallelBitmapTableScan::FValidContext
//
//	@doc:
//		Check if optimization contexts is valid;
//		Reject if parent requires REWINDABLE (e.g., for NL Join inner child)
//
//---------------------------------------------------------------------------
BOOL
CPhysicalParallelBitmapTableScan::FValidContext(CMemoryPool *,
												COptimizationContext *poc,
												COptimizationContextArray *) const
{
	GPOS_ASSERT(nullptr != poc);

	CReqdPropPlan *prpp = poc->Prpp();
	CRewindabilitySpec *prsRequired = prpp->Per()->PrsRequired();

	if (prsRequired->IsOriginNLJoin())
	{
		return false;
	}

	return true;
}

// EOF
