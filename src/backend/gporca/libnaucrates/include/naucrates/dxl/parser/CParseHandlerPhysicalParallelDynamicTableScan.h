//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (c) 2025, HashData Technology Limited.
//
//	@filename:
//		CParseHandlerPhysicalParallelDynamicTableScan.h
//
//	@doc:
//		SAX parse handler class for parsing parallel dynamic table scan
//		operator nodes
//---------------------------------------------------------------------------

#ifndef GPDXL_CParseHandlerPhysicalParallelDynamicTableScan_H
#define GPDXL_CParseHandlerPhysicalParallelDynamicTableScan_H

#include "gpos/base.h"

#include "naucrates/dxl/parser/CParseHandlerPhysicalOp.h"

namespace gpdxl
{
using namespace gpos;

XERCES_CPP_NAMESPACE_USE

//---------------------------------------------------------------------------
//	@class:
//		CParseHandlerPhysicalParallelDynamicTableScan
//
//	@doc:
//		Parse handler for parsing a parallel dynamic table scan operator
//
//---------------------------------------------------------------------------
class CParseHandlerPhysicalParallelDynamicTableScan
	: public CParseHandlerPhysicalOp
{
private:
	ULongPtrArray *m_selector_ids;
	ULONG m_parallel_workers;

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
	CParseHandlerPhysicalParallelDynamicTableScan(
		const CParseHandlerPhysicalParallelDynamicTableScan &) = delete;

	// ctor
	CParseHandlerPhysicalParallelDynamicTableScan(
		CMemoryPool *mp, CParseHandlerManager *parse_handler_mgr,
		CParseHandlerBase *pph);
};
}  // namespace gpdxl

#endif	// !GPDXL_CParseHandlerPhysicalParallelDynamicTableScan_H

// EOF
