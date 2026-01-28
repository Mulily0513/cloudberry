//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2026 Hashdata, Inc.
//
//	@filename:
//		CXformImplementPartitionTopK.h
//
//	@doc:
//		Transform logical PartitionTopK to physical PartitionTopK
//
//---------------------------------------------------------------------------

#ifndef GPOPT_CXformImplementPartitionTopK_H
#define GPOPT_CXformImplementPartitionTopK_H

#include "gpos/base.h"
#include "gpopt/xforms/CXformImplementation.h"

namespace gpopt
{
	using namespace gpos;

	//---------------------------------------------------------------------------
	//	@class:
	//		CXformImplementPartitionTopK
	//
	//	@doc:
	//		Implementation of logical PartitionTopK operator
	//
	//---------------------------------------------------------------------------
	class CXformImplementPartitionTopK : public CXformImplementation
	{

		private:

			// private copy ctor
			CXformImplementPartitionTopK(const CXformImplementPartitionTopK &);

		public:

			// ctor
			explicit
			CXformImplementPartitionTopK(CMemoryPool *mp);

			// dtor
			virtual
			~CXformImplementPartitionTopK() {}

			// ident accessors
			virtual
			EXformId Exfid() const
			{
				return ExfImplementPartitionTopK;
			}

			virtual
			const CHAR *SzId() const
			{
				return "CXformImplementPartitionTopK";
			}

			// compute xform promise for a given expression handle
			virtual
			CXform::EXformPromise Exfp(CExpressionHandle &exprhdl) const override;

			// actual transform
			virtual
			void Transform(CXformContext *pxfctxt, CXformResult *pxfres, CExpression *pexpr) const;

	}; // class CXformImplementPartitionTopK

} // namespace gpopt

#endif // !GPOPT_CXformImplementPartitionTopK_H

// EOF