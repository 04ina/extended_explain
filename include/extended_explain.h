/*-------------------------------------------------------------------------
 *
 * extended_explain.h
 * 
 *-------------------------------------------------------------------------
 */

#ifndef EXTENDED_EXPLAIN_H 
#define EXTENDED_EXPLAIN_H 

#include "postgres.h"

#include "nodes/bitmapset.h" 
#include "commands/dbcommands.h"
#include "utils/lsyscache.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "catalog/pg_type.h"
#include "optimizer/planner.h"
#include "commands/explain.h"

#include "optimizer/paths.h"          // Для add_path_hook_type, set_rel_pathlist_hook_type и др.
#include "commands/explain.h"        // Для ExplainOneQuery_hook_type
#include "optimizer/planmain.h"      // Для create_upper_paths_hook_type
#include "optimizer/pathnode.h"
#include "optimizer/planner.h"

typedef struct EEStatus 
{
	MemoryContext ctx; 

	List *eerel_list;
	List *eepath_list;
	List *eesubquery_list;

	int64 eepath_counter;
	int64 init_level;
} EEStatus;

typedef struct EERel 
{
	RelOptInfo *roi_pointer;

	Alias *eref;

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

extern void ee_get_upper_paths_hook(PlannerInfo *root,
									UpperRelationKind stage,
									RelOptInfo *input_rel,
									RelOptInfo *output_rel,
									void *extra);

#endif  /* EXTENDED_EXPLAIN_H */