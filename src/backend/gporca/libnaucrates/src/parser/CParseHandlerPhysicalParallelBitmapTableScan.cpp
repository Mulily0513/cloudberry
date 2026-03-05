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
 * CParseHandlerPhysicalParallelBitmapTableScan.cpp
 *
 * IDENTIFICATION
 *	  src/backend/gporca/libnaucrates/src/parser/CParseHandlerPhysicalParallelBitmapTableScan.cpp
 *
 *-------------------------------------------------------------------------
 */

#include "naucrates/dxl/parser/CParseHandlerPhysicalParallelBitmapTableScan.h"

#include "naucrates/dxl/operators/CDXLOperatorFactory.h"
#include "naucrates/dxl/operators/CDXLPhysicalParallelBitmapTableScan.h"
#include "naucrates/dxl/parser/CParseHandlerFactory.h"
#include "naucrates/dxl/parser/CParseHandlerFilter.h"
#include "naucrates/dxl/parser/CParseHandlerProjList.h"
#include "naucrates/dxl/parser/CParseHandlerProperties.h"
#include "naucrates/dxl/parser/CParseHandlerScalarOp.h"
#include "naucrates/dxl/parser/CParseHandlerTableDescr.h"
#include "naucrates/dxl/parser/CParseHandlerUtils.h"
#include "naucrates/dxl/xml/dxltokens.h"

using namespace gpdxl;

//---------------------------------------------------------------------------
//	@function:
//		CParseHandlerPhysicalParallelBitmapTableScan::StartElement
//
//	@doc:
//		Invoked by Xerces to process an opening tag
//
//---------------------------------------------------------------------------
void
CParseHandlerPhysicalParallelBitmapTableScan::StartElement(
	const XMLCh *const,	 // element_uri
	const XMLCh *const element_local_name,
	const XMLCh *const,	 // element_qname
	const Attributes &attrs)
{
	// Extract parallel workers attribute before calling the base helper
	const XMLCh *parallel_workers_xml = CDXLOperatorFactory::ExtractAttrValue(
		attrs, EdxltokenParallelWorkers, EdxltokenPhysicalParallelBitmapTableScan);
	m_ulParallelWorkers = CDXLOperatorFactory::ConvertAttrValueToUlong(
		m_parse_handler_mgr->GetDXLMemoryManager(), parallel_workers_xml,
		EdxltokenParallelWorkers, EdxltokenPhysicalParallelBitmapTableScan);

	StartElementHelper(element_local_name, EdxltokenPhysicalParallelBitmapTableScan);
}

//---------------------------------------------------------------------------
//	@function:
//		CParseHandlerPhysicalParallelBitmapTableScan::EndElement
//
//	@doc:
//		Invoked by Xerces to process a closing tag
//
//---------------------------------------------------------------------------
void
CParseHandlerPhysicalParallelBitmapTableScan::EndElement(
	const XMLCh *const,	 // element_uri
	const XMLCh *const element_local_name,
	const XMLCh *const	// element_qname
)
{
	Edxltoken token_type = EdxltokenPhysicalParallelBitmapTableScan;

	if (0 != XMLString::compareString(CDXLTokens::XmlstrToken(token_type),
									  element_local_name))
	{
		CWStringDynamic *str = CDXLUtils::CreateDynamicStringFromXMLChArray(
			m_parse_handler_mgr->GetDXLMemoryManager(), element_local_name);
		GPOS_RAISE(gpdxl::ExmaDXL, gpdxl::ExmiDXLUnexpectedTag,
				   str->GetBuffer());
	}

	int i = 0;
	CParseHandlerProperties *prop_parse_handler =
		dynamic_cast<CParseHandlerProperties *>((*this)[i++]);
	CParseHandlerProjList *proj_list_parse_handler =
		dynamic_cast<CParseHandlerProjList *>((*this)[i++]);
	CParseHandlerFilter *filter_parse_handler =
		dynamic_cast<CParseHandlerFilter *>((*this)[i++]);
	CParseHandlerFilter *recheck_cond_parse_handler =
		dynamic_cast<CParseHandlerFilter *>((*this)[i++]);
	CParseHandlerScalarOp *bitmap_parse_handler =
		dynamic_cast<CParseHandlerScalarOp *>((*this)[i++]);
	CParseHandlerTableDescr *table_descr_parse_handler =
		dynamic_cast<CParseHandlerTableDescr *>((*this)[i++]);

	GPOS_ASSERT(nullptr != table_descr_parse_handler->GetDXLTableDescr());

	CDXLTableDescr *table_descr = table_descr_parse_handler->GetDXLTableDescr();
	table_descr->AddRef();

	CDXLPhysicalParallelBitmapTableScan *dxl_op =
		GPOS_NEW(m_mp) CDXLPhysicalParallelBitmapTableScan(m_mp, table_descr, m_ulParallelWorkers);

	m_dxl_node = GPOS_NEW(m_mp) CDXLNode(m_mp, dxl_op);

	// set statistics and physical properties
	CParseHandlerUtils::SetProperties(m_dxl_node, prop_parse_handler);

	// add constructed children
	AddChildFromParseHandler(proj_list_parse_handler);
	AddChildFromParseHandler(filter_parse_handler);
	AddChildFromParseHandler(recheck_cond_parse_handler);
	AddChildFromParseHandler(bitmap_parse_handler);

#ifdef GPOS_DEBUG
	dxl_op->AssertValid(m_dxl_node, false /* validate_children */);
#endif	// GPOS_DEBUG

	// deactivate handler
	m_parse_handler_mgr->DeactivateHandler();
}
