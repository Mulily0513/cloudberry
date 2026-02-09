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
 * CDXLPhysicalParallelIndexScan.cpp
 *
 * IDENTIFICATION
 *	  src/backend/gporca/libnaucrates/src/operators/CDXLPhysicalParallelIndexScan.cpp
 *
 *-------------------------------------------------------------------------
 */

#include "naucrates/dxl/operators/CDXLPhysicalParallelIndexScan.h"

#include "naucrates/dxl/operators/CDXLNode.h"
#include "naucrates/dxl/xml/CXMLSerializer.h"
#include "naucrates/md/IMDCacheObject.h"

using namespace gpos;
using namespace gpdxl;

//---------------------------------------------------------------------------
//	@function:
//		CDXLPhysicalParallelIndexScan::CDXLPhysicalParallelIndexScan
//
//	@doc:
//		Construct a parallel index scan node given its table descriptor,
//		index descriptor, filter conditions and parallel workers on the index
//
//---------------------------------------------------------------------------
CDXLPhysicalParallelIndexScan::CDXLPhysicalParallelIndexScan(
	CMemoryPool *mp, CDXLTableDescr *table_descr,
	CDXLIndexDescr *dxl_index_descr, EdxlIndexScanDirection idx_scan_direction,
	ULONG ulParallelWorkers)
	: CDXLPhysicalIndexScan(mp, table_descr, dxl_index_descr, idx_scan_direction),
	  m_ulParallelWorkers(ulParallelWorkers)
{
	GPOS_ASSERT(ulParallelWorkers > 0);
}

//---------------------------------------------------------------------------
//	@function:
//		CDXLPhysicalParallelIndexScan::GetDXLOperator
//
//	@doc:
//		Operator type
//
//---------------------------------------------------------------------------
Edxlopid
CDXLPhysicalParallelIndexScan::GetDXLOperator() const
{
	return EdxlopPhysicalParallelIndexScan;
}

//---------------------------------------------------------------------------
//	@function:
//		CDXLPhysicalParallelIndexScan::GetOpNameStr
//
//	@doc:
//		Operator name
//
//---------------------------------------------------------------------------
const CWStringConst *
CDXLPhysicalParallelIndexScan::GetOpNameStr() const
{
	return CDXLTokens::GetDXLTokenStr(EdxltokenPhysicalParallelIndexScan);
}

//---------------------------------------------------------------------------
//	@function:
//		CDXLPhysicalParallelIndexScan::SerializeToDXL
//
//	@doc:
//		Serialize operator in DXL format
//
//---------------------------------------------------------------------------
void
CDXLPhysicalParallelIndexScan::SerializeToDXL(CXMLSerializer *xml_serializer,
									  const CDXLNode *node) const
{
	const CWStringConst *element_name = GetOpNameStr();
	EdxlIndexScanDirection m_index_scan_dir = GetIndexScanDir();
	const CDXLIndexDescr *m_dxl_index_descr = GetDXLIndexDescr();
	const CDXLTableDescr *m_dxl_table_descr = GetDXLTableDescr();

	xml_serializer->OpenElement(
		CDXLTokens::GetDXLTokenStr(EdxltokenNamespacePrefix), element_name);
	xml_serializer->AddAttribute(
		CDXLTokens::GetDXLTokenStr(EdxltokenIndexScanDirection),
		CDXLOperator::GetIdxScanDirectionStr(m_index_scan_dir));

	// serialize parallel workers attribute
	xml_serializer->AddAttribute(
		CDXLTokens::GetDXLTokenStr(EdxltokenParallelWorkers),
		m_ulParallelWorkers);

	// serialize properties
	node->SerializePropertiesToDXL(xml_serializer);

	// serialize children
	node->SerializeChildrenToDXL(xml_serializer);

	// serialize partition mdids (null in this case)
	IMdIdArray *empty = GPOS_NEW(m_mp) IMdIdArray(m_mp);
	IMDCacheObject::SerializeMDIdList(
		xml_serializer, empty, CDXLTokens::GetDXLTokenStr(EdxltokenPartitions),
		CDXLTokens::GetDXLTokenStr(EdxltokenPartition));
	empty->Release();
	// serialize index descriptor
	m_dxl_index_descr->SerializeToDXL(xml_serializer);

	// serialize table descriptor
	m_dxl_table_descr->SerializeToDXL(xml_serializer);

	xml_serializer->CloseElement(
		CDXLTokens::GetDXLTokenStr(EdxltokenNamespacePrefix), element_name);
}

#ifdef GPOS_DEBUG
//---------------------------------------------------------------------------
//	@function:
//		CDXLPhysicalParallelIndexScan::AssertValid
//
//	@doc:
//		Checks whether operator node is well-structured
//
//---------------------------------------------------------------------------
void
CDXLPhysicalParallelIndexScan::AssertValid(const CDXLNode *node,
								   BOOL validate_children) const
{
	const CDXLIndexDescr *m_dxl_index_descr = GetDXLIndexDescr();
	const CDXLTableDescr *m_dxl_table_descr = GetDXLTableDescr();

	// assert proj list and filter are valid
	CDXLPhysical::AssertValid(node, validate_children);

	// index scan has only 3 children
	GPOS_ASSERT(3 == node->Arity());

	// assert validity of the index descriptor
	GPOS_ASSERT(nullptr != m_dxl_index_descr);
	GPOS_ASSERT(nullptr != m_dxl_index_descr->MdName());
	GPOS_ASSERT(m_dxl_index_descr->MdName()->GetMDName()->IsValid());

	// assert validity of the table descriptor
	GPOS_ASSERT(nullptr != m_dxl_table_descr);
	GPOS_ASSERT(nullptr != m_dxl_table_descr->MdName());
	GPOS_ASSERT(m_dxl_table_descr->MdName()->GetMDName()->IsValid());

	CDXLNode *index_cond_dxlnode = (*node)[EdxlisIndexCondition];

	// assert children are of right type (physical/scalar)
	GPOS_ASSERT(EdxlopScalarIndexCondList ==
				index_cond_dxlnode->GetOperator()->GetDXLOperator());

	if (validate_children)
	{
		index_cond_dxlnode->GetOperator()->AssertValid(index_cond_dxlnode,
													   validate_children);
	}
}
#endif	// GPOS_DEBUG

// EOF
