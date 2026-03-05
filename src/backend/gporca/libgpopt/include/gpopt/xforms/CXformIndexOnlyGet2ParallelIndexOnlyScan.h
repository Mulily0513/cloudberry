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
 * CXformIndexOnlyGet2ParallelIndexOnlyScan.h
 *
 * IDENTIFICATION
 *	  src/backend/gporca/libgpopt/include/gpopt/xforms/CXformIndexOnlyGet2ParallelIndexOnlyScan.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef GPOPT_CXformIndexOnlyGet2ParallelIndexOnlyScan_H
#define GPOPT_CXformIndexOnlyGet2ParallelIndexOnlyScan_H

#include "gpos/base.h"

#include "gpopt/xforms/CXformImplementation.h"

namespace gpopt
{
using namespace gpos;

//---------------------------------------------------------------------------
//	@class:
//		CXformIndexOnlyGet2ParallelIndexOnlyScan
//
//	@doc:
//		Transform Index Only Get to Parallel Index Only Scan
//
//---------------------------------------------------------------------------
class CXformIndexOnlyGet2ParallelIndexOnlyScan : public CXformImplementation
{
public:
	CXformIndexOnlyGet2ParallelIndexOnlyScan(
		const CXformIndexOnlyGet2ParallelIndexOnlyScan &) = delete;

	// ctor
	explicit CXformIndexOnlyGet2ParallelIndexOnlyScan(CMemoryPool *);

	// dtor
	~CXformIndexOnlyGet2ParallelIndexOnlyScan() override = default;

	// ident accessors
	EXformId
	Exfid() const override
	{
		return ExfIndexOnlyGet2ParallelIndexOnlyScan;
	}

	// xform name
	const CHAR *
	SzId() const override
	{
		return "CXformIndexOnlyGet2ParallelIndexOnlyScan";
	}

	// compute xform promise for a given expression handle
	EXformPromise Exfp(CExpressionHandle &exprhdl) const override;

	// actual transform
	void Transform(CXformContext *pxfctxt, CXformResult *pxfres,
				   CExpression *pexpr) const override;

};	// class CXformIndexOnlyGet2ParallelIndexOnlyScan

}  // namespace gpopt

#endif	// !GPOPT_CXformIndexOnlyGet2ParallelIndexOnlyScan_H

// EOF
