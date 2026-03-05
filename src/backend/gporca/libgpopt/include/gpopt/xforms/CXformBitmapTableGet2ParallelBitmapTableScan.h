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
 * CXformBitmapTableGet2ParallelBitmapTableScan.h
 *
 * IDENTIFICATION
 *	  src/backend/gporca/libgpopt/include/gpopt/xforms/CXformBitmapTableGet2ParallelBitmapTableScan.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef GPOPT_CXformBitmapTableGet2ParallelBitmapTableScan_H
#define GPOPT_CXformBitmapTableGet2ParallelBitmapTableScan_H

#include "gpos/base.h"

#include "gpopt/xforms/CXformImplementation.h"

namespace gpopt
{
//---------------------------------------------------------------------------
//	@class:
//		CXformBitmapTableGet2ParallelBitmapTableScan
//
//	@doc:
//		Implement CLogicalBitmapTableGet as a CPhysicalParallelBitmapTableScan
//
//---------------------------------------------------------------------------
class CXformBitmapTableGet2ParallelBitmapTableScan : public CXformImplementation
{
private:
public:
	CXformBitmapTableGet2ParallelBitmapTableScan(
		const CXformBitmapTableGet2ParallelBitmapTableScan &) = delete;

	// ctor
	explicit CXformBitmapTableGet2ParallelBitmapTableScan(CMemoryPool *mp);

	// dtor
	~CXformBitmapTableGet2ParallelBitmapTableScan() override = default;

	// identifier
	EXformId
	Exfid() const override
	{
		return ExfBitmapTableGet2ParallelBitmapTableScan;
	}

	// xform name
	const CHAR *
	SzId() const override
	{
		return "CXformBitmapTableGet2ParallelBitmapTableScan";
	}

	// compute xform promise for a given expression handle
	EXformPromise Exfp(CExpressionHandle &exprhdl) const override;

	// actual transform
	void Transform(CXformContext *pxfctxt, CXformResult *pxfres,
				   CExpression *pexpr) const override;

};	// class CXformBitmapTableGet2ParallelBitmapTableScan
}  // namespace gpopt

#endif	// !GPOPT_CXformBitmapTableGet2ParallelBitmapTableScan_H

// EOF
