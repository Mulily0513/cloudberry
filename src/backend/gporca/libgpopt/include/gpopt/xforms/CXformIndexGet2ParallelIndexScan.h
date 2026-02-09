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
 * CXformIndexGet2ParallelIndexScan.h
 *
 * IDENTIFICATION
 *	  src/backend/gporca/libgpopt/include/gpopt/xforms/CXformIndexGet2ParallelIndexScan.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef GPOPT_CXformIndexGet2ParallelIndexScan_H
#define GPOPT_CXformIndexGet2ParallelIndexScan_H

#include "gpos/base.h"

#include "gpopt/xforms/CXformImplementation.h"

namespace gpopt
{
using namespace gpos;

//---------------------------------------------------------------------------
//	@class:
//		CXformIndexGet2ParallelIndexScan
//
//	@doc:
//		Transform Index Get to Parallel Index Scan
//
//---------------------------------------------------------------------------
class CXformIndexGet2ParallelIndexScan : public CXformImplementation
{
private:
public:
	CXformIndexGet2ParallelIndexScan(const CXformIndexGet2ParallelIndexScan &) = delete;

	// ctor
	explicit CXformIndexGet2ParallelIndexScan(CMemoryPool *);

	// dtor
	~CXformIndexGet2ParallelIndexScan() override = default;

	// ident accessors
	EXformId
	Exfid() const override
	{
		return ExfIndexGet2ParallelIndexScan;
	}

	// xform name
	const CHAR *
	SzId() const override
	{
		return "CXformIndexGet2ParallelIndexScan";
	}

	// compute xform promise for a given expression handle
	EXformPromise Exfp(CExpressionHandle &	//exprhdl
	) const override;

	// actual transform
	void Transform(CXformContext *pxfctxt, CXformResult *pxfres,
				   CExpression *pexpr) const override;

};	// class CXformIndexGet2ParallelIndexScan

}  // namespace gpopt

#endif	// !GPOPT_CXformIndexGet2ParallelIndexScan_H

// EOF
