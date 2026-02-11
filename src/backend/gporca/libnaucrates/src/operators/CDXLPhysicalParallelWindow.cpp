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
 * CDXLPhysicalParallelWindow.cpp
 *
 * IDENTIFICATION
 *	  src/backend/gporca/libnaucrates/src/operators/CDXLPhysicalParallelWindow.cpp
 *
 *-------------------------------------------------------------------------
 */

#include "naucrates/dxl/operators/CDXLPhysicalParallelWindow.h"

#include "naucrates/dxl/CDXLUtils.h"
#include "naucrates/dxl/operators/CDXLNode.h"
#include "naucrates/dxl/xml/CXMLSerializer.h"

using namespace gpos;
using namespace gpdxl;

//---------------------------------------------------------------------------
//	@function:
//		CDXLPhysicalParallelWindow::CDXLPhysicalParallelWindow
//
//	@doc:
//		Ctor
//
//---------------------------------------------------------------------------
CDXLPhysicalParallelWindow::CDXLPhysicalParallelWindow(
	CMemoryPool *mp, ULongPtrArray *part_by_colid_array,
	CDXLWindowKeyArray *window_key_array, BOOL fWindowHashAgg,
	ULONG parallel_workers)
	: CDXLPhysicalWindow(mp, part_by_colid_array, window_key_array,
						 fWindowHashAgg),
	  m_parallel_workers(parallel_workers)
{
	GPOS_ASSERT(parallel_workers > 0);
}


//---------------------------------------------------------------------------
//	@function:
//		CDXLPhysicalParallelWindow::GetDXLOperator
//
//	@doc:
//		Operator type
//
//---------------------------------------------------------------------------
Edxlopid
CDXLPhysicalParallelWindow::GetDXLOperator() const
{
	return EdxlopPhysicalParallelWindow;
}


//---------------------------------------------------------------------------
//	@function:
//		CDXLPhysicalParallelWindow::GetOpNameStr
//
//	@doc:
//		Operator name
//
//---------------------------------------------------------------------------
const CWStringConst *
CDXLPhysicalParallelWindow::GetOpNameStr() const
{
	return CDXLTokens::GetDXLTokenStr(EdxltokenPhysicalParallelWindow);
}


//---------------------------------------------------------------------------
//	@function:
//		CDXLPhysicalParallelWindow::SerializeToDXL
//
//	@doc:
//		Serialize operator in DXL format
//
//---------------------------------------------------------------------------
void
CDXLPhysicalParallelWindow::SerializeToDXL(CXMLSerializer *xml_serializer,
										   const CDXLNode *dxlnode) const
{
	const CWStringConst *element_name = GetOpNameStr();

	xml_serializer->OpenElement(
		CDXLTokens::GetDXLTokenStr(EdxltokenNamespacePrefix), element_name);

	// serialize partition keys
	CWStringDynamic *part_by_cols_str =
		CDXLUtils::Serialize(m_mp, GetPartByColsArray());
	xml_serializer->AddAttribute(CDXLTokens::GetDXLTokenStr(EdxltokenPartKeys),
								 part_by_cols_str);
	GPOS_DELETE(part_by_cols_str);

	// serialize parallel workers
	xml_serializer->AddAttribute(
		CDXLTokens::GetDXLTokenStr(EdxltokenParallelWorkers),
		m_parallel_workers);

	// serialize window hash agg flag as attribute on the opening tag
	xml_serializer->AddAttribute(
		CDXLTokens::GetDXLTokenStr(EdxltokenWindowHashAgg), IsWindowHashAgg());

	// serialize properties
	dxlnode->SerializePropertiesToDXL(xml_serializer);

	// serialize children
	dxlnode->SerializeChildrenToDXL(xml_serializer);

	// serialize the list of window keys
	const CWStringConst *window_keys_list_str =
		CDXLTokens::GetDXLTokenStr(EdxltokenWindowKeyList);
	xml_serializer->OpenElement(
		CDXLTokens::GetDXLTokenStr(EdxltokenNamespacePrefix),
		window_keys_list_str);
	const ULONG size = WindowKeysCount();
	for (ULONG ul = 0; ul < size; ul++)
	{
		CDXLWindowKey *window_key_dxlnode = GetDXLWindowKeyAt(ul);
		window_key_dxlnode->SerializeToDXL(xml_serializer);
	}
	xml_serializer->CloseElement(
		CDXLTokens::GetDXLTokenStr(EdxltokenNamespacePrefix),
		window_keys_list_str);

	xml_serializer->CloseElement(
		CDXLTokens::GetDXLTokenStr(EdxltokenNamespacePrefix), element_name);
}

#ifdef GPOS_DEBUG
//---------------------------------------------------------------------------
//	@function:
//		CDXLPhysicalParallelWindow::AssertValid
//
//	@doc:
//		Checks whether operator node is well-structured
//
//---------------------------------------------------------------------------
void
CDXLPhysicalParallelWindow::AssertValid(const CDXLNode *dxlnode,
										BOOL validate_children) const
{
	CDXLPhysical::AssertValid(dxlnode, validate_children);
	GPOS_ASSERT(EdxlwindowIndexSentinel == dxlnode->Arity());
	CDXLNode *child_dxlnode = (*dxlnode)[EdxlwindowIndexChild];
	if (validate_children)
	{
		child_dxlnode->GetOperator()->AssertValid(child_dxlnode,
												  validate_children);
	}
}
#endif	// GPOS_DEBUG

// EOF
