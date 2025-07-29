/*-------------------------------------------------------------------------
 *
 * extended_explain.c
 *
 *-------------------------------------------------------------------------
 */

//#include "include/extended_explain.h"
//#include "extended_explain.h"

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

PG_MODULE_MAGIC;

/* hooks */
static ExplainOneQuery_hook_type prev_ExplainOneQuery_hook = NULL;

static add_path_hook_type prev_add_path_hook = NULL;

static set_rel_pathlist_hook_type prev_set_rel_pathlist_hook = NULL;
static set_join_pathlist_hook_type prev_set_join_pathlist_hook = NULL;
static create_upper_paths_hook_type prev_create_upper_paths_hook = NULL;

EEStatus *global_ee_status = NULL;

static EEStatus *init_ee_status(void);

static void delete_ee_status(EEStatus *ee_status);

EEStatus *
init_ee_status(void)
{
	EEStatus *ee_status; 

	ee_status = (EEStatus *) palloc0(sizeof(EEStatus)); 

	ee_status->ctx = AllocSetContextCreate(TopMemoryContext,
										   "extended explain context",
										   ALLOCSET_DEFAULT_SIZES);
	
	ee_status->eerel_list = NIL;
	ee_status->eepath_list = NIL;
	ee_status->eesubquery_list = NIL;
		
	ee_status->eepath_counter = 1;
	ee_status->init_level = 1;
	return ee_status;
}

void 
delete_ee_status(EEStatus *ee_status)
{
	// А надо ли ??? list_free() ???
    if (ee_status->eepath_list)
    {
        list_free_deep(eerel_list);
        eerel_list = NIL;
    }
    
    if (ee_status->eerel_list)
    {
        list_free_deep(eepathlist);
        eepathlist = NIL;
    }

    if (ee_status->eesubquery_list)
    {
        list_free_deep(eepathlist);
        eepathlist = NIL;
    }
	/*
	
		old_ctx = MemoryContextSwitchTo(ee_ctx);

		// Очистка предыдущих данных

		MemoryContextSwitchTo(old_ctx);
		
		list_free_deep...
	*/

	MemoryContextReset(ee_status->ctx);

	pfree(ee_status);
}

void 
_PG_init(void) 
{
	is_explain = false;
	global_ee_status = NULL;

	prev_ExplainOneQuery_hook = ExplainOneQuery_hook;
	ExplainOneQuery_hook = ee_explain;

	prev_add_path_hook = add_path_hook;
	add_path_hook = ee_remember_path;

	prev_set_rel_pathlist_hook = set_rel_pathlist_hook;
	set_rel_pathlist_hook = ee_remember_rel_pathlist;

	prev_set_join_pathlist_hook = set_join_pathlist_hook;
	set_join_pathlist_hook = ee_remember_join_pathlist;

	prev_create_upper_paths_hook = create_upper_paths_hook;
	create_upper_paths_hook = ee_get_upper_paths_hook; 
}

void 
ee_explain(Query *query, int cursorOptions,
		   IntoClause *into, ExplainState *es,
		   const char *queryString, ParamListInfo params,
		   QueryEnvironment *queryEnv) 
{
	MemoryContext old_ctx;
	ListCell *br;	

	is_explain = true;

	global_ee_status = init_ee_status();

	standard_ExplainOneQuery(query, cursorOptions, into, es,
							 queryString, params, queryEnv);

    if (es->format == EXPLAIN_FORMAT_TEXT) 
	{
		appendStringInfo(es->str, "\n-- Additional Paths Info --\n");
		appendStringInfo(es->str, "Total scan paths: %d\n", 4);
		appendStringInfo(es->str, "Total join paths: %d\n", 4);
		appendStringInfoString(es->str, "Planning:\n");

	}


	//old_ctx = MemoryContextSwitchTo(ee_ctx); ??? 

	/*
		функция для всех отношений

		foreach(br, eerel_list)
		{
			EERel *rel = (EERel *) lfirst(br); 

			FillPathsTable(rel);
		}
	*/

	//MemoryContextSwitchTo(old_ctx);

	delete_ee_status(global_ee_status);

	is_explain = false;
}

