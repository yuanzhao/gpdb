/*-------------------------------------------------------------------------
 *
 * subselect.h
 *
 * Portions Copyright (c) 2006-2008, Greenplum inc
 * Portions Copyright (c) 1996-2008, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $PostgreSQL: pgsql/src/include/optimizer/subselect.h,v 1.34 2008/10/04 21:56:55 tgl Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef SUBSELECT_H
#define SUBSELECT_H

#include "nodes/plannodes.h"
#include "nodes/relation.h"

extern void SS_process_ctes(PlannerInfo *root);
extern Node *convert_IN_to_join(PlannerInfo *root, List **rtrlist_inout, SubLink *sublink);
extern Node *convert_testexpr(PlannerInfo *root,
				 Node *testexpr,
				 List *subst_nodes);
extern Node *SS_replace_correlation_vars(PlannerInfo *root, Node *expr);
extern Node *SS_process_sublinks(PlannerInfo *root, Node *expr, bool isQual);
extern void SS_finalize_plan(PlannerInfo *root, Plan *plan,
							 bool attach_initplans);
extern Param *SS_make_initplan_from_plan(PlannerInfo *root, Plan *plan,
						   Oid resulttype, int32 resulttypmod);
extern int	SS_assign_worktable_param(PlannerInfo *root);

extern bool IsSubqueryCorrelated(Query *sq);
extern bool IsSubqueryMultiLevelCorrelated(Query *sq);

extern List *generate_subquery_vars(PlannerInfo *root, List *tlist,
					   Index varno);

#endif   /* SUBSELECT_H */
