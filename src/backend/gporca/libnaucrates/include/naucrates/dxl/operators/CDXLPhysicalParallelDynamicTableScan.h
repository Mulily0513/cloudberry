//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (c) 2025, HashData Technology Limited.
//
//	@filename:
//		CDXLPhysicalParallelDynamicTableScan.h
//
//	@doc:
//		Class for representing DXL parallel dynamic table scan operators
//---------------------------------------------------------------------------

#ifndef GPDXL_CDXLPhysicalParallelDynamicTableScan_H
#define GPDXL_CDXLPhysicalParallelDynamicTableScan_H

#include "gpos/base.h"

#include "naucrates/dxl/operators/CDXLPhysicalDynamicTableScan.h"

namespace gpdxl
{
//---------------------------------------------------------------------------
//	@class:
//		CDXLPhysicalParallelDynamicTableScan
//
//	@doc:
//		Class for representing DXL parallel dynamic table scan operators
//
//---------------------------------------------------------------------------
class CDXLPhysicalParallelDynamicTableScan
	: public CDXLPhysicalDynamicTableScan
{
private:
	// number of parallel workers
	ULONG m_ulParallelWorkers;

public:
	CDXLPhysicalParallelDynamicTableScan(
		CDXLPhysicalParallelDynamicTableScan &) = delete;

	// ctor
	CDXLPhysicalParallelDynamicTableScan(CMemoryPool *mp,
										 CDXLTableDescr *table_descr,
										 IMdIdArray *part_mdids,
										 ULongPtrArray *selector_ids,
										 ULONG ulParallelWorkers);

	// operator type
	Edxlopid GetDXLOperator() const override;

	// operator name
	const CWStringConst *GetOpNameStr() const override;

	// number of parallel workers
	ULONG
	UlParallelWorkers() const
	{
		return m_ulParallelWorkers;
	}

	// serialize operator in DXL format
	void SerializeToDXL(CXMLSerializer *xml_serializer,
						const CDXLNode *node) const override;

	// conversion function
	static CDXLPhysicalParallelDynamicTableScan *
	Cast(CDXLOperator *dxl_op)
	{
		GPOS_ASSERT(nullptr != dxl_op);
		GPOS_ASSERT(EdxlopPhysicalParallelDynamicTableScan ==
					dxl_op->GetDXLOperator());

		return dynamic_cast<CDXLPhysicalParallelDynamicTableScan *>(dxl_op);
	}

#ifdef GPOS_DEBUG
	// checks whether the operator has valid structure
	void AssertValid(const CDXLNode *, BOOL validate_children) const override;
#endif	// GPOS_DEBUG
};
}  // namespace gpdxl
#endif	// !GPDXL_CDXLPhysicalParallelDynamicTableScan_H

// EOF
