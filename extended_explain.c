/*-------------------------------------------------------------------------
 *
 * extended_explain.c
 *
 *-------------------------------------------------------------------------
 */

#include "include/extended_explain.h"
#include "include/output_result.h"

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
        list_free_deep(ee_status->eerel_list);
        ee_status->eerel_list = NIL;
    }
    
    if (ee_status->eerel_list)
    {
        list_free_deep(ee_status->eerel_list);
        ee_status->eepath_list = NIL;
    }

    if (ee_status->eesubquery_list)
    {
        list_free_deep(ee_status->eesubquery_list);
        ee_status->eepath_list = NIL;
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
	ListCell *br;

	global_ee_status = init_ee_status();

	standard_ExplainOneQuery(query, cursorOptions, into, es,
							 queryString, params, queryEnv);

/*
    if (es->format == EXPLAIN_FORMAT_TEXT) 
	{
		appendStringInfo(es->str, "\n-- Additional Paths Info --\n");
		appendStringInfo(es->str, "Total scan paths: %d\n", 4);
		appendStringInfo(es->str, "Total join paths: %d\n", 4);
		appendStringInfoString(es->str, "Planning:\n");

	}
*/

	foreach(br, global_ee_status->eerel_list)
	{
		EERel *rel = (EERel *) lfirst(br); 

		FillPathsTable(rel);
	}

	delete_ee_status(global_ee_status);
	global_ee_status = NULL;
}

PG_FUNCTION_INFO_V1(ee_func);

Datum
ee_func(PG_FUNCTION_ARGS)
{
	PG_RETURN_BOOL(true);
}

EERel *eerel_exist(RelOptInfo *roi);
EEPath *eepath_exist(Path *path);
EERel *init_eerel(void);
EEPath *init_eepath(void);
void fill_eerel(EERel *eerel, RelOptInfo *roi);
void  fill_eepath(EEPath *eepath, Path *path);

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
		if (eepath->path_pointer == path && eepath->rows == path->rows)
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
	eepath->id = global_ee_status->eepath_counter++;

	return eepath;
}

void 
fill_eerel(EERel *eerel, RelOptInfo *roi)
{
	eerel->roi_pointer = roi;
	eerel->eref = NULL;

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

	if (path->type == T_IndexPath)
		eepath->indexoid = ((IndexPath *) path)->indexinfo->indexoid;
	else
		eepath->indexoid = 0;

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
	if (global_ee_status == NULL)	
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
	MemoryContext old_ctx;
	EERel *eerel;

	if (global_ee_status == NULL)	
		return;

	old_ctx = MemoryContextSwitchTo(global_ee_status->ctx);

	eerel = eerel_exist(rel);

	eerel->eref = makeNode(Alias);
	eerel->eref->aliasname = pstrdup(rte->eref->aliasname);
	eerel->eref->colnames = copyObject(rte->eref->colnames);

	MemoryContextSwitchTo(old_ctx);
}

void
ee_get_upper_paths_hook(PlannerInfo *root,
						UpperRelationKind stage,
						RelOptInfo *input_rel,
						RelOptInfo *output_rel,
						void *extra)
{
	if (global_ee_status == NULL)	
		return;

	/*
	if (stage != UPPERREL_FINAL)
		return;
	*/

}
/*
void
create_unvisible_path(EEPath *eepath, EEPath *sub_eepath,  Path *path)
{
	EEPath *unvisible_eepath;
	EERel *unvisible_eerel;

	unvisible_eepath = init_eepath();
	fill_eepath(eepath, path);
	unvisible_eerel = eerel_exist(path->parent);
	unvisible_eepath->nsub = 1;

	unvisible_eerel->eepath_list = lappend(unvisible_eerel->eepath_list, unvisible_eepath);
	unvisible_eepath->is_del = false;

	unvisible_eepath->sub_eepath_1 = eepath_exist(((MaterialPath *) path)->subpath);
	unvisible_eepath->level = unvisible_eepath->sub_eepath_1->level + 1;

	sub_eepath = eepath_exist(path);
}
*/

