//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2026 Hashdata, Inc.
//
//	@filename:
//		CDXLPhysicalPartitionTopK.h
//
//	@doc:
//		Class definition for DXL physical PartitionTopK operator (GPDB 7 compatible)
//---------------------------------------------------------------------------

#ifndef GPDXL_CDXLPhysicalPartitionTopK_H
#define GPDXL_CDXLPhysicalPartitionTopK_H

#include "gpos/base.h"
#include "gpos/common/CDynamicPtrArray.h"  // For CDynamicPtrArray

#include "naucrates/dxl/operators/CDXLColRef.h"	 // CDXLColRefArray
#include "naucrates/dxl/operators/CDXLPhysical.h"
#include "naucrates/dxl/operators/CDXLScalarSortCol.h"	// CDXLScalarSortCol exists

namespace gpdxl
{
// Forward declarations
class CXMLSerializer;
class CDXLNode;

// Define array type with proper cleanup function
typedef CDynamicPtrArray<CDXLScalarSortCol, gpos::CleanupRelease>
	CDXLSortColArray;

class CDXLPhysicalPartitionTopK : public CDXLPhysical
{
private:
	CDXLColRefArray *m_pdrgdxlcrPart;
	CDXLSortColArray *m_pdrgdxlsc;	// Now properly defined
	INT m_iN;

	// Private copy ctor
	CDXLPhysicalPartitionTopK(const CDXLPhysicalPartitionTopK &);

public:
	// Ctor
	CDXLPhysicalPartitionTopK(CMemoryPool *mp, CDXLColRefArray *pdrgdxlcrPart,
							  CDXLSortColArray *pdrgdxlsc, INT n);

	// Dtor
	virtual ~CDXLPhysicalPartitionTopK() override;

	static CDXLPhysicalPartitionTopK *Cast(CDXLOperator *op);

	// Get operator type
	virtual Edxlopid GetDXLOperator() const override;

	// Get operator name
	virtual const CWStringConst *GetOpNameStr() const override;

	// Serialize operator in DXL format
	virtual void SerializeToDXL(CXMLSerializer *xml_serializer,
								const CDXLNode *dxlnode) const override;

#ifdef GPOS_DEBUG
	// Checks whether the operator has valid structure
	virtual BOOL IsValid() const;
#endif	// GPOS_DEBUG

	// Accessors
	const CDXLColRefArray *
	PdrgdxlcrPart() const
	{
		return m_pdrgdxlcrPart;
	}
	const CDXLSortColArray *
	Pdrgdxlsc() const
	{
		return m_pdrgdxlsc;
	}
	INT
	GetN() const
	{
		return m_iN;
	}
};
}  // namespace gpdxl

#endif	// !GPDXL_CDXLPhysicalPartitionTopK_H