/*
bool is_explain = false; 
int64 counter = 0;
static int init_level = 1;

List *eerel_list;
List *eepathlist;
*/

PG_FUNCTION_INFO_V1(ee_func);

Datum
ee_func(PG_FUNCTION_ARGS)
{
	PG_RETURN_BOOL(true);
}


void FillPathsTable(EERel *eerel);

EERel *
eerel_exist(RelOptInfo *roi)
{
	ListCell *cell;
	foreach (cell, global_ee_status->eerel_list)	
	{
		EERel *eerel = (EERel *) lfirst(cell);
		if (eerel->roi_pointer == roi)
		{
			return eerel;	
		}
	}
	return NULL;
}

EEPath *
eepath_exist(Path *path)
{
	ListCell *cl;
	foreach (cl, global_ee_status->eepath_list)	
	{
		EEPath *eepath = (EEPath *) lfirst(cl);
		if (eepath->path_pointer == path)
		{
			return eepath;	
		}
	}
	return NULL;
}

EERel *
init_eerel(void)
{
	EERel *eerel = (EERel *) palloc0(sizeof(EERel));
	global_ee_status->eerel_list = lappend(global_ee_status->eerel_list, eerel);

	return eerel;
}

EEPath *
init_eepath(void)
{
	EEPath *eepath = (EEPath *) palloc0(sizeof(EEPath));
	global_ee_status->eepath_list = lappend(global_ee_status->eepath_list, eepath);
	eepath->id = global_ee_status->counter++;

	return eepath;
}

void 
fill_eerel(EERel *eerel, RelOptInfo *roi)
{
	eerel->roi_pointer = roi;

	//eerel->level = bms_num_members(roi->relids);
}

void 
fill_eepath(EEPath *eepath, Path *path)
{
	eepath->path_pointer = path;
	eepath->pathtype = path->pathtype;

	eepath->sub_eepath_1 = NULL;
	eepath->sub_eepath_2 = NULL;

	eepath->rows = path->rows;
	eepath->startup_cost = path->startup_cost;
	eepath->total_cost = path->total_cost;
}

/*
 *
 */
void 
ee_remember_join_pathlist(PlannerInfo *root,
						  RelOptInfo *joinrel,
						  RelOptInfo *outerrel,
						  RelOptInfo *innerrel,
						  JoinType jointype,
						  JoinPathExtraData *extra)
{
	if (!is_explain)	
		return;

}

/*
 *
 */
void 
ee_remember_rel_pathlist(PlannerInfo *root,
						 RelOptInfo *rel,
						 Index rti,
						 RangeTblEntry *rte)
{
	if (!is_explain)	
		return;

}

void
ee_get_upper_paths_hook(PlannerInfo *root,
						UpperRelationKind stage,
						RelOptInfo *input_rel,
						RelOptInfo *output_rel,
						void *extra)
{
	if (!is_explain)	
		return;

	/*
	if (stage != UPPERREL_FINAL)
		return;
	*/

}

