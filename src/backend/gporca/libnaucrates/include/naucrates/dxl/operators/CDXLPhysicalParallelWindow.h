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
 * CDXLPhysicalParallelWindow.h
 *
 * IDENTIFICATION
 *	  src/backend/gporca/libnaucrates/include/naucrates/dxl/operators/CDXLPhysicalParallelWindow.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef GPDXL_CDXLPhysicalParallelWindow_H
#define GPDXL_CDXLPhysicalParallelWindow_H

#include "gpos/base.h"

#include "naucrates/dxl/operators/CDXLPhysicalWindow.h"

namespace gpdxl
{
//---------------------------------------------------------------------------
//	@class:
//		CDXLPhysicalParallelWindow
//
//	@doc:
//		Class for representing DXL parallel window operators.
//		Extends CDXLPhysicalWindow with parallel worker count.
//
//---------------------------------------------------------------------------
class CDXLPhysicalParallelWindow : public CDXLPhysicalWindow
{
private:
	// number of parallel workers
	ULONG m_parallel_workers;

public:
	CDXLPhysicalParallelWindow(CDXLPhysicalParallelWindow &) = delete;

	// ctor
	CDXLPhysicalParallelWindow(CMemoryPool *mp,
							   ULongPtrArray *part_by_colid_array,
							   CDXLWindowKeyArray *window_key_array,
							   BOOL fWindowHashAgg,
							   ULONG parallel_workers);

	// dtor
	~CDXLPhysicalParallelWindow() override = default;

	// accessors
	Edxlopid GetDXLOperator() const override;
	const CWStringConst *GetOpNameStr() const override;

	// parallel worker count
	ULONG
	ParallelWorkers() const
	{
		return m_parallel_workers;
	}

	// serialize operator in DXL format
	void SerializeToDXL(CXMLSerializer *xml_serializer,
						const CDXLNode *dxlnode) const override;

	// conversion function
	static CDXLPhysicalParallelWindow *
	Cast(CDXLOperator *dxl_op)
	{
		GPOS_ASSERT(nullptr != dxl_op);
		GPOS_ASSERT(EdxlopPhysicalParallelWindow == dxl_op->GetDXLOperator());

		return dynamic_cast<CDXLPhysicalParallelWindow *>(dxl_op);
	}

#ifdef GPOS_DEBUG
	// checks whether the operator has valid structure
	void AssertValid(const CDXLNode *, BOOL validate_children) const override;
#endif	// GPOS_DEBUG
};
}  // namespace gpdxl
#endif	// !GPDXL_CDXLPhysicalParallelWindow_H

// EOF
