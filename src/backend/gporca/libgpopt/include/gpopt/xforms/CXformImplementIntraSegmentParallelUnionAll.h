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

#ifndef GPOPT_CXformImplementIntraSegmentParallelUnionAll_H
#define GPOPT_CXformImplementIntraSegmentParallelUnionAll_H

#include "gpos/base.h"

#include "gpopt/xforms/CXformImplementation.h"

namespace gpopt
{
using namespace gpos;

//---------------------------------------------------------------------------
//	@class:
//		CXformImplementIntraSegmentParallelUnionAll
//
//	@doc:
//		Transform LogicalUnionAll to PhysicalParallelUnionAll with
//		intra-segment (worker-level) parallelism enabled.
//		This allows Union children to use parallel table scans.
//
//---------------------------------------------------------------------------
class CXformImplementIntraSegmentParallelUnionAll : public CXformImplementation
{
private:
public:
	CXformImplementIntraSegmentParallelUnionAll(
		const CXformImplementIntraSegmentParallelUnionAll &) = delete;

	// ctor
	explicit CXformImplementIntraSegmentParallelUnionAll(CMemoryPool *mp);

	// dtor
	~CXformImplementIntraSegmentParallelUnionAll() override = default;

	// ident accessors
	EXformId
	Exfid() const override
	{
		return ExfImplementIntraSegmentParallelUnionAll;
	}

	const CHAR *
	SzId() const override
	{
		return "CXformImplementIntraSegmentParallelUnionAll";
	}

	// compute xform promise for a given expression handle
	EXformPromise Exfp(CExpressionHandle &exprhdl) const override;

	// actual transformation
	void Transform(CXformContext *pxfctxt, CXformResult *pxfres,
				   CExpression *pexpr) const override;

};	// class CXformImplementIntraSegmentParallelUnionAll

}  // namespace gpopt

#endif	// !GPOPT_CXformImplementIntraSegmentParallelUnionAll_H
