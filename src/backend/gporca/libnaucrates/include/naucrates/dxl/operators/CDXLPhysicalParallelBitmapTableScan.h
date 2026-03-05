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
 * CDXLPhysicalParallelBitmapTableScan.h
 *
 * IDENTIFICATION
 *	  src/backend/gporca/libnaucrates/include/naucrates/dxl/operators/CDXLPhysicalParallelBitmapTableScan.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef GPDXL_CDXLPhysicalParallelBitmapTableScan_H
#define GPDXL_CDXLPhysicalParallelBitmapTableScan_H

#include "gpos/base.h"

#include "naucrates/dxl/operators/CDXLPhysicalAbstractBitmapScan.h"

namespace gpdxl
{
using namespace gpos;

// fwd declarations
class CDXLTableDescr;
class CXMLSerializer;

//---------------------------------------------------------------------------
//	@class:
//		CDXLPhysicalParallelBitmapTableScan
//
//	@doc:
//		Class for representing DXL parallel bitmap table scan operators
//
//---------------------------------------------------------------------------
class CDXLPhysicalParallelBitmapTableScan : public CDXLPhysicalAbstractBitmapScan
{
private:
	// number of parallel workers
	ULONG m_ulParallelWorkers;

public:
	CDXLPhysicalParallelBitmapTableScan(const CDXLPhysicalParallelBitmapTableScan &) = delete;

	// ctor
	CDXLPhysicalParallelBitmapTableScan(CMemoryPool *mp, CDXLTableDescr *table_descr,
										 ULONG ulParallelWorkers);

	// dtor
	~CDXLPhysicalParallelBitmapTableScan() override = default;

	// operator type
	Edxlopid
	GetDXLOperator() const override
	{
		return EdxlopPhysicalParallelBitmapTableScan;
	}

	// operator name
	const CWStringConst *GetOpNameStr() const override;

	// get number of parallel workers
	ULONG UlParallelWorkers() const
	{
		return m_ulParallelWorkers;
	}

	// serialize operator in DXL format
	void SerializeToDXL(CXMLSerializer *xml_serializer,
						const CDXLNode *dxlnode) const override;

	// conversion function
	static CDXLPhysicalParallelBitmapTableScan *
	Cast(CDXLOperator *dxl_op)
	{
		GPOS_ASSERT(nullptr != dxl_op);
		GPOS_ASSERT(EdxlopPhysicalParallelBitmapTableScan == dxl_op->GetDXLOperator());

		return dynamic_cast<CDXLPhysicalParallelBitmapTableScan *>(dxl_op);
	}

};	// class CDXLPhysicalParallelBitmapTableScan
}  // namespace gpdxl

#endif	// !GPDXL_CDXLPhysicalParallelBitmapTableScan_H

// EOF