void
ee_remember_path(RelOptInfo *parent_rel, 
				 Path *new_path,
				 bool accept_new,
				 int insert_at)
{
	EERel *eerel;
	EEPath *eepath;
	MemoryContext old_ctx;

	if (global_ee_status == NULL)
		return;
/*
	if (eepath_exist(new_path) != NULL)
		return;
*/
	old_ctx = MemoryContextSwitchTo(global_ee_status->ctx);

	eerel = eerel_exist(parent_rel);

	if (eerel == NULL)
	{
		eerel = init_eerel();	
		fill_eerel(eerel, parent_rel);
	}

	eepath = init_eepath();
	fill_eepath(eepath, new_path);
	eerel->eepath_list = lappend(eerel->eepath_list, eepath);
	eepath->is_del = !accept_new;

	switch (new_path->type)
	{
		case T_IndexPath:
		case T_Path:
		case T_BitmapHeapPath:
			eepath->nsub = 0;
			eepath->level = global_ee_status->init_level;

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


				eemrel->eepath_list = lappend(eemrel->eepath_list, eempath);
				eepath->is_del = false;

				eempath->sub_eepath_1 = eepath_exist(((MaterialPath *) (((MaterialPath *)new_path)->subpath))->subpath);
				eempath->level = eempath->sub_eepath_1->level + 1;

				eepath->sub_eepath_1 = eepath_exist(((MaterialPath *)new_path)->subpath);
			}
			eepath->level = eepath->sub_eepath_1->level + 1;
			if (new_path->type == T_SubqueryScanPath)
				global_ee_status->init_level = eepath->level + 1;	

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

				eemrel->eepath_list = lappend(eemrel->eepath_list, eempath);
				eepath->is_del = false;

				eempath->sub_eepath_1 = eepath_exist(((MaterialPath *) (((JoinPath *)new_path)->outerjoinpath))->subpath);
				eempath->level = eempath->sub_eepath_1->level + 1;

				eepath->sub_eepath_1 = eepath_exist(((JoinPath *)new_path)->outerjoinpath);

				//create_unvisible_path(eepath->sub_eepath_1, ((JoinPath *)new_path)->outerjoinpath);
				//create_unvisible_path(eepath, eepath->sub_eepath_1, ((JoinPath *)new_path)->outerjoinpath);
			}

			if (eepath->sub_eepath_2 == NULL /* && ((((JoinPath *)new_path)->innerjoinpath->pathtype == T_Memoize) || (((JoinPath *)new_path)->innerjoinpath->pathtype == T_Material))*/)
			{
				EEPath *eempath;
				EERel *eemrel;

				eempath = init_eepath();
				fill_eepath(eempath, ((JoinPath *)new_path)->innerjoinpath);
				eemrel = eerel_exist(((JoinPath *)new_path)->innerjoinpath->parent);
				eempath->nsub = 1;

				eemrel->eepath_list = lappend(eemrel->eepath_list, eempath);
				eepath->is_del = false;

				eempath->sub_eepath_1 = eepath_exist(((MaterialPath *) (((JoinPath *)new_path)->innerjoinpath))->subpath);
				eempath->level = eempath->sub_eepath_1->level + 1;

				eepath->sub_eepath_2 = eepath_exist(((JoinPath *)new_path)->innerjoinpath);

				//create_unvisible_path(eepath->sub_eepath_2, ((JoinPath *)new_path)->outerjoinpath);
			}

			eepath->level = Max(eepath->sub_eepath_1->level, eepath->sub_eepath_2->level) + 1;
			break;
		default:
			eepath->sub_eepath_2 = NULL;
			break;
	}

	MemoryContextSwitchTo(old_ctx);
}

//create_unvisible_path(eepath->sub_eepath_1, ((JoinPath *)new_path)->outerjoinpath)
