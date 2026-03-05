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
 * CDXLPhysicalParallelIndexOnlyScan.h
 *
 * IDENTIFICATION
 *	  src/backend/gporca/libnaucrates/include/naucrates/dxl/operators/CDXLPhysicalParallelIndexOnlyScan.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef GPDXL_CDXLPhysicalParallelIndexOnlyScan_H
#define GPDXL_CDXLPhysicalParallelIndexOnlyScan_H

#include "gpos/base.h"

#include "naucrates/dxl/operators/CDXLPhysicalIndexOnlyScan.h"

namespace gpdxl
{
using namespace gpos;

//---------------------------------------------------------------------------
//	@class:
//		CDXLPhysicalParallelIndexOnlyScan
//
//	@doc:
//		Class for representing DXL parallel index only scan operators
//
//---------------------------------------------------------------------------
class CDXLPhysicalParallelIndexOnlyScan : public CDXLPhysicalIndexOnlyScan
{
private:
	// number of parallel workers
	ULONG m_ulParallelWorkers;

public:
	CDXLPhysicalParallelIndexOnlyScan(
		const CDXLPhysicalParallelIndexOnlyScan &) = delete;

	// ctor
	CDXLPhysicalParallelIndexOnlyScan(CMemoryPool *mp,
									  CDXLTableDescr *table_descr,
									  CDXLIndexDescr *dxl_index_descr,
									  EdxlIndexScanDirection idx_scan_direction,
									  ULONG ulParallelWorkers);

	// dtor
	~CDXLPhysicalParallelIndexOnlyScan() override = default;

	// get operator type
	Edxlopid GetDXLOperator() const override;

	// get operator name
	const CWStringConst *GetOpNameStr() const override;

	// get number of parallel workers
	ULONG
	UlParallelWorkers() const
	{
		return m_ulParallelWorkers;
	}

	// serialize operator in DXL format
	void SerializeToDXL(CXMLSerializer *xml_serializer,
						const CDXLNode *dxlnode) const override;

	// conversion function
	static CDXLPhysicalParallelIndexOnlyScan *
	Cast(CDXLOperator *dxl_op)
	{
		GPOS_ASSERT(nullptr != dxl_op);
		GPOS_ASSERT(EdxlopPhysicalParallelIndexOnlyScan ==
					dxl_op->GetDXLOperator());

		return dynamic_cast<CDXLPhysicalParallelIndexOnlyScan *>(dxl_op);
	}

};	// class CDXLPhysicalParallelIndexOnlyScan

}  // namespace gpdxl

#endif	// !GPDXL_CDXLPhysicalParallelIndexOnlyScan_H

// EOF
