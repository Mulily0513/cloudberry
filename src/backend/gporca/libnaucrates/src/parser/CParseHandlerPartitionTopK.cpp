//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2026 Hashdata, Inc.
//
//	@filename:
//		CParseHandlerPartitionTopK.cpp
//
//	@doc:
//		Implementation of the SAX parse handler class for parsing
//		PartitionTopK operator nodes.
//
//		The XML format produced by CDXLPhysicalPartitionTopK::SerializeToDXL is:
//
//		<dxl:PartitionTopK n="2">
//		  <dxl:Properties>...</dxl:Properties>
//		  <dxl:ProjList>...</dxl:ProjList>
//		  <dxl:Filter/>
//		  <dxl:SomePhysicalOp>...</dxl:SomePhysicalOp>
//		  <dxl:partCols>
//		    <dxl:ColRef ColId="1"/>
//		    ...
//		  </dxl:partCols>
//		  <dxl:sortCols>
//		    <dxl:SortingColumn ColId="..." SortOpId="..." ... />
//		    ...
//		  </dxl:sortCols>
//		</dxl:PartitionTopK>
//
//		After the child parse handlers (Properties, ProjList, Filter, Child)
//		finish, control returns to this handler for partCols and sortCols.
//---------------------------------------------------------------------------

#include "naucrates/dxl/parser/CParseHandlerPartitionTopK.h"

#include "naucrates/dxl/CDXLUtils.h"
#include "naucrates/dxl/operators/CDXLOperatorFactory.h"
#include "naucrates/dxl/parser/CParseHandlerFactory.h"
#include "naucrates/dxl/parser/CParseHandlerFilter.h"
#include "naucrates/dxl/parser/CParseHandlerProjList.h"
#include "naucrates/dxl/parser/CParseHandlerProperties.h"
#include "naucrates/dxl/parser/CParseHandlerUtils.h"
#include "naucrates/md/CMDIdGPDB.h"

using namespace gpdxl;
using namespace gpmd;

XERCES_CPP_NAMESPACE_USE

//---------------------------------------------------------------------------
//	@function:
//		CParseHandlerPartitionTopK::CParseHandlerPartitionTopK
//
//	@doc:
//		Constructor
//
//---------------------------------------------------------------------------
CParseHandlerPartitionTopK::CParseHandlerPartitionTopK(
	CMemoryPool *mp, CParseHandlerManager *parse_handler_mgr,
	CParseHandlerBase *parse_handler_root)
	: CParseHandlerPhysicalOp(mp, parse_handler_mgr, parse_handler_root),
	  m_parse_state(eParseInit),
	  m_n(-1),
	  m_part_col_refs(nullptr),
	  m_sort_cols(nullptr)
{
}


