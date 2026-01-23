//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2012 EMC Corp.
//
//	@filename:
//		CQueryMutators.h
//
//	@doc:
//		Class providing methods for translating a GPDB Query object into a
//		DXL Tree
//
//	@test:
//
//
//---------------------------------------------------------------------------

#ifndef GPDXL_CQueryReformers_H
#define GPDXL_CQueryReformers_H

#include "gpos/base.h"

#include "gpopt/translate/CMappingVarColId.h"
#include "gpopt/translate/CTranslatorScalarToDXL.h"
#include "gpopt/translate/CTranslatorUtils.h"
#include "naucrates/dxl/operators/CDXLNode.h"
#include "naucrates/md/IMDType.h"

// fwd declarations
namespace gpopt
{
class CMDAccessor;
}

struct Query;
struct RangeTblEntry;
struct Const;
struct List;


namespace gpdxl
{
class CQueryReformers
{
private:
	using MutatorWalkerFn = Node *(*)(Node*, void*);
	using ExprWalkerFn = BOOL (*)(Node*, void*);
	constexpr static int SINGLE_QUAL_ID = 0;

	// gather same spj subqueries into cte
	static Query *ReformSubQueries(CMemoryPool *mp, CMDAccessor *md_accessor,
								   Query *query, ULONG query_level,
								   BOOL *free_old);

	// ReformSubQueries related helper function begin
	static Query *ExtractCteQuery(CMemoryPool *mp, CMDAccessor *md_accessor,
								  Query *query, List *subqueryrefs,
								  Bitmapset *diffquals, int id);

	static List *StripCteQueryTlist(Query *query);

	static List *StripCteQueryQual(Query *query, Bitmapset *diffquals);

	static List *GetSubQueryCandidates(Query *query, ULONG query_level);

	static RangeTblEntry *RTRefLC2RTE(Query *query, ListCell *lc);

	static List *GetSubqueryRelids(Query *query, List *refs, List **pRelids);

	static List *GroupedSameRelidsSubquery(List *subqrefs, List *sq_relids);

	static List *GroupedMatchSubquery(Query *query, List *comm_subq_refs, List **pDiffQuals);

	static bool ExprSimilar(Node *lhs, Node *rhs);

	static void ConjunctiveSubQueryJoinQual(Query *query, List *comm_subq_refs);

	static Node *FixUpperExprByTlist(Node *node, List *tlist);

	struct CollectVarContext
	{
		Query *lowerquery;
		List *vars;
	};

	static BOOL CollectExprVar(Node *node, CollectVarContext *ctx);
	// ReformSubQueries related helper function end
public:
	static Query *ReformQuery(CMemoryPool *mp, CMDAccessor *md_accessor,
							  Query *query, ULONG query_level);
};
}  // namespace gpdxl
#endif //GPDXL_CQueryReformers_H

// EOF
