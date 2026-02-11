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
 * CParseHandlerPhysicalParallelWindow.h
 *
 * IDENTIFICATION
 *	  src/backend/gporca/libnaucrates/include/naucrates/dxl/parser/CParseHandlerPhysicalParallelWindow.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef GPDXL_CParseHandlerPhysicalParallelWindow_H
#define GPDXL_CParseHandlerPhysicalParallelWindow_H

#include "gpos/base.h"

#include "naucrates/dxl/operators/CDXLWindowKey.h"
#include "naucrates/dxl/parser/CParseHandlerPhysicalOp.h"


namespace gpdxl
{
using namespace gpos;

XERCES_CPP_NAMESPACE_USE

//---------------------------------------------------------------------------
//	@class:
//		CParseHandlerPhysicalParallelWindow
//
//	@doc:
//		Parse handler for parsing a physical parallel window operator
//
//---------------------------------------------------------------------------
class CParseHandlerPhysicalParallelWindow : public CParseHandlerPhysicalOp
{
private:
	// array of partition columns
	ULongPtrArray *m_part_by_colid_array;

	// number of parallel workers
	ULONG m_parallel_workers;

	// process the start of an element
	void StartElement(
		const XMLCh *const element_uri,
		const XMLCh *const element_local_name,
		const XMLCh *const element_qname,
		const Attributes &attr
		) override;

	// process the end of an element
	void EndElement(
		const XMLCh *const element_uri,
		const XMLCh *const element_local_name,
		const XMLCh *const element_qname
		) override;

public:
	CParseHandlerPhysicalParallelWindow(
		const CParseHandlerPhysicalParallelWindow &) = delete;

	// ctor
	CParseHandlerPhysicalParallelWindow(
		CMemoryPool *mp, CParseHandlerManager *parse_handler_mgr,
		CParseHandlerBase *parse_handler_root);
};
}  // namespace gpdxl

#endif	// !GPDXL_CParseHandlerPhysicalParallelWindow_H

// EOF
