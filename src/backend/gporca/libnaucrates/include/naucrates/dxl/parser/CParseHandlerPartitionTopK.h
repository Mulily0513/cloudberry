//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2026 Hashdata, Inc.
//
//	@filename:
//		CParseHandlerPartitionTopK.h
//
//	@doc:
//		SAX parse handler class for parsing PartitionTopK operator nodes.
//---------------------------------------------------------------------------

#ifndef GPDXL_CParseHandlerPartitionTopK_H
#define GPDXL_CParseHandlerPartitionTopK_H

#include "gpos/base.h"

#include "naucrates/dxl/operators/CDXLPhysicalPartitionTopK.h"
#include "naucrates/dxl/parser/CParseHandlerPhysicalOp.h"

namespace gpdxl
{
using namespace gpos;

XERCES_CPP_NAMESPACE_USE

//---------------------------------------------------------------------------
//	@class:
//		CParseHandlerPartitionTopK
//
//	@doc:
//		Parse handler for PartitionTopK operators
//
//---------------------------------------------------------------------------
class CParseHandlerPartitionTopK : public CParseHandlerPhysicalOp
{
private:
	// parse states for the state machine
	enum EParseState
	{
		eParseInit = 0,
		eParseChildHandlersActive,
		eParsePartCols,
		eParseSortCols,
		eParseDone
	};

	// current parse state
	EParseState m_parse_state;

	// the N (top-K limit) value
	INT m_n;

	// collected partition column IDs
	CDXLColRefArray *m_part_col_refs;

	// collected sort columns
	CDXLSortColArray *m_sort_cols;

	// process the start of an element
	void StartElement(
		const XMLCh *const element_uri,
		const XMLCh *const element_local_name,
		const XMLCh *const element_qname,
		const Attributes &attr) override;

	// process the end of an element
	void EndElement(
		const XMLCh *const element_uri,
		const XMLCh *const element_local_name,
		const XMLCh *const element_qname) override;

public:
	CParseHandlerPartitionTopK(const CParseHandlerPartitionTopK &) = delete;

	// ctor
	CParseHandlerPartitionTopK(CMemoryPool *mp,
							   CParseHandlerManager *parse_handler_mgr,
							   CParseHandlerBase *parse_handler_root);
};
}  // namespace gpdxl

#endif	// !GPDXL_CParseHandlerPartitionTopK_H

// EOF
