/*-------------------------------------------------------------------------
 *
 * extended_explain.h
 * 
 *-------------------------------------------------------------------------
 */

#ifndef EXTENDED_EXPLAIN_H 
#define EXTENDED_EXPLAIN_H 

#include "nodes/bitmapset.h" 
#include "commands/dbcommands.h"
#include "utils/lsyscache.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "catalog/pg_type.h"
#include "postgres.h"

#include "optimizer/planner.h"

typedef struct EEStatus 
{
	MemoryContext ctx = NULL; 

	List *eerel_list;
	List *eepath_list;
	List *eesubquery_list;

	int64 eepath_counter;
	int64 init_level;
} EEStatus;

typedef struct EERel 
{
	RelOptInfo *roi_pointer;

	int level;
	List *eepath_list;
} EERel;

typedef struct EEPath 
{
	Path *path_pointer;

	int64 id;
	NodeTag	pathtype;
	int level;

	int nsub;
	struct EEPath *sub_eepath_1;
	struct EEPath *sub_eepath_2;

	Cardinality rows;			/* estimated number of result tuples */
	Cost		startup_cost;	/* cost expended before fetching any tuples */
	Cost		total_cost;		/* total cost (assuming all tuples fetched) */

	bool is_del;
} EEPath;

typedef struct EESubQuery
{
	PlannerInfo *PlannerInfo_pointer;

	List *eerel_list;

} EESubQuery;

/*-------------------------------------------------------------------------
 * 								function Headers 
 *-------------------------------------------------------------------------
 */

/*
extern void ee_remember_path(RelOptInfo *parent_rel, 
							Path *new_path,
							bool accept_new,
							int insert_at);

extern void ee_explain(Query *query, int cursorOptions,
					   IntoClause *into, ExplainState *es,
					   const char *queryString, ParamListInfo params,
					   QueryEnvironment *queryEnv);

extern void ee_remember_join_pathlist(PlannerInfo *root,
									  RelOptInfo *joinrel,
									  RelOptInfo *outerrel,
									  RelOptInfo *innerrel,
									  JoinType jointype,
									  JoinPathExtraData *extra);

extern void ee_remember_rel_pathlist(PlannerInfo *root,
									  RelOptInfo *rel,
									  Index rti,
									  RangeTblEntry *rte);
*/
#endif  /* EXTENDED_EXPLAIN_H */