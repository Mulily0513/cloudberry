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
 * CDXLPhysicalParallelIndexOnlyScan.cpp
 *
 * IDENTIFICATION
 *	  src/backend/gporca/libnaucrates/src/operators/CDXLPhysicalParallelIndexOnlyScan.cpp
 *
 *-------------------------------------------------------------------------
 */

#include "naucrates/dxl/operators/CDXLPhysicalParallelIndexOnlyScan.h"

#include "naucrates/dxl/operators/CDXLNode.h"
#include "naucrates/dxl/xml/CXMLSerializer.h"
#include "naucrates/md/IMDCacheObject.h"

using namespace gpos;
using namespace gpdxl;

//---------------------------------------------------------------------------
//	@function:
//		CDXLPhysicalParallelIndexOnlyScan::CDXLPhysicalParallelIndexOnlyScan
//
//	@doc:
//		Constructor
//
//---------------------------------------------------------------------------
CDXLPhysicalParallelIndexOnlyScan::CDXLPhysicalParallelIndexOnlyScan(
	CMemoryPool *mp, CDXLTableDescr *table_descr,
	CDXLIndexDescr *dxl_index_descr,
	EdxlIndexScanDirection idx_scan_direction, ULONG ulParallelWorkers)
	: CDXLPhysicalIndexOnlyScan(mp, table_descr, dxl_index_descr,
								idx_scan_direction),
	  m_ulParallelWorkers(ulParallelWorkers)
{
	GPOS_ASSERT(ulParallelWorkers > 0);
}

//---------------------------------------------------------------------------
//	@function:
//		CDXLPhysicalParallelIndexOnlyScan::GetDXLOperator
//
//	@doc:
//		Operator type
//
//---------------------------------------------------------------------------
Edxlopid
CDXLPhysicalParallelIndexOnlyScan::GetDXLOperator() const
{
	return EdxlopPhysicalParallelIndexOnlyScan;
}

//---------------------------------------------------------------------------
//	@function:
//		CDXLPhysicalParallelIndexOnlyScan::GetOpNameStr
//
//	@doc:
//		Operator name
//
//---------------------------------------------------------------------------
const CWStringConst *
CDXLPhysicalParallelIndexOnlyScan::GetOpNameStr() const
{
	return CDXLTokens::GetDXLTokenStr(
		EdxltokenPhysicalParallelIndexOnlyScan);
}

//---------------------------------------------------------------------------
//	@function:
//		CDXLPhysicalParallelIndexOnlyScan::SerializeToDXL
//
//	@doc:
//		Serialize operator in DXL format
//
//---------------------------------------------------------------------------
void
CDXLPhysicalParallelIndexOnlyScan::SerializeToDXL(
	CXMLSerializer *xml_serializer, const CDXLNode *dxlnode) const
{
	const CWStringConst *element_name = GetOpNameStr();

	xml_serializer->OpenElement(
		CDXLTokens::GetDXLTokenStr(EdxltokenNamespacePrefix), element_name);

	// serialize scan direction
	xml_serializer->AddAttribute(
		CDXLTokens::GetDXLTokenStr(EdxltokenIndexScanDirection),
		CDXLOperator::GetIdxScanDirectionStr(GetIndexScanDir()));

	// serialize parallel workers attribute
	xml_serializer->AddAttribute(
		CDXLTokens::GetDXLTokenStr(EdxltokenParallelWorkers),
		m_ulParallelWorkers);

	// serialize properties
	dxlnode->SerializePropertiesToDXL(xml_serializer);

	// serialize children
	dxlnode->SerializeChildrenToDXL(xml_serializer);

	// serialize partition mdids (null for non-partitioned)
	IMdIdArray *empty = GPOS_NEW(m_mp) IMdIdArray(m_mp);
	IMDCacheObject::SerializeMDIdList(
		xml_serializer, empty,
		CDXLTokens::GetDXLTokenStr(EdxltokenPartitions),
		CDXLTokens::GetDXLTokenStr(EdxltokenPartition));
	empty->Release();

	// serialize index descriptor
	GetDXLIndexDescr()->SerializeToDXL(xml_serializer);

	// serialize table descriptor
	GetDXLTableDescr()->SerializeToDXL(xml_serializer);

	xml_serializer->CloseElement(
		CDXLTokens::GetDXLTokenStr(EdxltokenNamespacePrefix), element_name);
}

// EOF