void
ee_remember_path(RelOptInfo *parent_rel, 
				 Path *new_path,
				 bool accept_new,
				 int insert_at)
{
	EERel *eerel;
	EEPath *eepath;
	MemoryContext old_ctx;

	if (!is_explain)
		return;

	old_ctx = MemoryContextSwitchTo(ee_ctx);

	eerel = eerel_exist(parent_rel);

	if (eerel == NULL)
	{
		eerel = init_eerel();	
		fill_eerel(eerel, parent_rel);
	}

	eepath = init_eepath();
	fill_eepath(eepath, new_path);
	eerel->eepaths = lappend(eerel->eepaths, eepath);
	eepath->is_del = !accept_new;

	switch (new_path->type)
	{
		case T_Path:
		case T_IndexPath:
		case T_BitmapHeapPath:
			eepath->nsub = 0;
			eepath->level = init_level;
			break;
		case T_SubqueryScanPath:
		case T_ProjectionPath:
		case T_LimitPath:
			eepath->nsub = 1;
			eepath->sub_eepath_1 = eepath_exist(((MaterialPath *)new_path)->subpath);

			if (eepath->sub_eepath_1 == NULL /* && ((((MaterialPath *)new_path)->subpath->pathtype == T_Memoize) || (((MaterialPath *)new_path)->subpath->pathtype == T_Material))*/)
			{
				EEPath *eempath;
				EERel *eemrel;

				eempath = init_eepath();
				fill_eepath(eempath, ((MaterialPath *)new_path)->subpath);
				eemrel = eerel_exist(((MaterialPath *)new_path)->subpath->parent);
				eempath->nsub = 1;


				eemrel->eepaths = lappend(eemrel->eepath_list, eempath);
				eepath->is_del = false;

				eempath->sub_eepath_1 = eepath_exist(((MaterialPath *) (((MaterialPath *)new_path)->subpath))->subpath);
				eempath->level = eempath->sub_eepath_1->level + 1;

				eepath->sub_eepath_1 = eepath_exist(((MaterialPath *)new_path)->subpath);
			}
			eepath->level = eepath->sub_eepath_1->level + 1;
			if (new_path->type == T_SubqueryScanPath)
				init_level = eepath->level + 1;	

			break;
		case T_NestPath:
		case T_MergePath:
		case T_HashPath:
			eepath->nsub = 2;
			eepath->sub_eepath_1 = eepath_exist(((JoinPath *)new_path)->outerjoinpath);
			eepath->sub_eepath_2 = eepath_exist(((JoinPath *)new_path)->innerjoinpath);

			if (eepath->sub_eepath_1 == NULL /* && ((((JoinPath *)new_path)->outerjoinpath->pathtype == T_Memoize) || (((JoinPath *)new_path)->outerjoinpath->pathtype == T_Material))*/)
			{
				EEPath *eempath;
				EERel *eemrel;

				eempath = init_eepath();
				fill_eepath(eepath, ((JoinPath *)new_path)->outerjoinpath);
				eemrel = eerel_exist(((JoinPath *)new_path)->outerjoinpath->parent);
				eempath->nsub = 1;

				eemrel->eepaths = lappend(eemrel->eepath_list, eempath);
				eepath->is_del = false;

				eempath->sub_eepath_1 = eepath_exist(((MaterialPath *) (((JoinPath *)new_path)->outerjoinpath))->subpath);
				eempath->level = eempath->sub_eepath_1->level + 1;

				eepath->sub_eepath_1 = eepath_exist(((JoinPath *)new_path)->outerjoinpath);
			}

			if (eepath->sub_eepath_2 == NULL /* && ((((JoinPath *)new_path)->innerjoinpath->pathtype == T_Memoize) || (((JoinPath *)new_path)->innerjoinpath->pathtype == T_Material))*/)
			{
				EEPath *eempath;
				EERel *eemrel;

				eempath = init_eepath();
				fill_eepath(eempath, ((JoinPath *)new_path)->innerjoinpath);
				eemrel = eerel_exist(((JoinPath *)new_path)->innerjoinpath->parent);
				eempath->nsub = 1;

				eemrel->eepaths = lappend(eemrel->eepath_list, eempath);
				eepath->is_del = false;

				eempath->sub_eepath_1 = eepath_exist(((MaterialPath *) (((JoinPath *)new_path)->innerjoinpath))->subpath);
				eempath->level = eempath->sub_eepath_1->level + 1;

				eepath->sub_eepath_2 = eepath_exist(((JoinPath *)new_path)->innerjoinpath);
			}

			eepath->level = Max(eepath->sub_eepath_1->level, eepath->sub_eepath_2->level) + 1;
			break;
		default:
			eepath->sub_eepath_2 = NULL;
			break;
	}

	MemoryContextSwitchTo(old_ctx);
}