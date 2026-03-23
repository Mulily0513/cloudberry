//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (c) 2025, HashData Technology Limited.
//
//	@filename:
//		CDXLPhysicalParallelDynamicTableScan.cpp
//
//	@doc:
//		Implementation of DXL physical parallel dynamic table scan operator
//---------------------------------------------------------------------------

#include "naucrates/dxl/operators/CDXLPhysicalParallelDynamicTableScan.h"

#include "naucrates/dxl/CDXLUtils.h"
#include "naucrates/dxl/operators/CDXLNode.h"
#include "naucrates/dxl/xml/CXMLSerializer.h"
#include "naucrates/dxl/xml/dxltokens.h"
#include "naucrates/md/IMDCacheObject.h"

using namespace gpos;
using namespace gpdxl;


CDXLPhysicalParallelDynamicTableScan::CDXLPhysicalParallelDynamicTableScan(
	CMemoryPool *mp, CDXLTableDescr *table_descr, IMdIdArray *part_mdids,
	ULongPtrArray *selector_ids, ULONG ulParallelWorkers)
	: CDXLPhysicalDynamicTableScan(mp, table_descr, part_mdids, selector_ids),
	  m_ulParallelWorkers(ulParallelWorkers)
{
	GPOS_ASSERT(ulParallelWorkers > 0);
}


Edxlopid
CDXLPhysicalParallelDynamicTableScan::GetDXLOperator() const
{
	return EdxlopPhysicalParallelDynamicTableScan;
}


const CWStringConst *
CDXLPhysicalParallelDynamicTableScan::GetOpNameStr() const
{
	return CDXLTokens::GetDXLTokenStr(
		EdxltokenPhysicalParallelDynamicTableScan);
}


void
CDXLPhysicalParallelDynamicTableScan::SerializeToDXL(
	CXMLSerializer *xml_serializer, const CDXLNode *node) const
{
	const CWStringConst *element_name = GetOpNameStr();

	xml_serializer->OpenElement(
		CDXLTokens::GetDXLTokenStr(EdxltokenNamespacePrefix), element_name);

	xml_serializer->AddAttribute(
		CDXLTokens::GetDXLTokenStr(EdxltokenParallelWorkers),
		m_ulParallelWorkers);

	CWStringDynamic *serialized_selector_ids =
		CDXLUtils::Serialize(m_mp, GetSelectorIds());
	xml_serializer->AddAttribute(
		CDXLTokens::GetDXLTokenStr(EdxltokenSelectorIds),
		serialized_selector_ids);
	GPOS_DELETE(serialized_selector_ids);

	node->SerializePropertiesToDXL(xml_serializer);
	node->SerializeChildrenToDXL(xml_serializer);

	IMDCacheObject::SerializeMDIdList(
		xml_serializer, GetParts(),
		CDXLTokens::GetDXLTokenStr(EdxltokenPartitions),
		CDXLTokens::GetDXLTokenStr(EdxltokenPartition));

	GetDXLTableDescr()->SerializeToDXL(xml_serializer);

	xml_serializer->CloseElement(
		CDXLTokens::GetDXLTokenStr(EdxltokenNamespacePrefix), element_name);
}


#ifdef GPOS_DEBUG
void
CDXLPhysicalParallelDynamicTableScan::AssertValid(
	const CDXLNode *node, BOOL	 // validate_children
) const
{
	GPOS_ASSERT(2 == node->Arity());
	GPOS_ASSERT(nullptr != GetDXLTableDescr());
	GPOS_ASSERT(nullptr != GetDXLTableDescr()->MdName());
	GPOS_ASSERT(GetDXLTableDescr()->MdName()->GetMDName()->IsValid());
}
#endif	// GPOS_DEBUG

// EOF
