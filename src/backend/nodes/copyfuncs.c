/*-------------------------------------------------------------------------
 *
 * copyfuncs.c
 *	  Copy functions for Postgres tree nodes.
 *
 * NOTE: a general convention when copying or comparing plan nodes is
 * that we ignore the executor state subnode.  We do not need to look
 * at it because no current uses of copyObject() or equal() need to
 * deal with already-executing plan trees.	By leaving the state subnodes
 * out, we avoid needing to write copy/compare routines for all the
 * different executor state node types.
 *
 *
 * Portions Copyright (c) 1996-2002, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  $Header: /cvsroot/pgsql/src/backend/nodes/copyfuncs.c,v 1.224 2002/11/30 00:08:16 tgl Exp $
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "optimizer/clauses.h"
#include "optimizer/planmain.h"
#include "utils/datum.h"


/*
 * Macros to simplify copying of different kinds of fields.  Use these
 * wherever possible to reduce the chance for silly typos.  Note that these
 * hard-wire the convention that the local variables in a Copy routine are
 * named 'newnode' and 'from'.
 */

/* Copy a simple scalar field (int, float, bool, enum, etc) */
#define COPY_SCALAR_FIELD(fldname) \
	(newnode->fldname = from->fldname)

/* Copy a field that is a pointer to some kind of Node or Node tree */
#define COPY_NODE_FIELD(fldname) \
	(newnode->fldname = copyObject(from->fldname))

/* Copy a field that is a pointer to a list of integers */
#define COPY_INTLIST_FIELD(fldname) \
	(newnode->fldname = listCopy(from->fldname))

/* Copy a field that is a pointer to a C string, or perhaps NULL */
#define COPY_STRING_FIELD(fldname) \
	(newnode->fldname = from->fldname ? pstrdup(from->fldname) : (char *) NULL)

/* Copy a field that is a pointer to a simple palloc'd object of size sz */
#define COPY_POINTER_FIELD(fldname, sz) \
	do { \
		Size	_size = (sz); \
		newnode->fldname = palloc(_size); \
		memcpy(newnode->fldname, from->fldname, _size); \
	} while (0)

/* Special hack for fixing subplan lists of Plan nodes (ick) */
#define FIX_SUBPLAN_LINKS(subplanfldname, fldname) \
	do { \
		if (from->subplanfldname != NIL) \
			newnode->subplanfldname = \
				nconc(newnode->subplanfldname, \
					  pull_subplans((Node *) (newnode->fldname))); \
	} while (0)


/*
 * listCopy
 *	  This copy function only copies the "cons-cells" of the list, not the
 *	  pointed-to objects.  (Use copyObject if you want a "deep" copy.)
 *
 *	  We also use this function for copying lists of integers, which is
 *	  grotty but unlikely to break --- it could fail if sizeof(pointer)
 *	  is less than sizeof(int), but I don't know any such machines...
 *
 *	  Note that copyObject will surely coredump if applied to a list
 *	  of integers!
 */
List *
listCopy(List *list)
{
	List	   *newlist,
			   *l,
			   *nl;

	/* rather ugly coding for speed... */
	if (list == NIL)
		return NIL;

	newlist = nl = makeList1(lfirst(list));

	foreach(l, lnext(list))
	{
		lnext(nl) = makeList1(lfirst(l));
		nl = lnext(nl);
	}
	return newlist;
}

/* ****************************************************************
 *					 plannodes.h copy functions
 * ****************************************************************
 */

/*
 * CopyPlanFields
 *
 *		This function copies the fields of the Plan node.  It is used by
 *		all the copy functions for classes which inherit from Plan.
 */
static void
CopyPlanFields(Plan *from, Plan *newnode)
{
	COPY_SCALAR_FIELD(startup_cost);
	COPY_SCALAR_FIELD(total_cost);
	COPY_SCALAR_FIELD(plan_rows);
	COPY_SCALAR_FIELD(plan_width);
	/* execution state is NOT copied */
	COPY_NODE_FIELD(targetlist);
	COPY_NODE_FIELD(qual);
	COPY_NODE_FIELD(lefttree);
	COPY_NODE_FIELD(righttree);
	COPY_INTLIST_FIELD(extParam);
	COPY_INTLIST_FIELD(locParam);
	COPY_INTLIST_FIELD(chgParam);
	COPY_NODE_FIELD(initPlan);
	/* subPlan list must point to subplans in the new subtree, not the old */
	newnode->subPlan = NIL;
	FIX_SUBPLAN_LINKS(subPlan, targetlist);
	FIX_SUBPLAN_LINKS(subPlan, qual);
	COPY_SCALAR_FIELD(nParamExec);
}

/*
 * _copyPlan
 */
static Plan *
_copyPlan(Plan *from)
{
	Plan	   *newnode = makeNode(Plan);

	/*
	 * copy node superclass fields
	 */
	CopyPlanFields(from, newnode);

	return newnode;
}


/*
 * _copyResult
 */
