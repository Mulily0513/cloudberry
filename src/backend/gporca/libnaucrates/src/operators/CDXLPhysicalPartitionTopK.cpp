//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2026 Hashdata, Inc.
//
//	@filename:
//		CDXLPhysicalPartitionTopK.cpp
//
//	@doc:
//		Implementation of DXL physical PartitionTopK operator
//
//---------------------------------------------------------------------------

#include "naucrates/dxl/operators/CDXLPhysicalPartitionTopK.h"

#include "gpos/string/CWStringConst.h"

#include "naucrates/dxl/CDXLUtils.h"
#include "naucrates/dxl/operators/CDXLNode.h"
#include "naucrates/dxl/xml/CXMLSerializer.h"
#include "naucrates/dxl/xml/dxltokens.h"

using namespace gpos;
using namespace gpdxl;

//---------------------------------------------------------------------------
//	@function:
//		CDXLPhysicalPartitionTopK::CDXLPhysicalPartitionTopK
//
//	@doc:
//		Ctor
//
//---------------------------------------------------------------------------
CDXLPhysicalPartitionTopK::CDXLPhysicalPartitionTopK(
	CMemoryPool *mp, CDXLColRefArray *pdrgdxlcrPart,
	CDXLSortColArray *pdrgdxlsc, INT n)
	: CDXLPhysical(mp),
	  m_pdrgdxlcrPart(pdrgdxlcrPart),
	  m_pdrgdxlsc(pdrgdxlsc),
	  m_iN(n)
{
	GPOS_ASSERT(nullptr != pdrgdxlcrPart);
	GPOS_ASSERT(nullptr != pdrgdxlsc);
	GPOS_ASSERT(0 < n);
}

//---------------------------------------------------------------------------
//	@function:
//		CDXLPhysicalPartitionTopK::～CDXLPhysicalPartitionTopK
//
//	@doc:
//		Dtor
//
//---------------------------------------------------------------------------
CDXLPhysicalPartitionTopK::~CDXLPhysicalPartitionTopK()
{
	CRefCount::SafeRelease(m_pdrgdxlcrPart);
	CRefCount::SafeRelease(m_pdrgdxlsc);
}

// 👇 IMPLEMENT Cast
CDXLPhysicalPartitionTopK *
CDXLPhysicalPartitionTopK::Cast(CDXLOperator *op)
{
	GPOS_ASSERT(nullptr != op);
	GPOS_ASSERT(EdxlopPhysicalPartitionTopK == op->GetDXLOperator());
	return dynamic_cast<CDXLPhysicalPartitionTopK *>(op);
}


//---------------------------------------------------------------------------
//	@function:
//		CDXLPhysicalPartitionTopK::GetOpNameStr
//
//	@doc:
//		Returns the name of the DXL operator
//
//---------------------------------------------------------------------------
const CWStringConst *
CDXLPhysicalPartitionTopK::GetOpNameStr() const
{
	return CDXLTokens::GetDXLTokenStr(EdxltokenPhysicalPartitionTopK);
}

//---------------------------------------------------------------------------
//	@function:
//		CDXLPhysicalPartitionTopK::GetDXLOperator
//
//	@doc:
//		Returns the DXL operator type
//
//---------------------------------------------------------------------------
Edxlopid
CDXLPhysicalPartitionTopK::GetDXLOperator() const
{
	return EdxlopPhysicalPartitionTopK;
}

//---------------------------------------------------------------------------
//	@function:
//		CDXLPhysicalPartitionTopK::SerializeToDXL
//
//	@doc:
//		Serialize operator in DXL format
//
//---------------------------------------------------------------------------
void
CDXLPhysicalPartitionTopK::SerializeToDXL(CXMLSerializer *xml_serializer,
										  const CDXLNode *dxlnode) const
{
	const CWStringConst *element_name = GetOpNameStr();
	const CWStringConst *namespace_prefix =
		CDXLTokens::GetDXLTokenStr(EdxltokenNamespacePrefix);

	xml_serializer->OpenElement(namespace_prefix, element_name);

	// Serialize 'n' attribute IMMEDIATELY after opening the element
	xml_serializer->AddAttribute(CDXLTokens::GetDXLTokenStr(EdxltokenN), m_iN);

	// Serialize properties and children first (e.g., cost, stats, child plans)
	dxlnode->SerializePropertiesToDXL(xml_serializer);
	dxlnode->SerializeChildrenToDXL(xml_serializer);

	// Serialize partition columns: <dxl:partCols> ... </dxl:partCols>
	xml_serializer->OpenElement(namespace_prefix,
								CDXLTokens::GetDXLTokenStr(EdxltokenPartCols));

	for (ULONG ul = 0; ul < m_pdrgdxlcrPart->Size(); ul++)
	{
		CDXLColRef *dxl_col_ref = (*m_pdrgdxlcrPart)[ul];

		xml_serializer->OpenElement(
			namespace_prefix, CDXLTokens::GetDXLTokenStr(EdxltokenColRef));
		xml_serializer->AddAttribute(CDXLTokens::GetDXLTokenStr(EdxltokenColId),
									 dxl_col_ref->Id());
		xml_serializer->AddAttribute(
			CDXLTokens::GetDXLTokenStr(EdxltokenColName),
			dxl_col_ref->MdName()->GetMDName());
		dxl_col_ref->MdidType()->Serialize(
			xml_serializer, CDXLTokens::GetDXLTokenStr(EdxltokenTypeId));
		xml_serializer->CloseElement(
			namespace_prefix, CDXLTokens::GetDXLTokenStr(EdxltokenColRef));
	}
	xml_serializer->CloseElement(namespace_prefix,
								 CDXLTokens::GetDXLTokenStr(EdxltokenPartCols));

	// Serialize sort columns: <dxl:sortCols> ... </dxl:sortCols>
	xml_serializer->OpenElement(namespace_prefix,
								CDXLTokens::GetDXLTokenStr(EdxltokenSortCols));
	for (ULONG ul = 0; ul < m_pdrgdxlsc->Size(); ul++)
	{
		CDXLScalarSortCol *dxl_sort_col = (*m_pdrgdxlsc)[ul];
		// CDXLScalarSortCol::SerializeToDXL requires a CDXLNode*, but we can pass nullptr
		// if the implementation doesn't use it (common in GPDB 7+)
		dxl_sort_col->SerializeToDXL(xml_serializer, nullptr);
	}
	xml_serializer->CloseElement(namespace_prefix,
								 CDXLTokens::GetDXLTokenStr(EdxltokenSortCols));


	xml_serializer->CloseElement(namespace_prefix, element_name);
}

#ifdef GPOS_DEBUG
//---------------------------------------------------------------------------
//	@function:
//		CDXLPhysicalPartitionTopK::IsValid
//
//	@doc:
//		Checks whether the operator has valid structure
//
//---------------------------------------------------------------------------
BOOL
CDXLPhysicalPartitionTopK::IsValid() const
{
	/* partition columns can be empty (global window, no PARTITION BY) */
	return (0 < m_pdrgdxlsc->Size() && 0 < m_iN);
}
#endif	// GPOS_DEBUG