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
 * CDXLPhysicalParallelIndexScan.h
 *
 * IDENTIFICATION
 *	  src/backend/gporca/libnaucrates/include/naucrates/dxl/operators/CDXLPhysicalParallelIndexScan.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef GPDXL_CDXLPhysicalParallelIndexScan_H
#define GPDXL_CDXLPhysicalParallelIndexScan_H

#include "gpos/base.h"

#include "naucrates/dxl/operators/CDXLPhysicalIndexScan.h"

namespace gpdxl
{
//---------------------------------------------------------------------------
//	@class:
//		CDXLPhysicalParallelIndexScan
//
//	@doc:
//		Class for representing DXL parallel index scan operators
//
//---------------------------------------------------------------------------
class CDXLPhysicalParallelIndexScan : public CDXLPhysicalIndexScan
{
private:
	// number of parallel workers
	ULONG m_ulParallelWorkers;

public:
	CDXLPhysicalParallelIndexScan(CDXLPhysicalParallelIndexScan &) = delete;

	//ctor
	CDXLPhysicalParallelIndexScan(CMemoryPool *mp, CDXLTableDescr *table_descr,
								  CDXLIndexDescr *dxl_index_descr,
								  EdxlIndexScanDirection idx_scan_direction,
								  ULONG ulParallelWorkers);

	//dtor
	~CDXLPhysicalParallelIndexScan() override = default;

	// operator type
	Edxlopid GetDXLOperator() const override;

	// operator name
	const CWStringConst *GetOpNameStr() const override;

	// get number of parallel workers
	ULONG UlParallelWorkers() const
	{
		return m_ulParallelWorkers;
	}

	// serialize operator in DXL format
	void SerializeToDXL(CXMLSerializer *xml_serializer,
						const CDXLNode *node) const override;

	// conversion function
	static CDXLPhysicalParallelIndexScan *
	Cast(CDXLOperator *dxl_op)
	{
		GPOS_ASSERT(nullptr != dxl_op);
		GPOS_ASSERT(EdxlopPhysicalParallelIndexScan == dxl_op->GetDXLOperator());

		return dynamic_cast<CDXLPhysicalParallelIndexScan *>(dxl_op);
	}

#ifdef GPOS_DEBUG
	// checks whether the operator has valid structure, i.e. number and
	// types of child nodes
	void AssertValid(const CDXLNode *, BOOL validate_children) const override;
#endif	// GPOS_DEBUG
};
}  // namespace gpdxl
#endif	// !GPDXL_CDXLPhysicalParallelIndexScan_H

// EOF