//---------------------------------------------------------------------------
//	@function:
//		CParseHandlerPartitionTopK::StartElement
//
//	@doc:
//		Invoked by Xerces to process an opening tag.
//		Uses a state machine to handle the PartitionTopK element and its
//		inline partCols / sortCols sections that appear after standard children.
//
//---------------------------------------------------------------------------
void
CParseHandlerPartitionTopK::StartElement(const XMLCh *const,  // element_uri
										 const XMLCh *const element_local_name,
										 const XMLCh *const,  // element_qname
										 const Attributes &attrs)
{
	if (m_parse_state == eParseInit &&
		0 == XMLString::compareString(
				 CDXLTokens::XmlstrToken(EdxltokenPhysicalPartitionTopK),
				 element_local_name))
	{
		// Parse the 'n' attribute from <dxl:PartitionTopK n="...">
		m_n = CDXLOperatorFactory::ExtractConvertAttrValueToInt(
			m_parse_handler_mgr->GetDXLMemoryManager(), attrs, EdxltokenN,
			EdxltokenPhysicalPartitionTopK);

		// Initialize arrays to collect partition and sort columns
		m_part_col_refs = GPOS_NEW(m_mp) CDXLColRefArray(m_mp);
		m_sort_cols = GPOS_NEW(m_mp) CDXLSortColArray(m_mp);

		// Activate child parse handlers in REVERSE order of expected appearance.
		// The last activated is the first to receive events.

		// parse handler for the child physical operator
		CParseHandlerBase *child_parse_handler =
			CParseHandlerFactory::GetParseHandler(
				m_mp, CDXLTokens::XmlstrToken(EdxltokenPhysical),
				m_parse_handler_mgr, this);
		m_parse_handler_mgr->ActivateParseHandler(child_parse_handler);

		// parse handler for the filter
		CParseHandlerBase *filter_parse_handler =
			CParseHandlerFactory::GetParseHandler(
				m_mp, CDXLTokens::XmlstrToken(EdxltokenScalarFilter),
				m_parse_handler_mgr, this);
		m_parse_handler_mgr->ActivateParseHandler(filter_parse_handler);

		// parse handler for the proj list
		CParseHandlerBase *proj_list_parse_handler =
			CParseHandlerFactory::GetParseHandler(
				m_mp, CDXLTokens::XmlstrToken(EdxltokenScalarProjList),
				m_parse_handler_mgr, this);
		m_parse_handler_mgr->ActivateParseHandler(proj_list_parse_handler);

		// parse handler for the properties
		CParseHandlerBase *prop_parse_handler =
			CParseHandlerFactory::GetParseHandler(
				m_mp, CDXLTokens::XmlstrToken(EdxltokenProperties),
				m_parse_handler_mgr, this);
		m_parse_handler_mgr->ActivateParseHandler(prop_parse_handler);

		// Store in order: [0] properties, [1] projlist, [2] filter, [3] child
		this->Append(prop_parse_handler);
		this->Append(proj_list_parse_handler);
		this->Append(filter_parse_handler);
		this->Append(child_parse_handler);

		m_parse_state = eParseChildHandlersActive;
	}
	else if (m_parse_state == eParseChildHandlersActive &&
			 0 == XMLString::compareString(
					  CDXLTokens::XmlstrToken(EdxltokenPartCols),
					  element_local_name))
	{
		// Entering <dxl:partCols> section (only valid after child handlers done)
		m_parse_state = eParsePartCols;
	}
	else if (m_parse_state == eParsePartCols &&
			 0 == XMLString::compareString(
					  CDXLTokens::XmlstrToken(EdxltokenColRef),
					  element_local_name))
	{
		// Parse <dxl:ColRef ColId="..." ColName="..." TypeMdid="..."/>
		ULONG col_id = CDXLOperatorFactory::ExtractConvertAttrValueToUlong(
			m_parse_handler_mgr->GetDXLMemoryManager(), attrs, EdxltokenColId,
			EdxltokenColRef);

		const XMLCh *col_name_xml =
			attrs.getValue(CDXLTokens::XmlstrToken(EdxltokenColName));
		CMDName *mdname = nullptr;
		if (nullptr != col_name_xml)
		{
			CWStringDynamic *col_name_str =
				CDXLUtils::CreateDynamicStringFromXMLChArray(
					m_parse_handler_mgr->GetDXLMemoryManager(), col_name_xml);
			mdname = GPOS_NEW(m_mp) CMDName(m_mp, col_name_str);
			GPOS_DELETE(col_name_str);
		}
		else
		{
			CWStringConst str_empty(GPOS_WSZ_LIT(""));
			mdname = GPOS_NEW(m_mp) CMDName(m_mp, &str_empty);
		}

		IMDId *mdid_type = nullptr;
		const XMLCh *type_xml =
			attrs.getValue(CDXLTokens::XmlstrToken(EdxltokenTypeId));
		if (nullptr != type_xml)
		{
			mdid_type = CDXLOperatorFactory::ExtractConvertAttrValueToMdId(
				m_parse_handler_mgr->GetDXLMemoryManager(), attrs,
				EdxltokenTypeId, EdxltokenColRef);
		}
		else
		{
			mdid_type = GPOS_NEW(m_mp)
				CMDIdGPDB(IMDId::EmdidGeneral, 23 /* int4 fallback */, 1, 0);
		}

		CDXLColRef *col_ref = GPOS_NEW(m_mp)
			CDXLColRef(mdname, col_id, mdid_type, default_type_modifier);
		m_part_col_refs->Append(col_ref);
	}
	else if (m_parse_state == eParseChildHandlersActive &&
			 0 == XMLString::compareString(
					  CDXLTokens::XmlstrToken(EdxltokenSortCols),
					  element_local_name))
	{
		// Entering <dxl:sortCols> section (only valid after partCols done)
		m_parse_state = eParseSortCols;
	}
	else if (m_parse_state == eParseSortCols &&
			 0 == XMLString::compareString(
					  CDXLTokens::XmlstrToken(EdxltokenScalarSortCol),
					  element_local_name))
	{
		// Parse <dxl:SortingColumn .../> inside sortCols
		CDXLScalarSortCol *sort_col =
			(CDXLScalarSortCol *) CDXLOperatorFactory::MakeDXLSortCol(
				m_parse_handler_mgr->GetDXLMemoryManager(), attrs);
		m_sort_cols->Append(sort_col);
	}
	else
	{
		CWStringDynamic *str = CDXLUtils::CreateDynamicStringFromXMLChArray(
			m_parse_handler_mgr->GetDXLMemoryManager(), element_local_name);
		GPOS_RAISE(gpdxl::ExmaDXL, gpdxl::ExmiDXLUnexpectedTag,
				   str->GetBuffer());
	}
}