static Result *
_copyResult(Result *from)
{
	Result	   *newnode = makeNode(Result);

	/*
	 * copy node superclass fields
	 */
	CopyPlanFields((Plan *) from, (Plan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_NODE_FIELD(resconstantqual);

	/* subPlan list must point to subplans in the new subtree, not the old */
	FIX_SUBPLAN_LINKS(plan.subPlan, resconstantqual);

	return newnode;
}

/*
 * _copyAppend
 */
static Append *
_copyAppend(Append *from)
{
	Append	   *newnode = makeNode(Append);

	/*
	 * copy node superclass fields
	 */
	CopyPlanFields((Plan *) from, (Plan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_NODE_FIELD(appendplans);
	COPY_SCALAR_FIELD(isTarget);

	return newnode;
}


/*
 * CopyScanFields
 *
 *		This function copies the fields of the Scan node.  It is used by
 *		all the copy functions for classes which inherit from Scan.
 */
static void
CopyScanFields(Scan *from, Scan *newnode)
{
	CopyPlanFields((Plan *) from, (Plan *) newnode);

	COPY_SCALAR_FIELD(scanrelid);
}

/*
 * _copyScan
 */
static Scan *
_copyScan(Scan *from)
{
	Scan	   *newnode = makeNode(Scan);

	/*
	 * copy node superclass fields
	 */
	CopyScanFields((Scan *) from, (Scan *) newnode);

	return newnode;
}

/*
 * _copySeqScan
 */
static SeqScan *
_copySeqScan(SeqScan *from)
{
	SeqScan    *newnode = makeNode(SeqScan);

	/*
	 * copy node superclass fields
	 */
	CopyScanFields((Scan *) from, (Scan *) newnode);

	return newnode;
}

/*
 * _copyIndexScan
 */
static IndexScan *
_copyIndexScan(IndexScan *from)
{
	IndexScan  *newnode = makeNode(IndexScan);

	/*
	 * copy node superclass fields
	 */
	CopyScanFields((Scan *) from, (Scan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_INTLIST_FIELD(indxid);
	COPY_NODE_FIELD(indxqual);
	COPY_NODE_FIELD(indxqualorig);
	COPY_SCALAR_FIELD(indxorderdir);

	/* subPlan list must point to subplans in the new subtree, not the old */
	FIX_SUBPLAN_LINKS(scan.plan.subPlan, indxqual);
	FIX_SUBPLAN_LINKS(scan.plan.subPlan, indxqualorig);

	return newnode;
}

/*
 * _copyTidScan
 */
static TidScan *
_copyTidScan(TidScan *from)
{
	TidScan    *newnode = makeNode(TidScan);

	/*
	 * copy node superclass fields
	 */
	CopyScanFields((Scan *) from, (Scan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_SCALAR_FIELD(needRescan);
	COPY_NODE_FIELD(tideval);

	/* subPlan list must point to subplans in the new subtree, not the old */
	FIX_SUBPLAN_LINKS(scan.plan.subPlan, tideval);

	return newnode;
}

/*
 * _copySubqueryScan
 */
static SubqueryScan *
_copySubqueryScan(SubqueryScan *from)
{
	SubqueryScan *newnode = makeNode(SubqueryScan);

	/*
	 * copy node superclass fields
	 */
	CopyScanFields((Scan *) from, (Scan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_NODE_FIELD(subplan);

	return newnode;
}

/*
 * _copyFunctionScan
 */
static FunctionScan *
_copyFunctionScan(FunctionScan *from)
{
	FunctionScan *newnode = makeNode(FunctionScan);

	/*
	 * copy node superclass fields
	 */
	CopyScanFields((Scan *) from, (Scan *) newnode);

	return newnode;
}

/*
 * CopyJoinFields
 *
 *		This function copies the fields of the Join node.  It is used by
 *		all the copy functions for classes which inherit from Join.
 */
static void
CopyJoinFields(Join *from, Join *newnode)
{
	CopyPlanFields((Plan *) from, (Plan *) newnode);

	COPY_SCALAR_FIELD(jointype);
	COPY_NODE_FIELD(joinqual);

	/* subPlan list must point to subplans in the new subtree, not the old */
	FIX_SUBPLAN_LINKS(plan.subPlan, joinqual);
}


/*
 * _copyJoin
 */
static Join *
_copyJoin(Join *from)
{
	Join	   *newnode = makeNode(Join);

	/*
	 * copy node superclass fields
	 */
	CopyJoinFields(from, newnode);

	return newnode;
}


/*
 * _copyNestLoop
 */
static NestLoop *
_copyNestLoop(NestLoop *from)
{
	NestLoop   *newnode = makeNode(NestLoop);

	/*
	 * copy node superclass fields
	 */
	CopyJoinFields((Join *) from, (Join *) newnode);

	return newnode;
}


/*
 * _copyMergeJoin
 */
static MergeJoin *
_copyMergeJoin(MergeJoin *from)
{
	MergeJoin  *newnode = makeNode(MergeJoin);

	/*
	 * copy node superclass fields
	 */
	CopyJoinFields((Join *) from, (Join *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_NODE_FIELD(mergeclauses);

	/* subPlan list must point to subplans in the new subtree, not the old */
	FIX_SUBPLAN_LINKS(join.plan.subPlan, mergeclauses);

	return newnode;
}

/*
 * _copyHashJoin
 */
static HashJoin *
_copyHashJoin(HashJoin *from)
{
	HashJoin   *newnode = makeNode(HashJoin);

	/*
	 * copy node superclass fields
	 */
	CopyJoinFields((Join *) from, (Join *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_NODE_FIELD(hashclauses);

	/* subPlan list must point to subplans in the new subtree, not the old */
	FIX_SUBPLAN_LINKS(join.plan.subPlan, hashclauses);

	return newnode;
}


/*
 * _copyMaterial
 */
static Material *
_copyMaterial(Material *from)
{
	Material   *newnode = makeNode(Material);

	/*
	 * copy node superclass fields
	 */
	CopyPlanFields((Plan *) from, (Plan *) newnode);

	return newnode;
}


/*
 * _copySort
 */
static Sort *
_copySort(Sort *from)
{
	Sort	   *newnode = makeNode(Sort);

	/*
	 * copy node superclass fields
	 */
	CopyPlanFields((Plan *) from, (Plan *) newnode);

	COPY_SCALAR_FIELD(keycount);

	return newnode;
}


/*
 * _copyGroup
 */
static Group *
_copyGroup(Group *from)
{
	Group	   *newnode = makeNode(Group);

	CopyPlanFields((Plan *) from, (Plan *) newnode);

	COPY_SCALAR_FIELD(numCols);
	COPY_POINTER_FIELD(grpColIdx, from->numCols * sizeof(AttrNumber));

	return newnode;
}

/*
 * _copyAgg
 */
static Agg *
_copyAgg(Agg *from)
{
	Agg		   *newnode = makeNode(Agg);

	CopyPlanFields((Plan *) from, (Plan *) newnode);

	COPY_SCALAR_FIELD(aggstrategy);
	COPY_SCALAR_FIELD(numCols);
	if (from->numCols > 0)
		COPY_POINTER_FIELD(grpColIdx, from->numCols * sizeof(AttrNumber));
	COPY_SCALAR_FIELD(numGroups);

	return newnode;
}

/*
 * _copyUnique
 */
static Unique *
_copyUnique(Unique *from)
{
	Unique	   *newnode = makeNode(Unique);

	/*
	 * copy node superclass fields
	 */
	CopyPlanFields((Plan *) from, (Plan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_SCALAR_FIELD(numCols);
	COPY_POINTER_FIELD(uniqColIdx, from->numCols * sizeof(AttrNumber));

	return newnode;
}

/*
 * _copySetOp
 */
static SetOp *
_copySetOp(SetOp *from)
{
	SetOp	   *newnode = makeNode(SetOp);

	/*
	 * copy node superclass fields
	 */
	CopyPlanFields((Plan *) from, (Plan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_SCALAR_FIELD(cmd);
	COPY_SCALAR_FIELD(numCols);
	COPY_POINTER_FIELD(dupColIdx, from->numCols * sizeof(AttrNumber));
	COPY_SCALAR_FIELD(flagColIdx);

	return newnode;
}

/*
 * _copyLimit
 */
static Limit *
_copyLimit(Limit *from)
{
	Limit	   *newnode = makeNode(Limit);

	/*
	 * copy node superclass fields
	 */
	CopyPlanFields((Plan *) from, (Plan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_NODE_FIELD(limitOffset);
	COPY_NODE_FIELD(limitCount);

	return newnode;
}

/*
 * _copyHash
 */
static Hash *
_copyHash(Hash *from)
{
	Hash	   *newnode = makeNode(Hash);

	/*
	 * copy node superclass fields
	 */
	CopyPlanFields((Plan *) from, (Plan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_NODE_FIELD(hashkeys);

	/* XXX could the hashkeys contain subplans?  Not at present... */

	return newnode;
}

static SubPlan *
_copySubPlan(SubPlan *from)
{
	SubPlan    *newnode = makeNode(SubPlan);

	COPY_NODE_FIELD(plan);
	COPY_SCALAR_FIELD(plan_id);
	COPY_NODE_FIELD(rtable);
	COPY_INTLIST_FIELD(setParam);
	COPY_INTLIST_FIELD(parParam);
	COPY_NODE_FIELD(sublink);

	/* do not copy execution state */
	newnode->needShutdown = false;
	newnode->curTuple = NULL;

	return newnode;
}

/* ****************************************************************
 *					   primnodes.h copy functions
 * ****************************************************************
 */

/*
 * _copyResdom
 */
static Resdom *
_copyResdom(Resdom *from)
{
	Resdom	   *newnode = makeNode(Resdom);

	COPY_SCALAR_FIELD(resno);
	COPY_SCALAR_FIELD(restype);
	COPY_SCALAR_FIELD(restypmod);
	COPY_STRING_FIELD(resname);
	COPY_SCALAR_FIELD(ressortgroupref);
	COPY_SCALAR_FIELD(reskey);
	COPY_SCALAR_FIELD(reskeyop);
	COPY_SCALAR_FIELD(resjunk);

	return newnode;
}

static Fjoin *
_copyFjoin(Fjoin *from)
{
	Fjoin	   *newnode = makeNode(Fjoin);

	COPY_SCALAR_FIELD(fj_initialized);
	COPY_SCALAR_FIELD(fj_nNodes);
	COPY_NODE_FIELD(fj_innerNode);
	COPY_POINTER_FIELD(fj_results, from->fj_nNodes * sizeof(Datum));
	COPY_POINTER_FIELD(fj_alwaysDone, from->fj_nNodes * sizeof(bool));

	return newnode;
}

static Alias *
_copyAlias(Alias *from)
{
	Alias	   *newnode = makeNode(Alias);

	COPY_STRING_FIELD(aliasname);
	COPY_NODE_FIELD(colnames);

	return newnode;
}

static RangeVar *
_copyRangeVar(RangeVar *from)
{
	RangeVar   *newnode = makeNode(RangeVar);

	COPY_STRING_FIELD(catalogname);
	COPY_STRING_FIELD(schemaname);
	COPY_STRING_FIELD(relname);
	COPY_SCALAR_FIELD(inhOpt);
	COPY_SCALAR_FIELD(istemp);
	COPY_NODE_FIELD(alias);

	return newnode;
}

/*
 * _copyExpr
 */
static Expr *
_copyExpr(Expr *from)
{
	Expr	   *newnode = makeNode(Expr);

	COPY_SCALAR_FIELD(typeOid);
	COPY_SCALAR_FIELD(opType);
	COPY_NODE_FIELD(oper);
	COPY_NODE_FIELD(args);

	return newnode;
}

/*
 * _copyVar
 */
static Var *
_copyVar(Var *from)
{
	Var		   *newnode = makeNode(Var);

	COPY_SCALAR_FIELD(varno);
	COPY_SCALAR_FIELD(varattno);
	COPY_SCALAR_FIELD(vartype);
	COPY_SCALAR_FIELD(vartypmod);
	COPY_SCALAR_FIELD(varlevelsup);
	COPY_SCALAR_FIELD(varnoold);
	COPY_SCALAR_FIELD(varoattno);

	return newnode;
}

/*
 * _copyOper
 */
static Oper *
_copyOper(Oper *from)
{
	Oper	   *newnode = makeNode(Oper);

	COPY_SCALAR_FIELD(opno);
	COPY_SCALAR_FIELD(opid);
	COPY_SCALAR_FIELD(opresulttype);
	COPY_SCALAR_FIELD(opretset);

	/* Do not copy the run-time state, if any */
	newnode->op_fcache = NULL;

	return newnode;
}

/*
 * _copyConst
 */
static Const *
_copyConst(Const *from)
{
	Const	   *newnode = makeNode(Const);

	COPY_SCALAR_FIELD(consttype);
	COPY_SCALAR_FIELD(constlen);

	if (from->constbyval || from->constisnull)
	{
		/*
		 * passed by value so just copy the datum. Also, don't try to copy
		 * struct when value is null!
		 */
		newnode->constvalue = from->constvalue;
	}
	else
	{
		/*
		 * passed by reference.  We need a palloc'd copy.
		 */
		newnode->constvalue = datumCopy(from->constvalue,
										from->constbyval,
										from->constlen);
	}

	COPY_SCALAR_FIELD(constisnull);
	COPY_SCALAR_FIELD(constbyval);

	return newnode;
}

/*
 * _copyParam
 */
static Param *
_copyParam(Param *from)
{
	Param	   *newnode = makeNode(Param);

	COPY_SCALAR_FIELD(paramkind);
	COPY_SCALAR_FIELD(paramid);
	COPY_STRING_FIELD(paramname);
	COPY_SCALAR_FIELD(paramtype);

	return newnode;
}

/*
 * _copyFunc
 */
static Func *
_copyFunc(Func *from)
{
	Func	   *newnode = makeNode(Func);

	COPY_SCALAR_FIELD(funcid);
	COPY_SCALAR_FIELD(funcresulttype);
	COPY_SCALAR_FIELD(funcretset);
	COPY_SCALAR_FIELD(funcformat);

	/* Do not copy the run-time state, if any */
	newnode->func_fcache = NULL;

	return newnode;
}

/*
 * _copyAggref
 */
static Aggref *
_copyAggref(Aggref *from)
{
	Aggref	   *newnode = makeNode(Aggref);

	COPY_SCALAR_FIELD(aggfnoid);
	COPY_SCALAR_FIELD(aggtype);
	COPY_NODE_FIELD(target);
	COPY_SCALAR_FIELD(aggstar);
	COPY_SCALAR_FIELD(aggdistinct);
	COPY_SCALAR_FIELD(aggno);	/* probably not necessary */

	return newnode;
}

/*
 * _copySubLink
 */
static SubLink *
_copySubLink(SubLink *from)
{
	SubLink    *newnode = makeNode(SubLink);

	COPY_SCALAR_FIELD(subLinkType);
	COPY_SCALAR_FIELD(useor);
	COPY_NODE_FIELD(lefthand);
	COPY_NODE_FIELD(oper);
	COPY_NODE_FIELD(subselect);

	return newnode;
}

/*
 * _copyFieldSelect
 */
static FieldSelect *
_copyFieldSelect(FieldSelect *from)
{
	FieldSelect *newnode = makeNode(FieldSelect);

	COPY_NODE_FIELD(arg);
	COPY_SCALAR_FIELD(fieldnum);
	COPY_SCALAR_FIELD(resulttype);
	COPY_SCALAR_FIELD(resulttypmod);

	return newnode;
}

/*
 * _copyRelabelType
 */
static RelabelType *
_copyRelabelType(RelabelType *from)
{
	RelabelType *newnode = makeNode(RelabelType);

	COPY_NODE_FIELD(arg);
	COPY_SCALAR_FIELD(resulttype);
	COPY_SCALAR_FIELD(resulttypmod);
	COPY_SCALAR_FIELD(relabelformat);

	return newnode;
}

static RangeTblRef *
_copyRangeTblRef(RangeTblRef *from)
{
	RangeTblRef *newnode = makeNode(RangeTblRef);

	COPY_SCALAR_FIELD(rtindex);

	return newnode;
}

static JoinExpr *
_copyJoinExpr(JoinExpr *from)
{
	JoinExpr   *newnode = makeNode(JoinExpr);

	COPY_SCALAR_FIELD(jointype);
	COPY_SCALAR_FIELD(isNatural);
	COPY_NODE_FIELD(larg);
	COPY_NODE_FIELD(rarg);
	COPY_NODE_FIELD(using);
	COPY_NODE_FIELD(quals);
	COPY_NODE_FIELD(alias);
	COPY_SCALAR_FIELD(rtindex);

	return newnode;
}

static FromExpr *
_copyFromExpr(FromExpr *from)
{
	FromExpr   *newnode = makeNode(FromExpr);

	COPY_NODE_FIELD(fromlist);
	COPY_NODE_FIELD(quals);

	return newnode;
}

static ArrayRef *
_copyArrayRef(ArrayRef *from)
{
	ArrayRef   *newnode = makeNode(ArrayRef);

	COPY_SCALAR_FIELD(refrestype);
	COPY_SCALAR_FIELD(refattrlength);
	COPY_SCALAR_FIELD(refelemlength);
	COPY_SCALAR_FIELD(refelembyval);
	COPY_SCALAR_FIELD(refelemalign);
	COPY_NODE_FIELD(refupperindexpr);
	COPY_NODE_FIELD(reflowerindexpr);
	COPY_NODE_FIELD(refexpr);
	COPY_NODE_FIELD(refassgnexpr);

	return newnode;
}

/* ****************************************************************
 *						relation.h copy functions
 *
 * XXX the code to copy RelOptInfo and Path nodes is really completely bogus,
 * because it makes no attempt to deal with multiple links from RelOptInfo
 * to Paths, nor with back-links from Paths to their parent RelOptInfo.
 * Currently, since we never actually try to copy a RelOptInfo, this is okay.
 * ****************************************************************
 */

/*
 * _copyRelOptInfo
 */
static RelOptInfo *
_copyRelOptInfo(RelOptInfo *from)
{
	RelOptInfo *newnode = makeNode(RelOptInfo);

	COPY_SCALAR_FIELD(reloptkind);
	COPY_INTLIST_FIELD(relids);
	COPY_SCALAR_FIELD(rows);
	COPY_SCALAR_FIELD(width);
	COPY_NODE_FIELD(targetlist);
	COPY_NODE_FIELD(pathlist);
	/* XXX cheapest-path fields should point to members of pathlist? */
	COPY_NODE_FIELD(cheapest_startup_path);
	COPY_NODE_FIELD(cheapest_total_path);
	COPY_SCALAR_FIELD(pruneable);
	COPY_SCALAR_FIELD(rtekind);
	COPY_NODE_FIELD(indexlist);
	COPY_SCALAR_FIELD(pages);
	COPY_SCALAR_FIELD(tuples);
	COPY_NODE_FIELD(subplan);
	COPY_SCALAR_FIELD(joinrti);
	COPY_INTLIST_FIELD(joinrteids);
	COPY_NODE_FIELD(baserestrictinfo);
	COPY_SCALAR_FIELD(baserestrictcost);
	COPY_INTLIST_FIELD(outerjoinset);
	COPY_NODE_FIELD(joininfo);
	COPY_INTLIST_FIELD(index_outer_relids);
	COPY_NODE_FIELD(index_inner_paths);

	return newnode;
}

/*
 * _copyIndexOptInfo
 */
static IndexOptInfo *
_copyIndexOptInfo(IndexOptInfo *from)
{
	IndexOptInfo *newnode = makeNode(IndexOptInfo);

	COPY_SCALAR_FIELD(indexoid);
	COPY_SCALAR_FIELD(pages);
	COPY_SCALAR_FIELD(tuples);
	COPY_SCALAR_FIELD(ncolumns);
	COPY_SCALAR_FIELD(nkeys);

	if (from->classlist)
	{
		/* copy the trailing zero too */
		COPY_POINTER_FIELD(classlist, (from->ncolumns + 1) * sizeof(Oid));
	}

	if (from->indexkeys)
	{
		/* copy the trailing zero too */
		COPY_POINTER_FIELD(indexkeys, (from->nkeys + 1) * sizeof(int));
	}

	if (from->ordering)
	{
		/* copy the trailing zero too */
		COPY_POINTER_FIELD(ordering, (from->ncolumns + 1) * sizeof(Oid));
	}

	COPY_SCALAR_FIELD(relam);
	COPY_SCALAR_FIELD(amcostestimate);
	COPY_SCALAR_FIELD(indproc);
	COPY_NODE_FIELD(indpred);
	COPY_SCALAR_FIELD(unique);
	COPY_INTLIST_FIELD(outer_relids);
	COPY_NODE_FIELD(inner_paths);

	return newnode;
}

/*
 * CopyPathFields
 *
 *		This function copies the fields of the Path node.  It is used by
 *		all the copy functions for classes which inherit from Path.
 */
static void
CopyPathFields(Path *from, Path *newnode)
{
	/*
	 * Modify the next line, since it causes the copying to cycle (i.e.
	 * the parent points right back here! -- JMH, 7/7/92. Old version:
	 * COPY_NODE_FIELD(parent);
	 */
	COPY_SCALAR_FIELD(parent);

	COPY_SCALAR_FIELD(startup_cost);
	COPY_SCALAR_FIELD(total_cost);
	COPY_SCALAR_FIELD(pathtype);
	COPY_NODE_FIELD(pathkeys);
}

/*
 * _copyPath
 */
static Path *
_copyPath(Path *from)
{
	Path	   *newnode = makeNode(Path);

	CopyPathFields(from, newnode);

	return newnode;
}

/*
 * _copyIndexPath
 */
static IndexPath *
_copyIndexPath(IndexPath *from)
{
	IndexPath  *newnode = makeNode(IndexPath);

	/*
	 * copy node superclass fields
	 */
	CopyPathFields((Path *) from, (Path *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_NODE_FIELD(indexinfo);
	COPY_NODE_FIELD(indexqual);
	COPY_SCALAR_FIELD(indexscandir);
	COPY_SCALAR_FIELD(rows);

	return newnode;
}

/*
 * _copyTidPath
 */
static TidPath *
_copyTidPath(TidPath *from)
{
	TidPath    *newnode = makeNode(TidPath);

	/*
	 * copy node superclass fields
	 */
	CopyPathFields((Path *) from, (Path *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_NODE_FIELD(tideval);
	COPY_INTLIST_FIELD(unjoined_relids);

	return newnode;
}

/*
 * _copyAppendPath
 */
static AppendPath *
_copyAppendPath(AppendPath *from)
{
	AppendPath *newnode = makeNode(AppendPath);

	/*
	 * copy node superclass fields
	 */
	CopyPathFields((Path *) from, (Path *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_NODE_FIELD(subpaths);

	return newnode;
}

/*
 * _copyResultPath
 */
static ResultPath *
_copyResultPath(ResultPath *from)
{
	ResultPath    *newnode = makeNode(ResultPath);

	/*
	 * copy node superclass fields
	 */
	CopyPathFields((Path *) from, (Path *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_NODE_FIELD(subpath);
	COPY_NODE_FIELD(constantqual);

	return newnode;
}

/*
 * CopyJoinPathFields
 *
 *		This function copies the fields of the JoinPath node.  It is used by
 *		all the copy functions for classes which inherit from JoinPath.
 */
static void
CopyJoinPathFields(JoinPath *from, JoinPath *newnode)
{
	CopyPathFields((Path *) from, (Path *) newnode);

	COPY_SCALAR_FIELD(jointype);
	COPY_NODE_FIELD(outerjoinpath);
	COPY_NODE_FIELD(innerjoinpath);
	COPY_NODE_FIELD(joinrestrictinfo);
}

/*
 * _copyNestPath
 */
static NestPath *
_copyNestPath(NestPath *from)
{
	NestPath   *newnode = makeNode(NestPath);

	/*
	 * copy node superclass fields
	 */
	CopyJoinPathFields((JoinPath *) from, (JoinPath *) newnode);

	return newnode;
}

/*
 * _copyMergePath
 */
static MergePath *
_copyMergePath(MergePath *from)
{
	MergePath  *newnode = makeNode(MergePath);

	/*
	 * copy node superclass fields
	 */
	CopyJoinPathFields((JoinPath *) from, (JoinPath *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_NODE_FIELD(path_mergeclauses);
	COPY_NODE_FIELD(outersortkeys);
	COPY_NODE_FIELD(innersortkeys);

	return newnode;
}

/*
 * _copyHashPath
 */
static HashPath *
_copyHashPath(HashPath *from)
{
	HashPath   *newnode = makeNode(HashPath);

	/*
	 * copy node superclass fields
	 */
	CopyJoinPathFields((JoinPath *) from, (JoinPath *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_NODE_FIELD(path_hashclauses);

	return newnode;
}

/*
 * _copyPathKeyItem
 */
static PathKeyItem *
_copyPathKeyItem(PathKeyItem *from)
{
	PathKeyItem *newnode = makeNode(PathKeyItem);

	COPY_NODE_FIELD(key);
	COPY_SCALAR_FIELD(sortop);

	return newnode;
}

/*
 * _copyRestrictInfo
 */
static RestrictInfo *
_copyRestrictInfo(RestrictInfo *from)
{
	RestrictInfo *newnode = makeNode(RestrictInfo);

	COPY_NODE_FIELD(clause);
	COPY_SCALAR_FIELD(ispusheddown);
	COPY_NODE_FIELD(subclauseindices); /* XXX probably bad */
	COPY_SCALAR_FIELD(eval_cost);
	COPY_SCALAR_FIELD(this_selec);
	COPY_SCALAR_FIELD(mergejoinoperator);
	COPY_SCALAR_FIELD(left_sortop);
	COPY_SCALAR_FIELD(right_sortop);

	/*
	 * Do not copy pathkeys, since they'd not be canonical in a copied
	 * query
	 */
	newnode->left_pathkey = NIL;
	newnode->right_pathkey = NIL;

	COPY_SCALAR_FIELD(left_mergescansel);
	COPY_SCALAR_FIELD(right_mergescansel);
	COPY_SCALAR_FIELD(hashjoinoperator);
	COPY_SCALAR_FIELD(left_bucketsize);
	COPY_SCALAR_FIELD(right_bucketsize);

	return newnode;
}

/*
 * _copyJoinInfo
 */
static JoinInfo *
_copyJoinInfo(JoinInfo *from)
{
	JoinInfo   *newnode = makeNode(JoinInfo);

	COPY_INTLIST_FIELD(unjoined_relids);
	COPY_NODE_FIELD(jinfo_restrictinfo);

	return newnode;
}

/*
 * _copyInnerIndexscanInfo
 */
static InnerIndexscanInfo *
_copyInnerIndexscanInfo(InnerIndexscanInfo *from)
{
	InnerIndexscanInfo   *newnode = makeNode(InnerIndexscanInfo);

	COPY_INTLIST_FIELD(other_relids);
	COPY_SCALAR_FIELD(isouterjoin);
	COPY_NODE_FIELD(best_innerpath);

	return newnode;
}

/* ****************************************************************
 *					parsenodes.h copy functions
 * ****************************************************************
 */

static TargetEntry *
_copyTargetEntry(TargetEntry *from)
{
	TargetEntry *newnode = makeNode(TargetEntry);

	COPY_NODE_FIELD(resdom);
	COPY_NODE_FIELD(fjoin);
	COPY_NODE_FIELD(expr);

	return newnode;
}

static RangeTblEntry *
_copyRangeTblEntry(RangeTblEntry *from)
{
	RangeTblEntry *newnode = makeNode(RangeTblEntry);

	COPY_SCALAR_FIELD(rtekind);
	COPY_SCALAR_FIELD(relid);
	COPY_NODE_FIELD(subquery);
	COPY_NODE_FIELD(funcexpr);
	COPY_NODE_FIELD(coldeflist);
	COPY_SCALAR_FIELD(jointype);
	COPY_NODE_FIELD(joinaliasvars);
	COPY_NODE_FIELD(alias);
	COPY_NODE_FIELD(eref);
	COPY_SCALAR_FIELD(inh);
	COPY_SCALAR_FIELD(inFromCl);
	COPY_SCALAR_FIELD(checkForRead);
	COPY_SCALAR_FIELD(checkForWrite);
	COPY_SCALAR_FIELD(checkAsUser);

	return newnode;
}

static FkConstraint *
_copyFkConstraint(FkConstraint *from)
{
	FkConstraint *newnode = makeNode(FkConstraint);

	COPY_STRING_FIELD(constr_name);
	COPY_NODE_FIELD(pktable);
	COPY_NODE_FIELD(fk_attrs);
	COPY_NODE_FIELD(pk_attrs);
	COPY_SCALAR_FIELD(fk_matchtype);
	COPY_SCALAR_FIELD(fk_upd_action);
	COPY_SCALAR_FIELD(fk_del_action);
	COPY_SCALAR_FIELD(deferrable);
	COPY_SCALAR_FIELD(initdeferred);
	COPY_SCALAR_FIELD(skip_validation);

	return newnode;
}

static SortClause *
_copySortClause(SortClause *from)
{
	SortClause *newnode = makeNode(SortClause);

	COPY_SCALAR_FIELD(tleSortGroupRef);
	COPY_SCALAR_FIELD(sortop);

	return newnode;
}

static GroupClause *
_copyGroupClause(GroupClause *from)
{
	GroupClause *newnode = makeNode(GroupClause);

	COPY_SCALAR_FIELD(tleSortGroupRef);
	COPY_SCALAR_FIELD(sortop);

	return newnode;
}

static A_Expr *
_copyAExpr(A_Expr *from)
{
	A_Expr	   *newnode = makeNode(A_Expr);

	COPY_SCALAR_FIELD(oper);
	COPY_NODE_FIELD(name);
	COPY_NODE_FIELD(lexpr);
	COPY_NODE_FIELD(rexpr);

	return newnode;
}

static ColumnRef *
_copyColumnRef(ColumnRef *from)
{
	ColumnRef  *newnode = makeNode(ColumnRef);

	COPY_NODE_FIELD(fields);
	COPY_NODE_FIELD(indirection);

	return newnode;
}

static ParamRef *
_copyParamRef(ParamRef *from)
{
	ParamRef   *newnode = makeNode(ParamRef);

	COPY_SCALAR_FIELD(number);
	COPY_NODE_FIELD(fields);
	COPY_NODE_FIELD(indirection);

	return newnode;
}

static A_Const *
_copyAConst(A_Const *from)
{
	A_Const    *newnode = makeNode(A_Const);

	/* This part must duplicate _copyValue */
	COPY_SCALAR_FIELD(val.type);
	switch (from->val.type)
	{
		case T_Integer:
			COPY_SCALAR_FIELD(val.val.ival);
			break;
		case T_Float:
		case T_String:
		case T_BitString:
			COPY_STRING_FIELD(val.val.str);
			break;
		case T_Null:
			/* nothing to do */
			break;
		default:
			elog(ERROR, "_copyAConst: unknown node type %d", from->val.type);
			break;
	}

	COPY_NODE_FIELD(typename);

	return newnode;
}

static FuncCall *
_copyFuncCall(FuncCall *from)
{
	FuncCall   *newnode = makeNode(FuncCall);

	COPY_NODE_FIELD(funcname);
	COPY_NODE_FIELD(args);
	COPY_SCALAR_FIELD(agg_star);
	COPY_SCALAR_FIELD(agg_distinct);

	return newnode;
}

static A_Indices *
_copyAIndices(A_Indices *from)
{
	A_Indices  *newnode = makeNode(A_Indices);

	COPY_NODE_FIELD(lidx);
	COPY_NODE_FIELD(uidx);

	return newnode;
}

static ExprFieldSelect *
_copyExprFieldSelect(ExprFieldSelect *from)
{
	ExprFieldSelect *newnode = makeNode(ExprFieldSelect);

	COPY_NODE_FIELD(arg);
	COPY_NODE_FIELD(fields);
	COPY_NODE_FIELD(indirection);

	return newnode;
}

static ResTarget *
_copyResTarget(ResTarget *from)
{
	ResTarget  *newnode = makeNode(ResTarget);

	COPY_STRING_FIELD(name);
	COPY_NODE_FIELD(indirection);
	COPY_NODE_FIELD(val);

	return newnode;
}

static TypeName *
_copyTypeName(TypeName *from)
{
	TypeName   *newnode = makeNode(TypeName);

	COPY_NODE_FIELD(names);
	COPY_SCALAR_FIELD(typeid);
	COPY_SCALAR_FIELD(timezone);
	COPY_SCALAR_FIELD(setof);
	COPY_SCALAR_FIELD(pct_type);
	COPY_SCALAR_FIELD(typmod);
	COPY_NODE_FIELD(arrayBounds);

	return newnode;
}

static SortGroupBy *
_copySortGroupBy(SortGroupBy *from)
{
	SortGroupBy *newnode = makeNode(SortGroupBy);

	COPY_NODE_FIELD(useOp);
	COPY_NODE_FIELD(node);

	return newnode;
}

static RangeSubselect *
_copyRangeSubselect(RangeSubselect *from)
{
	RangeSubselect *newnode = makeNode(RangeSubselect);

	COPY_NODE_FIELD(subquery);
	COPY_NODE_FIELD(alias);

	return newnode;
}

static RangeFunction *
_copyRangeFunction(RangeFunction *from)
{
	RangeFunction *newnode = makeNode(RangeFunction);

	COPY_NODE_FIELD(funccallnode);
	COPY_NODE_FIELD(alias);
	COPY_NODE_FIELD(coldeflist);

	return newnode;
}

static TypeCast *
_copyTypeCast(TypeCast *from)
{
	TypeCast   *newnode = makeNode(TypeCast);

	COPY_NODE_FIELD(arg);
	COPY_NODE_FIELD(typename);

	return newnode;
}

static IndexElem *
_copyIndexElem(IndexElem *from)
{
	IndexElem  *newnode = makeNode(IndexElem);

	COPY_STRING_FIELD(name);
	COPY_NODE_FIELD(funcname);
	COPY_NODE_FIELD(args);
	COPY_NODE_FIELD(opclass);

	return newnode;
}

static ColumnDef *
_copyColumnDef(ColumnDef *from)
{
	ColumnDef  *newnode = makeNode(ColumnDef);

	COPY_STRING_FIELD(colname);
	COPY_NODE_FIELD(typename);
	COPY_SCALAR_FIELD(inhcount);
	COPY_SCALAR_FIELD(is_local);
	COPY_SCALAR_FIELD(is_not_null);
	COPY_NODE_FIELD(raw_default);
	COPY_STRING_FIELD(cooked_default);
	COPY_NODE_FIELD(constraints);
	COPY_NODE_FIELD(support);

	return newnode;
}

static Constraint *
_copyConstraint(Constraint *from)
{
	Constraint *newnode = makeNode(Constraint);

	COPY_SCALAR_FIELD(contype);
	COPY_STRING_FIELD(name);
	COPY_NODE_FIELD(raw_expr);
	COPY_STRING_FIELD(cooked_expr);
	COPY_NODE_FIELD(keys);

	return newnode;
}

static CaseExpr *
_copyCaseExpr(CaseExpr *from)
{
	CaseExpr   *newnode = makeNode(CaseExpr);

	COPY_SCALAR_FIELD(casetype);
	COPY_NODE_FIELD(arg);
	COPY_NODE_FIELD(args);
	COPY_NODE_FIELD(defresult);

	return newnode;
}

static CaseWhen *
_copyCaseWhen(CaseWhen *from)
{
	CaseWhen   *newnode = makeNode(CaseWhen);

	COPY_NODE_FIELD(expr);
	COPY_NODE_FIELD(result);

	return newnode;
}

static NullTest *
_copyNullTest(NullTest *from)
{
	NullTest   *newnode = makeNode(NullTest);

	COPY_NODE_FIELD(arg);
	COPY_SCALAR_FIELD(nulltesttype);

	return newnode;
}

static BooleanTest *
_copyBooleanTest(BooleanTest *from)
{
	BooleanTest *newnode = makeNode(BooleanTest);

	COPY_NODE_FIELD(arg);
	COPY_SCALAR_FIELD(booltesttype);

	return newnode;
}

static ConstraintTest *
_copyConstraintTest(ConstraintTest *from)
{
	ConstraintTest *newnode = makeNode(ConstraintTest);

	COPY_NODE_FIELD(arg);
	COPY_SCALAR_FIELD(testtype);
	COPY_STRING_FIELD(name);
	COPY_STRING_FIELD(domname);
	COPY_NODE_FIELD(check_expr);

	return newnode;
}

static DomainConstraintValue *
_copyDomainConstraintValue(DomainConstraintValue *from)
{
	DomainConstraintValue *newnode = makeNode(DomainConstraintValue);

	return newnode;
}

static ConstraintTestValue *
_copyConstraintTestValue(ConstraintTestValue *from)
{
	ConstraintTestValue *newnode = makeNode(ConstraintTestValue);

	COPY_SCALAR_FIELD(typeId);
	COPY_SCALAR_FIELD(typeMod);

	return newnode;
}

static DefElem *
_copyDefElem(DefElem *from)
{
	DefElem    *newnode = makeNode(DefElem);

	COPY_STRING_FIELD(defname);
	COPY_NODE_FIELD(arg);

	return newnode;
}

static Query *
_copyQuery(Query *from)
{
	Query	   *newnode = makeNode(Query);

	COPY_SCALAR_FIELD(commandType);
	COPY_SCALAR_FIELD(querySource);
	COPY_NODE_FIELD(utilityStmt);
	COPY_SCALAR_FIELD(resultRelation);
	COPY_NODE_FIELD(into);
	COPY_SCALAR_FIELD(isPortal);
	COPY_SCALAR_FIELD(isBinary);
	COPY_SCALAR_FIELD(hasAggs);
	COPY_SCALAR_FIELD(hasSubLinks);
	COPY_NODE_FIELD(rtable);
	COPY_NODE_FIELD(jointree);
	COPY_INTLIST_FIELD(rowMarks);
	COPY_NODE_FIELD(targetList);
	COPY_NODE_FIELD(groupClause);
	COPY_NODE_FIELD(havingQual);
	COPY_NODE_FIELD(distinctClause);
	COPY_NODE_FIELD(sortClause);
	COPY_NODE_FIELD(limitOffset);
	COPY_NODE_FIELD(limitCount);
	COPY_NODE_FIELD(setOperations);
	COPY_INTLIST_FIELD(resultRelations);

	/*
	 * We do not copy the planner internal fields: base_rel_list,
	 * other_rel_list, join_rel_list, equi_key_list, query_pathkeys,
	 * hasJoinRTEs.  Not entirely clear if this is right?
	 */

	return newnode;
}

static InsertStmt *
_copyInsertStmt(InsertStmt *from)
{
	InsertStmt *newnode = makeNode(InsertStmt);

	COPY_NODE_FIELD(relation);
	COPY_NODE_FIELD(cols);
	COPY_NODE_FIELD(targetList);
	COPY_NODE_FIELD(selectStmt);

	return newnode;
}

static DeleteStmt *
_copyDeleteStmt(DeleteStmt *from)
{
	DeleteStmt *newnode = makeNode(DeleteStmt);

	COPY_NODE_FIELD(relation);
	COPY_NODE_FIELD(whereClause);

	return newnode;
}

static UpdateStmt *
_copyUpdateStmt(UpdateStmt *from)
{
	UpdateStmt *newnode = makeNode(UpdateStmt);

	COPY_NODE_FIELD(relation);
	COPY_NODE_FIELD(targetList);
	COPY_NODE_FIELD(whereClause);
	COPY_NODE_FIELD(fromClause);

	return newnode;
}

static SelectStmt *
_copySelectStmt(SelectStmt *from)
{
	SelectStmt *newnode = makeNode(SelectStmt);

	COPY_NODE_FIELD(distinctClause);
	COPY_NODE_FIELD(into);
	COPY_NODE_FIELD(intoColNames);
	COPY_NODE_FIELD(targetList);
	COPY_NODE_FIELD(fromClause);
	COPY_NODE_FIELD(whereClause);
	COPY_NODE_FIELD(groupClause);
	COPY_NODE_FIELD(havingClause);
	COPY_NODE_FIELD(sortClause);
	COPY_STRING_FIELD(portalname);
	COPY_SCALAR_FIELD(binary);
	COPY_NODE_FIELD(limitOffset);
	COPY_NODE_FIELD(limitCount);
	COPY_NODE_FIELD(forUpdate);
	COPY_SCALAR_FIELD(op);
	COPY_SCALAR_FIELD(all);
	COPY_NODE_FIELD(larg);
	COPY_NODE_FIELD(rarg);

	return newnode;
}

static SetOperationStmt *
_copySetOperationStmt(SetOperationStmt *from)
{
	SetOperationStmt *newnode = makeNode(SetOperationStmt);

	COPY_SCALAR_FIELD(op);
	COPY_SCALAR_FIELD(all);
	COPY_NODE_FIELD(larg);
	COPY_NODE_FIELD(rarg);
	COPY_INTLIST_FIELD(colTypes);

	return newnode;
}

static AlterTableStmt *
_copyAlterTableStmt(AlterTableStmt *from)
{
	AlterTableStmt *newnode = makeNode(AlterTableStmt);

	COPY_SCALAR_FIELD(subtype);
	COPY_NODE_FIELD(relation);
	COPY_STRING_FIELD(name);
	COPY_NODE_FIELD(def);
	COPY_SCALAR_FIELD(behavior);

	return newnode;
}

static GrantStmt *
_copyGrantStmt(GrantStmt *from)
{
	GrantStmt  *newnode = makeNode(GrantStmt);

	COPY_SCALAR_FIELD(is_grant);
	COPY_SCALAR_FIELD(objtype);
	COPY_NODE_FIELD(objects);
	COPY_INTLIST_FIELD(privileges);
	COPY_NODE_FIELD(grantees);

	return newnode;
}

static PrivGrantee *
_copyPrivGrantee(PrivGrantee *from)
{
	PrivGrantee *newnode = makeNode(PrivGrantee);

	COPY_STRING_FIELD(username);
	COPY_STRING_FIELD(groupname);

	return newnode;
}

static FuncWithArgs *
_copyFuncWithArgs(FuncWithArgs *from)
{
	FuncWithArgs *newnode = makeNode(FuncWithArgs);

	COPY_NODE_FIELD(funcname);
	COPY_NODE_FIELD(funcargs);

	return newnode;
}

static InsertDefault *
_copyInsertDefault(InsertDefault *from)
{
	InsertDefault *newnode = makeNode(InsertDefault);

	return newnode;
}


static ClosePortalStmt *
_copyClosePortalStmt(ClosePortalStmt *from)
{
	ClosePortalStmt *newnode = makeNode(ClosePortalStmt);

	COPY_STRING_FIELD(portalname);

	return newnode;
}

static ClusterStmt *
_copyClusterStmt(ClusterStmt *from)
{
	ClusterStmt *newnode = makeNode(ClusterStmt);

	COPY_NODE_FIELD(relation);
	COPY_STRING_FIELD(indexname);

	return newnode;
}

static CopyStmt *
_copyCopyStmt(CopyStmt *from)
{
	CopyStmt   *newnode = makeNode(CopyStmt);

	COPY_NODE_FIELD(relation);
	COPY_NODE_FIELD(attlist);
	COPY_SCALAR_FIELD(is_from);
	COPY_STRING_FIELD(filename);
	COPY_NODE_FIELD(options);

	return newnode;
}

static CreateStmt *
_copyCreateStmt(CreateStmt *from)
{
	CreateStmt *newnode = makeNode(CreateStmt);

	COPY_NODE_FIELD(relation);
	COPY_NODE_FIELD(tableElts);
	COPY_NODE_FIELD(inhRelations);
	COPY_NODE_FIELD(constraints);
	COPY_SCALAR_FIELD(hasoids);
	COPY_SCALAR_FIELD(oncommit);

	return newnode;
}

static DefineStmt *
_copyDefineStmt(DefineStmt *from)
{
	DefineStmt *newnode = makeNode(DefineStmt);

	COPY_SCALAR_FIELD(defType);
	COPY_NODE_FIELD(defnames);
	COPY_NODE_FIELD(definition);

	return newnode;
}

static DropStmt *
_copyDropStmt(DropStmt *from)
{
	DropStmt   *newnode = makeNode(DropStmt);

	COPY_NODE_FIELD(objects);
	COPY_SCALAR_FIELD(removeType);
	COPY_SCALAR_FIELD(behavior);

	return newnode;
}

static TruncateStmt *
_copyTruncateStmt(TruncateStmt *from)
{
	TruncateStmt *newnode = makeNode(TruncateStmt);

	COPY_NODE_FIELD(relation);

	return newnode;
}

static CommentStmt *
_copyCommentStmt(CommentStmt *from)
{
	CommentStmt *newnode = makeNode(CommentStmt);

	COPY_SCALAR_FIELD(objtype);
	COPY_NODE_FIELD(objname);
	COPY_NODE_FIELD(objargs);
	COPY_STRING_FIELD(comment);

	return newnode;
}

static FetchStmt *
_copyFetchStmt(FetchStmt *from)
{
	FetchStmt  *newnode = makeNode(FetchStmt);

	COPY_SCALAR_FIELD(direction);
	COPY_SCALAR_FIELD(howMany);
	COPY_STRING_FIELD(portalname);
	COPY_SCALAR_FIELD(ismove);

	return newnode;
}

static IndexStmt *
_copyIndexStmt(IndexStmt *from)
{
	IndexStmt  *newnode = makeNode(IndexStmt);

	COPY_STRING_FIELD(idxname);
	COPY_NODE_FIELD(relation);
	COPY_STRING_FIELD(accessMethod);
	COPY_NODE_FIELD(indexParams);
	COPY_NODE_FIELD(whereClause);
	COPY_NODE_FIELD(rangetable);
	COPY_SCALAR_FIELD(unique);
	COPY_SCALAR_FIELD(primary);
	COPY_SCALAR_FIELD(isconstraint);

	return newnode;
}

static CreateFunctionStmt *
_copyCreateFunctionStmt(CreateFunctionStmt *from)
{
	CreateFunctionStmt *newnode = makeNode(CreateFunctionStmt);

	COPY_SCALAR_FIELD(replace);
	COPY_NODE_FIELD(funcname);
	COPY_NODE_FIELD(argTypes);
	COPY_NODE_FIELD(returnType);
	COPY_NODE_FIELD(options);
	COPY_NODE_FIELD(withClause);

	return newnode;
}

static RemoveAggrStmt *
_copyRemoveAggrStmt(RemoveAggrStmt *from)
{
	RemoveAggrStmt *newnode = makeNode(RemoveAggrStmt);

	COPY_NODE_FIELD(aggname);
	COPY_NODE_FIELD(aggtype);
	COPY_SCALAR_FIELD(behavior);

	return newnode;
}

static RemoveFuncStmt *
_copyRemoveFuncStmt(RemoveFuncStmt *from)
{
	RemoveFuncStmt *newnode = makeNode(RemoveFuncStmt);

	COPY_NODE_FIELD(funcname);
	COPY_NODE_FIELD(args);
	COPY_SCALAR_FIELD(behavior);

	return newnode;
}

static RemoveOperStmt *
_copyRemoveOperStmt(RemoveOperStmt *from)
{
	RemoveOperStmt *newnode = makeNode(RemoveOperStmt);

	COPY_NODE_FIELD(opname);
	COPY_NODE_FIELD(args);
	COPY_SCALAR_FIELD(behavior);

	return newnode;
}

static RemoveOpClassStmt *
_copyRemoveOpClassStmt(RemoveOpClassStmt *from)
{
	RemoveOpClassStmt *newnode = makeNode(RemoveOpClassStmt);

	COPY_NODE_FIELD(opclassname);
	COPY_STRING_FIELD(amname);
	COPY_SCALAR_FIELD(behavior);

	return newnode;
}

static RenameStmt *
_copyRenameStmt(RenameStmt *from)
{
	RenameStmt *newnode = makeNode(RenameStmt);

	COPY_NODE_FIELD(relation);
	COPY_STRING_FIELD(oldname);
	COPY_STRING_FIELD(newname);
	COPY_SCALAR_FIELD(renameType);

	return newnode;
}

static RuleStmt *
_copyRuleStmt(RuleStmt *from)
{
	RuleStmt   *newnode = makeNode(RuleStmt);

	COPY_NODE_FIELD(relation);
	COPY_STRING_FIELD(rulename);
	COPY_NODE_FIELD(whereClause);
	COPY_SCALAR_FIELD(event);
	COPY_SCALAR_FIELD(instead);
	COPY_NODE_FIELD(actions);
	COPY_SCALAR_FIELD(replace);

	return newnode;
}

static NotifyStmt *
_copyNotifyStmt(NotifyStmt *from)
{
	NotifyStmt *newnode = makeNode(NotifyStmt);

	COPY_NODE_FIELD(relation);

	return newnode;
}

static ListenStmt *
_copyListenStmt(ListenStmt *from)
{
	ListenStmt *newnode = makeNode(ListenStmt);

	COPY_NODE_FIELD(relation);

	return newnode;
}

static UnlistenStmt *
_copyUnlistenStmt(UnlistenStmt *from)
{
	UnlistenStmt *newnode = makeNode(UnlistenStmt);

	COPY_NODE_FIELD(relation);

	return newnode;
}

static TransactionStmt *
_copyTransactionStmt(TransactionStmt *from)
{
	TransactionStmt *newnode = makeNode(TransactionStmt);

	COPY_SCALAR_FIELD(command);
	COPY_NODE_FIELD(options);

	return newnode;
}

static CompositeTypeStmt *
_copyCompositeTypeStmt(CompositeTypeStmt *from)
{
	CompositeTypeStmt *newnode = makeNode(CompositeTypeStmt);

	COPY_NODE_FIELD(typevar);
	COPY_NODE_FIELD(coldeflist);

	return newnode;
}

static ViewStmt *
_copyViewStmt(ViewStmt *from)
{
	ViewStmt   *newnode = makeNode(ViewStmt);

	COPY_NODE_FIELD(view);
	COPY_NODE_FIELD(aliases);
	COPY_NODE_FIELD(query);
	COPY_SCALAR_FIELD(replace);

	return newnode;
}

static LoadStmt *
_copyLoadStmt(LoadStmt *from)
{
	LoadStmt   *newnode = makeNode(LoadStmt);

	COPY_STRING_FIELD(filename);

	return newnode;
}

static CreateDomainStmt *
_copyCreateDomainStmt(CreateDomainStmt *from)
{
	CreateDomainStmt *newnode = makeNode(CreateDomainStmt);

	COPY_NODE_FIELD(domainname);
	COPY_NODE_FIELD(typename);
	COPY_NODE_FIELD(constraints);

	return newnode;
}

static CreateOpClassStmt *
_copyCreateOpClassStmt(CreateOpClassStmt *from)
{
	CreateOpClassStmt *newnode = makeNode(CreateOpClassStmt);

	COPY_NODE_FIELD(opclassname);
	COPY_STRING_FIELD(amname);
	COPY_NODE_FIELD(datatype);
	COPY_NODE_FIELD(items);
	COPY_SCALAR_FIELD(isDefault);

	return newnode;
}

static CreateOpClassItem *
_copyCreateOpClassItem(CreateOpClassItem *from)
{
	CreateOpClassItem *newnode = makeNode(CreateOpClassItem);

	COPY_SCALAR_FIELD(itemtype);
	COPY_NODE_FIELD(name);
	COPY_NODE_FIELD(args);
	COPY_SCALAR_FIELD(number);
	COPY_SCALAR_FIELD(recheck);
	COPY_NODE_FIELD(storedtype);

	return newnode;
}

static CreatedbStmt *
_copyCreatedbStmt(CreatedbStmt *from)
{
	CreatedbStmt *newnode = makeNode(CreatedbStmt);

	COPY_STRING_FIELD(dbname);
	COPY_NODE_FIELD(options);

	return newnode;
}

static AlterDatabaseSetStmt *
_copyAlterDatabaseSetStmt(AlterDatabaseSetStmt *from)
{
	AlterDatabaseSetStmt *newnode = makeNode(AlterDatabaseSetStmt);

	COPY_STRING_FIELD(dbname);
	COPY_STRING_FIELD(variable);
	COPY_NODE_FIELD(value);

	return newnode;
}

static DropdbStmt *
_copyDropdbStmt(DropdbStmt *from)
{
	DropdbStmt *newnode = makeNode(DropdbStmt);

	COPY_STRING_FIELD(dbname);

	return newnode;
}

static VacuumStmt *
_copyVacuumStmt(VacuumStmt *from)
{
	VacuumStmt *newnode = makeNode(VacuumStmt);

	COPY_SCALAR_FIELD(vacuum);
	COPY_SCALAR_FIELD(full);
	COPY_SCALAR_FIELD(analyze);
	COPY_SCALAR_FIELD(freeze);
	COPY_SCALAR_FIELD(verbose);
	COPY_NODE_FIELD(relation);
	COPY_NODE_FIELD(va_cols);

	return newnode;
}

static ExplainStmt *
_copyExplainStmt(ExplainStmt *from)
{
	ExplainStmt *newnode = makeNode(ExplainStmt);

	COPY_NODE_FIELD(query);
	COPY_SCALAR_FIELD(verbose);
	COPY_SCALAR_FIELD(analyze);

	return newnode;
}

static CreateSeqStmt *
_copyCreateSeqStmt(CreateSeqStmt *from)
{
	CreateSeqStmt *newnode = makeNode(CreateSeqStmt);

	COPY_NODE_FIELD(sequence);
	COPY_NODE_FIELD(options);

	return newnode;
}

static VariableSetStmt *
_copyVariableSetStmt(VariableSetStmt *from)
{
	VariableSetStmt *newnode = makeNode(VariableSetStmt);

	COPY_STRING_FIELD(name);
	COPY_NODE_FIELD(args);
	COPY_SCALAR_FIELD(is_local);

	return newnode;
}

static VariableShowStmt *
_copyVariableShowStmt(VariableShowStmt *from)
{
	VariableShowStmt *newnode = makeNode(VariableShowStmt);

	COPY_STRING_FIELD(name);

	return newnode;
}

static VariableResetStmt *
_copyVariableResetStmt(VariableResetStmt *from)
{
	VariableResetStmt *newnode = makeNode(VariableResetStmt);

	COPY_STRING_FIELD(name);

	return newnode;
}

static CreateTrigStmt *
_copyCreateTrigStmt(CreateTrigStmt *from)
{
	CreateTrigStmt *newnode = makeNode(CreateTrigStmt);

	COPY_STRING_FIELD(trigname);
	COPY_NODE_FIELD(relation);
	COPY_NODE_FIELD(funcname);
	COPY_NODE_FIELD(args);
	COPY_SCALAR_FIELD(before);
	COPY_SCALAR_FIELD(row);
	strcpy(newnode->actions, from->actions); /* in-line string field */
	COPY_SCALAR_FIELD(isconstraint);
	COPY_SCALAR_FIELD(deferrable);
	COPY_SCALAR_FIELD(initdeferred);
	COPY_NODE_FIELD(constrrel);

	return newnode;
}

static DropPropertyStmt *
_copyDropPropertyStmt(DropPropertyStmt *from)
{
	DropPropertyStmt *newnode = makeNode(DropPropertyStmt);

	COPY_NODE_FIELD(relation);
	COPY_STRING_FIELD(property);
	COPY_SCALAR_FIELD(removeType);
	COPY_SCALAR_FIELD(behavior);

	return newnode;
}

static CreatePLangStmt *
_copyCreatePLangStmt(CreatePLangStmt *from)
{
	CreatePLangStmt *newnode = makeNode(CreatePLangStmt);

	COPY_STRING_FIELD(plname);
	COPY_NODE_FIELD(plhandler);
	COPY_NODE_FIELD(plvalidator);
	COPY_SCALAR_FIELD(pltrusted);

	return newnode;
}

static DropPLangStmt *
_copyDropPLangStmt(DropPLangStmt *from)
{
	DropPLangStmt *newnode = makeNode(DropPLangStmt);

	COPY_STRING_FIELD(plname);
	COPY_SCALAR_FIELD(behavior);

	return newnode;
}

static CreateUserStmt *
_copyCreateUserStmt(CreateUserStmt *from)
{
	CreateUserStmt *newnode = makeNode(CreateUserStmt);

	COPY_STRING_FIELD(user);
	COPY_NODE_FIELD(options);

	return newnode;
}

static AlterUserStmt *
_copyAlterUserStmt(AlterUserStmt *from)
{
	AlterUserStmt *newnode = makeNode(AlterUserStmt);

	COPY_STRING_FIELD(user);
	COPY_NODE_FIELD(options);

	return newnode;
}

static AlterUserSetStmt *
_copyAlterUserSetStmt(AlterUserSetStmt *from)
{
	AlterUserSetStmt *newnode = makeNode(AlterUserSetStmt);

	COPY_STRING_FIELD(user);
	COPY_STRING_FIELD(variable);
	COPY_NODE_FIELD(value);

	return newnode;
}

static DropUserStmt *
_copyDropUserStmt(DropUserStmt *from)
{
	DropUserStmt *newnode = makeNode(DropUserStmt);

	COPY_NODE_FIELD(users);

	return newnode;
}

static LockStmt *
_copyLockStmt(LockStmt *from)
{
	LockStmt   *newnode = makeNode(LockStmt);

	COPY_NODE_FIELD(relations);
	COPY_SCALAR_FIELD(mode);

	return newnode;
}

static ConstraintsSetStmt *
_copyConstraintsSetStmt(ConstraintsSetStmt *from)
{
	ConstraintsSetStmt *newnode = makeNode(ConstraintsSetStmt);

	COPY_NODE_FIELD(constraints);
	COPY_SCALAR_FIELD(deferred);

	return newnode;
}

static CreateGroupStmt *
_copyCreateGroupStmt(CreateGroupStmt *from)
{
	CreateGroupStmt *newnode = makeNode(CreateGroupStmt);

	COPY_STRING_FIELD(name);
	COPY_NODE_FIELD(options);

	return newnode;
}

static AlterGroupStmt *
_copyAlterGroupStmt(AlterGroupStmt *from)
{
	AlterGroupStmt *newnode = makeNode(AlterGroupStmt);

	COPY_STRING_FIELD(name);
	COPY_SCALAR_FIELD(action);
	COPY_NODE_FIELD(listUsers);

	return newnode;
}

static DropGroupStmt *
_copyDropGroupStmt(DropGroupStmt *from)
{
	DropGroupStmt *newnode = makeNode(DropGroupStmt);

	COPY_STRING_FIELD(name);

	return newnode;
}

static ReindexStmt *
_copyReindexStmt(ReindexStmt *from)
{
	ReindexStmt *newnode = makeNode(ReindexStmt);

	COPY_SCALAR_FIELD(reindexType);
	COPY_NODE_FIELD(relation);
	COPY_STRING_FIELD(name);
	COPY_SCALAR_FIELD(force);
	COPY_SCALAR_FIELD(all);

	return newnode;
}

static CreateSchemaStmt *
_copyCreateSchemaStmt(CreateSchemaStmt *from)
{
	CreateSchemaStmt *newnode = makeNode(CreateSchemaStmt);

	COPY_STRING_FIELD(schemaname);
	COPY_STRING_FIELD(authid);
	COPY_NODE_FIELD(schemaElts);

	return newnode;
}

static CreateConversionStmt *
_copyCreateConversionStmt(CreateConversionStmt *from)
{
	CreateConversionStmt *newnode = makeNode(CreateConversionStmt);

	COPY_NODE_FIELD(conversion_name);
	COPY_STRING_FIELD(for_encoding_name);
	COPY_STRING_FIELD(to_encoding_name);
	COPY_NODE_FIELD(func_name);
	COPY_SCALAR_FIELD(def);

	return newnode;
}

static CreateCastStmt *
_copyCreateCastStmt(CreateCastStmt *from)
{
	CreateCastStmt *newnode = makeNode(CreateCastStmt);

	COPY_NODE_FIELD(sourcetype);
	COPY_NODE_FIELD(targettype);
	COPY_NODE_FIELD(func);
	COPY_SCALAR_FIELD(context);

	return newnode;
}

static DropCastStmt *
_copyDropCastStmt(DropCastStmt *from)
{
	DropCastStmt *newnode = makeNode(DropCastStmt);

	COPY_NODE_FIELD(sourcetype);
	COPY_NODE_FIELD(targettype);
	COPY_SCALAR_FIELD(behavior);

	return newnode;
}

static PrepareStmt *
_copyPrepareStmt(PrepareStmt *from)
{
	PrepareStmt *newnode = makeNode(PrepareStmt);

	COPY_STRING_FIELD(name);
	COPY_NODE_FIELD(argtypes);
	COPY_INTLIST_FIELD(argtype_oids);
	COPY_NODE_FIELD(query);

	return newnode;
}

static ExecuteStmt *
_copyExecuteStmt(ExecuteStmt *from)
{
	ExecuteStmt *newnode = makeNode(ExecuteStmt);

	COPY_STRING_FIELD(name);
	COPY_NODE_FIELD(into);
	COPY_NODE_FIELD(params);

	return newnode;
}

static DeallocateStmt *
_copyDeallocateStmt(DeallocateStmt *from)
{
	DeallocateStmt *newnode = makeNode(DeallocateStmt);

	COPY_STRING_FIELD(name);

	return newnode;
}


/* ****************************************************************
 *					pg_list.h copy functions
 * ****************************************************************
 */

static Value *
_copyValue(Value *from)
{
	Value	   *newnode = makeNode(Value);

	/* See also _copyAConst when changing this code! */

	COPY_SCALAR_FIELD(type);
	switch (from->type)
	{
		case T_Integer:
			COPY_SCALAR_FIELD(val.ival);
			break;
		case T_Float:
		case T_String:
		case T_BitString:
			COPY_STRING_FIELD(val.str);
			break;
		case T_Null:
			/* nothing to do */
			break;
		default:
			elog(ERROR, "_copyValue: unknown node type %d", from->type);
			break;
	}
	return newnode;
}

/*
 * copyObject
 *
 * Create a copy of a Node tree or list.  This is a "deep" copy: all
 * substructure is copied too, recursively.
 */
void *
copyObject(void *from)
{
	void	   *retval;

	if (from == NULL)
		return NULL;

	switch (nodeTag(from))
	{
			/*
			 * PLAN NODES
			 */
		case T_Plan:
			retval = _copyPlan(from);
			break;
		case T_Result:
			retval = _copyResult(from);
			break;
		case T_Append:
			retval = _copyAppend(from);
			break;
		case T_Scan:
			retval = _copyScan(from);
			break;
		case T_SeqScan:
			retval = _copySeqScan(from);
			break;
		case T_IndexScan:
			retval = _copyIndexScan(from);
			break;
		case T_TidScan:
			retval = _copyTidScan(from);
			break;
		case T_SubqueryScan:
			retval = _copySubqueryScan(from);
			break;
		case T_FunctionScan:
			retval = _copyFunctionScan(from);
			break;
		case T_Join:
			retval = _copyJoin(from);
			break;
		case T_NestLoop:
			retval = _copyNestLoop(from);
			break;
		case T_MergeJoin:
			retval = _copyMergeJoin(from);
			break;
		case T_HashJoin:
			retval = _copyHashJoin(from);
			break;
		case T_Material:
			retval = _copyMaterial(from);
			break;
		case T_Sort:
			retval = _copySort(from);
			break;
		case T_Group:
			retval = _copyGroup(from);
			break;
		case T_Agg:
			retval = _copyAgg(from);
			break;
		case T_Unique:
			retval = _copyUnique(from);
			break;
		case T_SetOp:
			retval = _copySetOp(from);
			break;
		case T_Limit:
			retval = _copyLimit(from);
			break;
		case T_Hash:
			retval = _copyHash(from);
			break;
		case T_SubPlan:
			retval = _copySubPlan(from);
			break;

			/*
			 * PRIMITIVE NODES
			 */
		case T_Resdom:
			retval = _copyResdom(from);
			break;
		case T_Fjoin:
			retval = _copyFjoin(from);
			break;
		case T_Alias:
			retval = _copyAlias(from);
			break;
		case T_RangeVar:
			retval = _copyRangeVar(from);
			break;
		case T_Expr:
			retval = _copyExpr(from);
			break;
		case T_Var:
			retval = _copyVar(from);
			break;
		case T_Oper:
			retval = _copyOper(from);
			break;
		case T_Const:
			retval = _copyConst(from);
			break;
		case T_Param:
			retval = _copyParam(from);
			break;
		case T_Func:
			retval = _copyFunc(from);
			break;
		case T_Aggref:
			retval = _copyAggref(from);
			break;
		case T_SubLink:
			retval = _copySubLink(from);
			break;
		case T_FieldSelect:
			retval = _copyFieldSelect(from);
			break;
		case T_RelabelType:
			retval = _copyRelabelType(from);
			break;
		case T_RangeTblRef:
			retval = _copyRangeTblRef(from);
			break;
		case T_JoinExpr:
			retval = _copyJoinExpr(from);
			break;
		case T_FromExpr:
			retval = _copyFromExpr(from);
			break;
		case T_ArrayRef:
			retval = _copyArrayRef(from);
			break;

			/*
			 * RELATION NODES
			 */
		case T_RelOptInfo:
			retval = _copyRelOptInfo(from);
			break;
		case T_Path:
			retval = _copyPath(from);
			break;
		case T_IndexPath:
			retval = _copyIndexPath(from);
			break;
		case T_TidPath:
			retval = _copyTidPath(from);
			break;
		case T_AppendPath:
			retval = _copyAppendPath(from);
			break;
		case T_ResultPath:
			retval = _copyResultPath(from);
			break;
		case T_NestPath:
			retval = _copyNestPath(from);
			break;
		case T_MergePath:
			retval = _copyMergePath(from);
			break;
		case T_HashPath:
			retval = _copyHashPath(from);
			break;
		case T_PathKeyItem:
			retval = _copyPathKeyItem(from);
			break;
		case T_RestrictInfo:
			retval = _copyRestrictInfo(from);
			break;
		case T_JoinInfo:
			retval = _copyJoinInfo(from);
			break;
		case T_IndexOptInfo:
			retval = _copyIndexOptInfo(from);
			break;
		case T_InnerIndexscanInfo:
			retval = _copyInnerIndexscanInfo(from);
			break;

			/*
			 * VALUE NODES
			 */
		case T_Integer:
		case T_Float:
		case T_String:
		case T_BitString:
		case T_Null:
			retval = _copyValue(from);
			break;
		case T_List:
			{
				List	   *list = from,
						   *l,
						   *nl;

				/* rather ugly coding for speed... */
				/* Note the input list cannot be NIL if we got here. */
				nl = makeList1(copyObject(lfirst(list)));
				retval = nl;

				foreach(l, lnext(list))
				{
					lnext(nl) = makeList1(copyObject(lfirst(l)));
					nl = lnext(nl);
				}
			}
			break;

			/*
			 * PARSE NODES
			 */
		case T_Query:
			retval = _copyQuery(from);
			break;
		case T_InsertStmt:
			retval = _copyInsertStmt(from);
			break;
		case T_DeleteStmt:
			retval = _copyDeleteStmt(from);
			break;
		case T_UpdateStmt:
			retval = _copyUpdateStmt(from);
			break;
		case T_SelectStmt:
			retval = _copySelectStmt(from);
			break;
		case T_SetOperationStmt:
			retval = _copySetOperationStmt(from);
			break;
		case T_AlterTableStmt:
			retval = _copyAlterTableStmt(from);
			break;
		case T_GrantStmt:
			retval = _copyGrantStmt(from);
			break;
		case T_ClosePortalStmt:
			retval = _copyClosePortalStmt(from);
			break;
		case T_ClusterStmt:
			retval = _copyClusterStmt(from);
			break;
		case T_CopyStmt:
			retval = _copyCopyStmt(from);
			break;
		case T_CreateStmt:
			retval = _copyCreateStmt(from);
			break;
		case T_DefineStmt:
			retval = _copyDefineStmt(from);
			break;
		case T_DropStmt:
			retval = _copyDropStmt(from);
			break;
		case T_TruncateStmt:
			retval = _copyTruncateStmt(from);
			break;
		case T_CommentStmt:
			retval = _copyCommentStmt(from);
			break;
		case T_FetchStmt:
			retval = _copyFetchStmt(from);
			break;
		case T_IndexStmt:
			retval = _copyIndexStmt(from);
			break;
		case T_CreateFunctionStmt:
			retval = _copyCreateFunctionStmt(from);
			break;
		case T_RemoveAggrStmt:
			retval = _copyRemoveAggrStmt(from);
			break;
		case T_RemoveFuncStmt:
			retval = _copyRemoveFuncStmt(from);
			break;
		case T_RemoveOperStmt:
			retval = _copyRemoveOperStmt(from);
			break;
		case T_RemoveOpClassStmt:
			retval = _copyRemoveOpClassStmt(from);
			break;
		case T_RenameStmt:
			retval = _copyRenameStmt(from);
			break;
		case T_RuleStmt:
			retval = _copyRuleStmt(from);
			break;
		case T_NotifyStmt:
			retval = _copyNotifyStmt(from);
			break;
		case T_ListenStmt:
			retval = _copyListenStmt(from);
			break;
		case T_UnlistenStmt:
			retval = _copyUnlistenStmt(from);
			break;
		case T_TransactionStmt:
			retval = _copyTransactionStmt(from);
			break;
		case T_CompositeTypeStmt:
			retval = _copyCompositeTypeStmt(from);
			break;
		case T_ViewStmt:
			retval = _copyViewStmt(from);
			break;
		case T_LoadStmt:
			retval = _copyLoadStmt(from);
			break;
		case T_CreateDomainStmt:
			retval = _copyCreateDomainStmt(from);
			break;
		case T_CreateOpClassStmt:
			retval = _copyCreateOpClassStmt(from);
			break;
		case T_CreateOpClassItem:
			retval = _copyCreateOpClassItem(from);
			break;
		case T_CreatedbStmt:
			retval = _copyCreatedbStmt(from);
			break;
		case T_AlterDatabaseSetStmt:
			retval = _copyAlterDatabaseSetStmt(from);
			break;
		case T_DropdbStmt:
			retval = _copyDropdbStmt(from);
			break;
		case T_VacuumStmt:
			retval = _copyVacuumStmt(from);
			break;
		case T_ExplainStmt:
			retval = _copyExplainStmt(from);
			break;
		case T_CreateSeqStmt:
			retval = _copyCreateSeqStmt(from);
			break;
		case T_VariableSetStmt:
			retval = _copyVariableSetStmt(from);
			break;
		case T_VariableShowStmt:
			retval = _copyVariableShowStmt(from);
			break;
		case T_VariableResetStmt:
			retval = _copyVariableResetStmt(from);
			break;
		case T_CreateTrigStmt:
			retval = _copyCreateTrigStmt(from);
			break;
		case T_DropPropertyStmt:
			retval = _copyDropPropertyStmt(from);
			break;
		case T_CreatePLangStmt:
			retval = _copyCreatePLangStmt(from);
			break;
		case T_DropPLangStmt:
			retval = _copyDropPLangStmt(from);
			break;
		case T_CreateUserStmt:
			retval = _copyCreateUserStmt(from);
			break;
		case T_AlterUserStmt:
			retval = _copyAlterUserStmt(from);
			break;
		case T_AlterUserSetStmt:
			retval = _copyAlterUserSetStmt(from);
			break;
		case T_DropUserStmt:
			retval = _copyDropUserStmt(from);
			break;
		case T_LockStmt:
			retval = _copyLockStmt(from);
			break;
		case T_ConstraintsSetStmt:
			retval = _copyConstraintsSetStmt(from);
			break;
		case T_CreateGroupStmt:
			retval = _copyCreateGroupStmt(from);
			break;
		case T_AlterGroupStmt:
			retval = _copyAlterGroupStmt(from);
			break;
		case T_DropGroupStmt:
			retval = _copyDropGroupStmt(from);
			break;
		case T_ReindexStmt:
			retval = _copyReindexStmt(from);
			break;
		case T_CheckPointStmt:
			retval = (void *) makeNode(CheckPointStmt);
			break;
		case T_CreateSchemaStmt:
			retval = _copyCreateSchemaStmt(from);
			break;
		case T_CreateConversionStmt:
			retval = _copyCreateConversionStmt(from);
			break;
		case T_CreateCastStmt:
			retval = _copyCreateCastStmt(from);
			break;
		case T_DropCastStmt:
			retval = _copyDropCastStmt(from);
			break;
		case T_PrepareStmt:
			retval = _copyPrepareStmt(from);
			break;
		case T_ExecuteStmt:
			retval = _copyExecuteStmt(from);
			break;
		case T_DeallocateStmt:
			retval = _copyDeallocateStmt(from);
			break;

		case T_A_Expr:
			retval = _copyAExpr(from);
			break;
		case T_ColumnRef:
			retval = _copyColumnRef(from);
			break;
		case T_ParamRef:
			retval = _copyParamRef(from);
			break;
		case T_A_Const:
			retval = _copyAConst(from);
			break;
		case T_FuncCall:
			retval = _copyFuncCall(from);
			break;
		case T_A_Indices:
			retval = _copyAIndices(from);
			break;
		case T_ExprFieldSelect:
			retval = _copyExprFieldSelect(from);
			break;
		case T_ResTarget:
			retval = _copyResTarget(from);
			break;
		case T_TypeCast:
			retval = _copyTypeCast(from);
			break;
		case T_SortGroupBy:
			retval = _copySortGroupBy(from);
			break;
		case T_RangeSubselect:
			retval = _copyRangeSubselect(from);
			break;
		case T_RangeFunction:
			retval = _copyRangeFunction(from);
			break;
		case T_TypeName:
			retval = _copyTypeName(from);
			break;
		case T_IndexElem:
			retval = _copyIndexElem(from);
			break;
		case T_ColumnDef:
			retval = _copyColumnDef(from);
			break;
		case T_Constraint:
			retval = _copyConstraint(from);
			break;
		case T_DefElem:
			retval = _copyDefElem(from);
			break;
		case T_TargetEntry:
			retval = _copyTargetEntry(from);
			break;
		case T_RangeTblEntry:
			retval = _copyRangeTblEntry(from);
			break;
		case T_SortClause:
			retval = _copySortClause(from);
			break;
		case T_GroupClause:
			retval = _copyGroupClause(from);
			break;
		case T_CaseExpr:
			retval = _copyCaseExpr(from);
			break;
		case T_CaseWhen:
			retval = _copyCaseWhen(from);
			break;
		case T_NullTest:
			retval = _copyNullTest(from);
			break;
		case T_BooleanTest:
			retval = _copyBooleanTest(from);
			break;
		case T_ConstraintTest:
			retval = _copyConstraintTest(from);
			break;
		case T_ConstraintTestValue:
			retval = _copyConstraintTestValue(from);
			break;
		case T_FkConstraint:
			retval = _copyFkConstraint(from);
			break;
		case T_PrivGrantee:
			retval = _copyPrivGrantee(from);
			break;
		case T_FuncWithArgs:
			retval = _copyFuncWithArgs(from);
			break;
		case T_InsertDefault:
			retval = _copyInsertDefault(from);
			break;
		case T_DomainConstraintValue:
			retval = _copyDomainConstraintValue(from);
			break;

		default:
			elog(ERROR, "copyObject: don't know how to copy node type %d",
				 nodeTag(from));
			retval = from;		/* keep compiler quiet */
			break;
	}

	return retval;
}
