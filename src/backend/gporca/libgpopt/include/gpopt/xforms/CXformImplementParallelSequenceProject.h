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
 * CXformImplementParallelSequenceProject.h
 *
 * IDENTIFICATION
 *	  src/backend/gporca/libgpopt/include/gpopt/xforms/CXformImplementParallelSequenceProject.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef GPOPT_CXformImplementParallelSequenceProject_H
#define GPOPT_CXformImplementParallelSequenceProject_H

#include "gpos/base.h"

#include "gpopt/operators/CExpressionHandle.h"
#include "gpopt/xforms/CXformImplementation.h"

namespace gpopt
{
using namespace gpos;

//---------------------------------------------------------------------------
//	@class:
//		CXformImplementParallelSequenceProject
//
//	@doc:
//		Transform Logical Sequence Project to Parallel Physical Sequence Project
//
//---------------------------------------------------------------------------
class CXformImplementParallelSequenceProject : public CXformImplementation
{
private:
public:
	CXformImplementParallelSequenceProject(
		const CXformImplementParallelSequenceProject &) = delete;

	// ctor
	explicit CXformImplementParallelSequenceProject(CMemoryPool *mp);

	// dtor
	~CXformImplementParallelSequenceProject() override = default;

	// ident accessors
	EXformId
	Exfid() const override
	{
		return ExfImplementParallelSequenceProject;
	}

	const CHAR *
	SzId() const override
	{
		return "CXformImplementParallelSequenceProject";
	}

	// compute xform promise for a given expression handle
	EXformPromise Exfp(CExpressionHandle &exprhdl) const override;

	// actual transform
	void Transform(CXformContext *, CXformResult *,
				   CExpression *) const override;

};	// class CXformImplementParallelSequenceProject

}  // namespace gpopt

#endif	// !GPOPT_CXformImplementParallelSequenceProject_H

// EOF