//---------------------------------------------------------------------------
//	@function:
//		CParseHandlerPartitionTopK::EndElement
//
//	@doc:
//		Invoked by Xerces to process a closing tag.
//		When </dxl:PartitionTopK> is received, constructs the CDXLNode
//		from collected children, partition columns, and sort columns.
//
//---------------------------------------------------------------------------
void
CParseHandlerPartitionTopK::EndElement(const XMLCh *const,	 // element_uri
									   const XMLCh *const element_local_name,
									   const XMLCh *const  // element_qname
)
{
	if (0 == XMLString::compareString(
				 CDXLTokens::XmlstrToken(EdxltokenPartCols),
				 element_local_name))
	{
		// </dxl:partCols> - done collecting partition columns
		GPOS_ASSERT(eParsePartCols == m_parse_state);
		m_parse_state = eParseChildHandlersActive;
		return;
	}

	if (0 == XMLString::compareString(
				 CDXLTokens::XmlstrToken(EdxltokenColRef), element_local_name))
	{
		// </dxl:ColRef> - self-closing element, nothing to do
		return;
	}

	if (0 == XMLString::compareString(
				 CDXLTokens::XmlstrToken(EdxltokenSortCols),
				 element_local_name))
	{
		// </dxl:sortCols> - done collecting sort columns
		GPOS_ASSERT(eParseSortCols == m_parse_state);
		m_parse_state = eParseChildHandlersActive;
		return;
	}

	if (0 == XMLString::compareString(
				 CDXLTokens::XmlstrToken(EdxltokenScalarSortCol),
				 element_local_name))
	{
		// </dxl:SortingColumn> - self-closing element, nothing to do
		return;
	}

	if (0 != XMLString::compareString(
				 CDXLTokens::XmlstrToken(EdxltokenPhysicalPartitionTopK),
				 element_local_name))
	{
		CWStringDynamic *str = CDXLUtils::CreateDynamicStringFromXMLChArray(
			m_parse_handler_mgr->GetDXLMemoryManager(), element_local_name);
		GPOS_RAISE(gpdxl::ExmaDXL, gpdxl::ExmiDXLUnexpectedTag,
				   str->GetBuffer());
	}

	// </dxl:PartitionTopK> - assemble the final CDXLNode

	// Retrieve child parse handlers
	GPOS_ASSERT(4 == this->Length());
	CParseHandlerProperties *prop_parse_handler =
		dynamic_cast<CParseHandlerProperties *>((*this)[0]);
	CParseHandlerProjList *proj_list_parse_handler =
		dynamic_cast<CParseHandlerProjList *>((*this)[1]);
	CParseHandlerFilter *filter_parse_handler =
		dynamic_cast<CParseHandlerFilter *>((*this)[2]);
	CParseHandlerPhysicalOp *child_parse_handler =
		dynamic_cast<CParseHandlerPhysicalOp *>((*this)[3]);

	GPOS_ASSERT(nullptr != m_part_col_refs);
	GPOS_ASSERT(nullptr != m_sort_cols);

	// Create the DXL operator
	CDXLPhysicalPartitionTopK *dxl_op = GPOS_NEW(m_mp)
		CDXLPhysicalPartitionTopK(m_mp, m_part_col_refs, m_sort_cols, m_n);

	// Create the DXL node
	m_dxl_node = GPOS_NEW(m_mp) CDXLNode(m_mp, dxl_op);

	// Set properties (cost, stats, etc.)
	CParseHandlerUtils::SetProperties(m_dxl_node, prop_parse_handler);

	// Add children: projlist, filter, child operator (3 children total)
	AddChildFromParseHandler(proj_list_parse_handler);
	AddChildFromParseHandler(filter_parse_handler);
	AddChildFromParseHandler(child_parse_handler);

	m_parse_state = eParseDone;

	// Deactivate handler
	m_parse_handler_mgr->DeactivateHandler();
}

// EOF
