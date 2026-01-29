/*-------------------------------------------------------------------------
 *
 * extended_explain.c
 *
 *    Перехват и обработка путей
 *
 * # TODO:
 *
 * 1. Необходим более надежный однозначный идентификатор для eepath.
 *    На данный момент при поиске eepath по соответствующему пути происходит
 *    по условию, которое не гарантирует однозначную идентификацию: (функция search_eepath)
 *		"eepath->path_pointer == path && 
 *		 eepath->rows == path->rows && 
 *		 eepath->startup_cost == path->startup_cost && 
 *		 eepath->total_cost == path->total_cost"
 *
 *-------------------------------------------------------------------------
 */

#include "include/extended_explain.h"
#include "include/output_result.h"
#include "miscadmin.h"

#include "commands/defrem.h"

#if (PG_VERSION_NUM >= 180000)
#include "commands/explain_state.h"
#else
#include "utils/guc.h"
#endif

PG_MODULE_MAGIC;

#define STD_FUZZ_FACTOR 1.01

#if (PG_VERSION_NUM >= 180000)
typedef struct 
{
	bool		get_paths;
} extended_explain_options;

static void ee_get_paths_handler(ExplainState *es, DefElem *opt,
								 ParseState *pstate);

/*
 * Идентификатор расширения, необходим для реализации EXPLAIN параметра
 * get_paths.  Без указания данного параметра расширение работать не будет.
 */
static int	ee_extension_id;

#else
static bool get_paths;
#endif

/*
 * Хуки для перехвата путей
 */
static ExplainOneQuery_hook_type prev_ExplainOneQuery_hook = NULL;
static add_path_hook_type prev_add_path_hook = NULL;
static set_rel_pathlist_hook_type prev_set_rel_pathlist_hook = NULL;
static create_upper_paths_hook_type prev_create_upper_paths_hook = NULL;

/*
 * global_ee_state сохраняет переменные расширения,
 * необходимые для обработки одного EXPLAIN запроса.
 *
 * Инициализируется каждый раз при вызове EXPLAIN.
 */
static EEState    		*global_ee_state = NULL;
static MemoryContext 	ee_ctx;

static PathCostComparison	compare_path_costs_fuzzily(Path *path1, 
													   Path *path2, 
													   double fuzz_factor);
static PathRowsComparison	compare_rows(Cardinality a, Cardinality b);
static PathParallelSafeComparison	compare_parallel_safe(bool a, bool b);
static void	mark_old_path_displaced(EEPath *old_eepath, EEPath *new_eepath, 
									PathCostComparison costcmp, 
									double fuzz_factor,
									PathKeysComparison keyscmp, 
									BMS_Comparison outercmp, 
									PathRowsComparison rowscmp, 
									PathParallelSafeComparison parallel_safe_cmp);
static void	mark_new_path_removed(EEPath *new_eepath);


void
_PG_init(void)
{

	/*
	 * Расширение использует механизм EXPLAIN опций для включения/выключения
	 * режима сбора всех путей, однако появился данный механизм с 18 версии PostgreSQL.
	 * На более старых версиях режим сбора всех путей можно включить посредством 
	 * GUC переменной.
	 * 
	 */
	#if (PG_VERSION_NUM >= 180000)

	/*
	 * Получаем Id расширения и регистрируем get_paths параметр для EXPLAIN.
	 */
	ee_extension_id = GetExplainExtensionId("extended_explain");
	RegisterExtensionExplainOption("get_paths", ee_get_paths_handler);

	#else 

	/*
	 * Определяем GUC переменную get_paths
	 */
    DefineCustomBoolVariable(
        "ee.get_paths",
        "Enable or disable path collection.",
        NULL,
        &get_paths,
        false,
        PGC_USERSET,
        GUC_NOT_IN_SAMPLE,
        NULL,
		NULL,
		NULL);    

	#endif

	prev_ExplainOneQuery_hook = ExplainOneQuery_hook;
	ExplainOneQuery_hook = ee_explain;

	prev_add_path_hook = add_path_hook;
	add_path_hook = ee_add_path_hook;

	prev_set_rel_pathlist_hook = set_rel_pathlist_hook;
	set_rel_pathlist_hook = ee_remember_rel_pathlist;

	prev_create_upper_paths_hook = create_upper_paths_hook;
	create_upper_paths_hook = ee_process_upper_paths;
}

#if (PG_VERSION_NUM >= 180000)
/*
 * Функция-обработчик параметра get_paths для EXPLAIN
 */
static void 
ee_get_paths_handler(ExplainState *es, DefElem *opt,
								 ParseState *pstate)
{
	extended_explain_options *options = GetExplainExtensionState(es, ee_extension_id);

	if (options == NULL)
	{
		options = palloc0(sizeof(extended_explain_options));
		SetExplainExtensionState(es, ee_extension_id, options);
	}

	options->get_paths = defGetBoolean(opt);
}
#endif

/*
 * Функция получения значения параметра get_paths.
 */
static bool
get_add_paths_setting(struct ExplainState *es)
{
#if (PG_VERSION_NUM >= 180000)
	extended_explain_options *options;

	options = GetExplainExtensionState(es, ee_extension_id);

	return !(options == NULL || !options->get_paths);
#else
	return get_paths;
#endif
}

/* ----------------------------------------------------------------
 *				Функции для работы с global_ee_state
 * ----------------------------------------------------------------
 */

/*
 * Инициализация глобального состояния
 */
EEState *
create_ee_state(void)
{
	EEState	*ee_state;
	HASHCTL	ctl;
	MemoryContext old_ctx;

	old_ctx = MemoryContextSwitchTo(ee_ctx);

	ee_state = (EEState *) palloc0(sizeof(EEState));

	ee_state->eesubquery_list = NIL;

	ee_state->eepath_counter = 1;
	ee_state->eerel_counter = 1;
	ee_state->eesubquery_counter = 1;

	memset(&ctl, 0, sizeof(HASHCTL));
	ctl.keysize = sizeof(uintptr_t);
	ctl.entrysize = sizeof(EERelHashEntry);
	ctl.hcxt = ee_ctx;
	ee_state->eerel_by_roi = hash_create("EERel by RelOptInfo*", 32, &ctl, HASH_ELEM | HASH_CONTEXT | HASH_BLOBS);

	memset(&ctl, 0, sizeof(HASHCTL));
	ctl.keysize = sizeof(uintptr_t) + sizeof(Cardinality) + 2 * sizeof(Cost);
	ctl.entrysize = sizeof(EEPathHashEntry);
	ctl.hcxt = ee_ctx;
	ee_state->eepath_by_path = hash_create("EEPath by path*", 32, &ctl, HASH_ELEM | HASH_CONTEXT | HASH_BLOBS);

	MemoryContextSwitchTo(old_ctx);

	return ee_state;
}

/* ----------------------------------------------------------------
 *				Функции-обработчики хуков
 * ----------------------------------------------------------------
 */

/*
 * Функция-обработчик хука add_path_hook
 *
 * В данном обработчике сохранена логика функции add_path, 
 * а также добавлен механизм сохранения путей.
 */
void
ee_add_path_hook(RelOptInfo *parent_rel,
				 Path *new_path)
{
	bool		accept_new = true;	/* unless we find a superior old path */
	List	   *new_path_pathkeys;
	ListCell   *p1;

	EERel	   *eerel;
	EEPath	   *new_eepath;
	EEPath 	   *old_eepath;
	MemoryContext old_ctx;

	if (global_ee_state != NULL)
	{
		old_ctx = MemoryContextSwitchTo(ee_ctx);

		if (global_ee_state->cached_current_rel == parent_rel)
		{
			/*
			* Нужное EERel отношение сохранилось в кэше.
			*/
			eerel = global_ee_state->cached_current_eerel;
		}
		else
		{
			/*
			* Нужного EERel отношения не оказалось в кэше, значит ищем его 
			* в списке отношений текущего подзапроса.
			*/
			eerel = search_eerel(parent_rel);
			if (eerel == NULL)
			{
				/*
				* Если отношение не было найдено, его необходимо создать 
				*/
				eerel = create_eerel(parent_rel);
			}

			/* Обновляем кэш */
			global_ee_state->cached_current_rel = parent_rel;
			global_ee_state->cached_current_eerel = eerel;
		}

		/*
		* Создаем eepath по new_path. 
		* 
		* По умолчанию путь считается сохраненным в pathlist (add_path_result = APR_SAVED), 
		* однако в процессе работы функции add_path данное состояние может измениться.
		*/
		new_eepath = record_eepath(eerel, new_path);

		MemoryContextSwitchTo(old_ctx);

		new_path_pathkeys = new_path->param_info ? NIL : new_path->pathkeys;

		foreach(p1, parent_rel->pathlist)
		{
			Path		*old_path = (Path *) lfirst(p1);
			bool		remove_old = false; /* unless new proves superior */
			double		fuzz_factor = STD_FUZZ_FACTOR;

			PathCostComparison costcmp;
			PathKeysComparison keyscmp;
			BMS_Comparison outercmp;	

			
			costcmp = compare_path_costs_fuzzily(new_path, old_path,
												fuzz_factor);

			if (costcmp != COSTS_DIFFERENT)
			{
				List	   *old_path_pathkeys;

				old_path_pathkeys = old_path->param_info ? NIL : old_path->pathkeys;
				keyscmp = compare_pathkeys(new_path_pathkeys,
										old_path_pathkeys);
				if (keyscmp != PATHKEYS_DIFFERENT)
				{
					switch (costcmp)
					{
						case COSTS_EQUAL:
							outercmp = bms_subset_compare(PATH_REQ_OUTER(new_path),
														PATH_REQ_OUTER(old_path));
							if (keyscmp == PATHKEYS_BETTER1)
							{
								if ((outercmp == BMS_EQUAL ||
									outercmp == BMS_SUBSET1) &&
									new_path->rows <= old_path->rows &&
									new_path->parallel_safe >= old_path->parallel_safe)
									remove_old = true;	/* new dominates old */
							}
							else if (keyscmp == PATHKEYS_BETTER2)
							{
								if ((outercmp == BMS_EQUAL ||
									outercmp == BMS_SUBSET2) &&
									new_path->rows >= old_path->rows &&
									new_path->parallel_safe <= old_path->parallel_safe)
									accept_new = false; /* old dominates new */
							}
							else	/* keyscmp == PATHKEYS_EQUAL */
							{
								if (outercmp == BMS_EQUAL)
								{
									if (new_path->parallel_safe >
										old_path->parallel_safe)
										remove_old = true;	/* new dominates old */
									else if (new_path->parallel_safe <
											old_path->parallel_safe)
										accept_new = false; /* old dominates new */
									else if (new_path->rows < old_path->rows)
										remove_old = true;	/* new dominates old */
									else if (new_path->rows > old_path->rows)
										accept_new = false; /* old dominates new */
									else
									{
										fuzz_factor = 1.0000000001;

										costcmp = compare_path_costs_fuzzily(new_path,
																			old_path,
																			fuzz_factor);

										if (costcmp == DISABLED_NODES_BETTER1 || 
											costcmp == TOTAL_AND_STARTUP_BETTER1 || 
											costcmp == TOTAL_EQUAL_STARTUP_BETTER1)
											remove_old = true;	/* new dominates old */
										else 
											accept_new = false; /* old equals or dominates new */
									}
								}
								else if (outercmp == BMS_SUBSET1 &&
										new_path->rows <= old_path->rows &&
										new_path->parallel_safe >= old_path->parallel_safe)
									remove_old = true;	/* new dominates old */
								else if (outercmp == BMS_SUBSET2 &&
										new_path->rows >= old_path->rows &&
										new_path->parallel_safe <= old_path->parallel_safe)
									accept_new = false; /* old dominates new */
								/* else different parameterizations, keep both */
							}
							break;
						case DISABLED_NODES_BETTER1:
						case TOTAL_AND_STARTUP_BETTER1:
						case TOTAL_EQUAL_STARTUP_BETTER1:
							if (keyscmp != PATHKEYS_BETTER2)
							{
								outercmp = bms_subset_compare(PATH_REQ_OUTER(new_path),
															PATH_REQ_OUTER(old_path));
								if ((outercmp == BMS_EQUAL ||
									outercmp == BMS_SUBSET1) &&
									new_path->rows <= old_path->rows &&
									new_path->parallel_safe >= old_path->parallel_safe)
									remove_old = true;	/* new dominates old */
							}
							break;
						case DISABLED_NODES_BETTER2:
						case TOTAL_AND_STARTUP_BETTER2:
						case TOTAL_EQUAL_STARTUP_BETTER2:
							if (keyscmp != PATHKEYS_BETTER1)
							{
								outercmp = bms_subset_compare(PATH_REQ_OUTER(new_path),
															PATH_REQ_OUTER(old_path));
								if ((outercmp == BMS_EQUAL ||
									outercmp == BMS_SUBSET2) &&
									new_path->rows >= old_path->rows &&
									new_path->parallel_safe <= old_path->parallel_safe)
									accept_new = false; /* old dominates new */
							}
							break;
						case COSTS_DIFFERENT:
							break;
					}
				}
			}

			/*
			* Remove current element from pathlist if dominated by new.
			*/
			if (remove_old)
			{
				old_eepath = search_eepath(old_path);

				/*
				* Добавляем в old_eepath информацию о вытеснении.
				*/
				mark_old_path_displaced(old_eepath, new_eepath, costcmp, fuzz_factor, keyscmp, outercmp, 
										compare_rows(new_path->rows, old_path->rows),
										compare_parallel_safe(new_path->parallel_safe, old_path->parallel_safe));
			}

			if (!accept_new)
				break;
		}

		if (!accept_new)
		{
			/*
			* Путь new_path не попал в pathlist.  Ставим соответствующую пометку APR_REMOVED в new_eepath
			*/
			mark_new_path_removed(new_eepath);
		}
	}

	/* Pass call to previous hook. */
	if (prev_add_path_hook)
		(*prev_add_path_hook) (parent_rel, new_path);
}

/*
 * Функция-обработчик хука ExplainOneQuery_hook
 */
void
ee_explain(Query *query, int cursorOptions,
		   IntoClause *into, struct ExplainState *es,
		   const char *queryString, ParamListInfo params,
		   QueryEnvironment *queryEnv)
{
	if (get_add_paths_setting(es))
	{
		int64		query_id;

		ee_ctx = AllocSetContextCreate(TopMemoryContext,
									   "extended explain context",
									   ALLOCSET_DEFAULT_SIZES);

		global_ee_state = create_ee_state();

		init_eesubquery();

		standard_ExplainOneQuery(query, cursorOptions, into, es,
								queryString, params, queryEnv);

		query_id = insert_query_info_into_eequery(queryString);

		insert_paths_into_eepaths(query_id, global_ee_state);

		MemoryContextReset(ee_ctx);

		global_ee_state = NULL;
	}
	else
	{
		standard_ExplainOneQuery(query, cursorOptions, into, es,
								queryString, params, queryEnv);
	}
}

/*
 * Функция для обработки хука set_rel_pathlist_hook
 *
 * Вызывается при окончании заполнения списка путей базового отношения,
 * необходима для получения названия отношения (например таблица или результат соединения).
 *
 * Во время сохранения путей название отношения не получить, поскольку из функции add_path()
 * невозможно выудить RangeTblEntry.
 */
void
ee_remember_rel_pathlist(PlannerInfo *root,
						 RelOptInfo *rel,
						 Index rti,
						 RangeTblEntry *rte)
{
	MemoryContext old_ctx;
	EERel	   *eerel;

	if (global_ee_state != NULL)
	{
		old_ctx = MemoryContextSwitchTo(ee_ctx);

		if (global_ee_state->cached_current_rel == rel)
			eerel = global_ee_state->cached_current_eerel; 
		else
			eerel = search_eerel(rel);

		eerel->name = get_rel_name(rte->relid);

		if (rte->alias != NULL)
			eerel->alias = pstrdup(rte->alias->aliasname);

		MemoryContextSwitchTo(old_ctx);
	}

	/* Pass call to previous hook. */
	if (prev_set_rel_pathlist_hook)
		(*prev_set_rel_pathlist_hook) (root, rel, rti, rte);
}

/*
 * Функция для обработки хука create_upper_paths_hook
 */
void
ee_process_upper_paths(PlannerInfo *root,
					   UpperRelationKind stage,
					   RelOptInfo *input_rel,
					   RelOptInfo *output_rel,
					   void *extra)
{

	if (global_ee_state != NULL && stage == UPPERREL_FINAL)
	{
		/* Указываем query_level для текущего eesubquery */
		global_ee_state->current_eesubquery->subquery_level = root->query_level;

		/* Инициализируем следующий eesubquery */
		init_eesubquery();
	}

	/* Pass call to previous hook. */
	if (prev_create_upper_paths_hook)
		(*prev_create_upper_paths_hook) (root, stage, input_rel, output_rel, extra);
}

/* ----------------------------------------------------------------
 *				Функции для работы с eepath
 * ----------------------------------------------------------------
 */

/*
 * Функция создания eepath пути
 */
EEPath *
create_eepath(Path *path, EERel *eerel)
{
	EEPathHashEntry *entry;
	EEPathHashKey 	key;

	EEPath	   *eepath = (EEPath *) palloc0(sizeof(EEPath));

	eepath->id = global_ee_state->eepath_counter++;

	eepath->path_pointer = path;
	eepath->pathtype = path->pathtype;

	eepath->sub_eepath_1 = NULL;
	eepath->sub_eepath_2 = NULL;

	eepath->rows = path->rows;
	eepath->startup_cost = path->startup_cost;
	eepath->total_cost = path->total_cost;

	eepath->add_path_result = APR_SAVED;

	if (path->type == T_IndexPath)
		eepath->indexoid = ((IndexPath *) path)->indexinfo->indexoid;
	else
		eepath->indexoid = 0;

	eerel->eepath_list = lappend(eerel->eepath_list, eepath);

    key.path_ptr = path;
	key.rows = path->rows;
	key.startup_cost = path->startup_cost;
	key.total_cost = path->total_cost;
    entry = (EEPathHashEntry *) hash_search(global_ee_state->eepath_by_path,
										   &key,
        								   HASH_ENTER,
        								   NULL);
	
	entry->eepath = eepath;

	return eepath;
}

/*
 * Функция поиска пути eepath, соответствующего исходному пути path.
 *
 * Если функция не нашла путь, то она вернет NULL.
 *
 * TODO:
 *	Требуется более надежная идентификация путей.
 */
EEPath *
search_eepath(Path *path)
{
	EEPathHashEntry *entry;
	EEPathHashKey 	key;

    key.path_ptr = path;
	key.rows = path->rows;
	key.startup_cost = path->startup_cost;
	key.total_cost = path->total_cost;

	entry = (EEPathHashEntry *) hash_search(global_ee_state->eepath_by_path,
										   &key,
										   HASH_FIND,
										   NULL);

	if (entry)
		return entry->eepath;
	else
		return NULL;
}

/*
 * Функция получения количества возможных дочерних путей у указанного пути.
 */
int
get_subpath_num(Path *path)
{
	int			subpath_num;

	switch (nodeTag(path))
	{
		case T_IndexPath:
		case T_Path:
		case T_BitmapHeapPath:
		case T_AppendPath: /* ????? */
			/* Путь не имеет дочерних путей */
			subpath_num = 0;
			break;
		case T_SubqueryScanPath:
		case T_ProjectionPath:
		case T_LimitPath:
		case T_MaterialPath:
		case T_AggPath:
		case T_GatherPath:
		case T_SortPath:
		case T_GatherMergePath:
		case T_UpperUniquePath:
		case T_WindowAggPath:
		case T_IncrementalSortPath:
			/* Путь имеет один дочерний путь */
			subpath_num = 1;
			break;
		case T_NestPath:
		case T_MergePath:
		case T_HashPath:
			/* Путь имеет два дочерних пути */
			subpath_num = 2;
			break;
		default:
			Assert(false);
			subpath_num = -1;
			break;
	}

	return subpath_num;
}

/*
 * record_eepath -- функция сохранения пути Path в путь EEPath
 *
 * Для создания необходим исходный путь из планировщика и EERel -- отношение со списком eepath путей
 */
EEPath *
record_eepath(EERel * eerel, Path *new_path)
{
	EEPath	   *eepath;

	/*
	 * Находим соответствующее EERel отношение, если оно не было указано в качестве аргумента
	 *
	 * Предполагается, что если eerel не указан, то он уже был создан
	 * ранее.
	 */
	if (eerel == NULL)
		eerel = search_eerel(new_path->parent);

	/*
	 * Инициализируем и заполняем eepath характеристиками пути
	 * new_path
	 */
	eepath = create_eepath(new_path, eerel);

	/*
	 * Получаем количество возможных дочерних путей
	 */
	eepath->nsub = get_subpath_num(new_path);

	/*
	 * Связываем eepath с дочерними путями, если они есть
	 */
	if (eepath->nsub == 1)		/* Дочерний путь один */
	{
		EERel *sub_eerel;

		sub_eerel = search_eerel(GET_SUB_PATH(new_path)->parent);

		/* Связываем eepath с дочерним путем */
		eepath->sub_eepath_1 = search_eepath(GET_SUB_PATH(new_path));

		/*
		 * Если мы не находим дочерний узел в списке, 
		 * значит он не был обработан функцией add_path, 
		 * а значит и хуком ee_add_path_hook.  
		 * 
		 * В таком случае создаем дочерний путь отдельно.
		 */
		if (eepath->sub_eepath_1 == NULL)
			eepath->sub_eepath_1 = record_eepath(sub_eerel, GET_SUB_PATH(new_path));
	}
	else if (eepath->nsub == 2)	/* Два дочерних пути */
	{
		EERel *outer_eerel;
		EERel *inner_eerel;

		outer_eerel = search_eerel(GET_OUTER_PATH(new_path)->parent);

		inner_eerel = search_eerel(GET_INNER_PATH(new_path)->parent);

		/* Связываем eepath с дочернии путями */
		eepath->sub_eepath_1 = search_eepath(GET_OUTER_PATH(new_path));
		if (eepath->sub_eepath_1 == NULL)
			eepath->sub_eepath_1 = record_eepath(outer_eerel, GET_OUTER_PATH(new_path));

		eepath->sub_eepath_2 = search_eepath(GET_INNER_PATH(new_path));
		if (eepath->sub_eepath_2 == NULL)
			eepath->sub_eepath_2 = record_eepath(inner_eerel, GET_INNER_PATH(new_path));
	}

	return eepath;
}

/* ----------------------------------------------------------------
 *				Функции для работы с eerel
 * ----------------------------------------------------------------
 */

/*
 * Функция создания eerel отношения.
 */
EERel *
create_eerel(RelOptInfo *roi)
{
	EERelHashEntry 	*entry;
	EERel	   *eerel = (EERel *) palloc0(sizeof(EERel));

	eerel->roi_pointer = roi;
	eerel->width = roi->reltarget->width;
	eerel->id = global_ee_state->eerel_counter++;
	eerel->joined_rel_num = bms_num_members(roi->relids);

	eerel->name = NULL;
	eerel->alias = NULL;

	global_ee_state->current_eesubquery->eerel_list = lappend(global_ee_state->current_eesubquery->eerel_list, eerel);

    entry = (EERelHashEntry *) hash_search(global_ee_state->eerel_by_roi,
										   (void *) &eerel->roi_pointer,
        								   HASH_ENTER,
        								   NULL);

    entry->eerel = eerel;

	return eerel;
}

/*
 * Функция поиска eerel по структуре RelOptInfo
 *
 * Если функция не нашла отношение, то она вернет NULL.
 */
EERel *
search_eerel(RelOptInfo *roi)
{
	EERelHashEntry 	*entry;
	
	entry = (EERelHashEntry *) hash_search(global_ee_state->eerel_by_roi,
										   &roi,
										   HASH_FIND,
										   NULL);

	if (entry)
		return entry->eerel;
	else
		return NULL;
}

/* ----------------------------------------------------------------
 *				Функции для работы с eesubquery
 * ----------------------------------------------------------------
 */

/*
 * Функция инициализации eesubquery
 */
void
init_eesubquery(void)
{
	EESubQuery		*eesubquery;
	MemoryContext 	old_ctx;

	old_ctx = MemoryContextSwitchTo(ee_ctx);

	eesubquery = (EESubQuery *) palloc0(sizeof(EESubQuery));

	global_ee_state->current_eesubquery = eesubquery;
	global_ee_state->eesubquery_list = lappend(global_ee_state->eesubquery_list, 
											   eesubquery);

	eesubquery->eerel_list = NIL;
	eesubquery->id = global_ee_state->eesubquery_counter++;

	MemoryContextSwitchTo(old_ctx);
}

/* ----------------------------------------------------------------
 *				 Остальные функции
 * ----------------------------------------------------------------
 */

/*
 * Дубликат статической функции compare_path_costs_fuzzily из исходного кода postgres
 */
static PathCostComparison
compare_path_costs_fuzzily(Path *path1, Path *path2, double fuzz_factor)
{
#define CONSIDER_PATH_STARTUP_COST(p)  \
	((p)->param_info == NULL ? (p)->parent->consider_startup : (p)->parent->consider_param_startup)

	/* Number of disabled nodes, if different, trumps all else. */
	if (unlikely(path1->disabled_nodes != path2->disabled_nodes))
	{
		if (path1->disabled_nodes < path2->disabled_nodes)
			return DISABLED_NODES_BETTER1;
		else
			return DISABLED_NODES_BETTER2;
	}

	/*
	 * Check total cost first since it's more likely to be different; many
	 * paths have zero startup cost.
	 */
	if (path1->total_cost > path2->total_cost * fuzz_factor)
	{
		/* path1 fuzzily worse on total cost */
		if (CONSIDER_PATH_STARTUP_COST(path1) &&
			path2->startup_cost > path1->startup_cost * fuzz_factor)
		{
			/* ... but path2 fuzzily worse on startup, so DIFFERENT */
			return COSTS_DIFFERENT;
		}
		/* else path2 dominates */
		return TOTAL_AND_STARTUP_BETTER2;
	}
	if (path2->total_cost > path1->total_cost * fuzz_factor)
	{
		/* path2 fuzzily worse on total cost */
		if (CONSIDER_PATH_STARTUP_COST(path2) &&
			path1->startup_cost > path2->startup_cost * fuzz_factor)
		{
			/* ... but path1 fuzzily worse on startup, so DIFFERENT */
			return COSTS_DIFFERENT;
		}
		/* else path1 dominates */
		return TOTAL_AND_STARTUP_BETTER1;
	}
	/* fuzzily the same on total cost ... */
	if (path1->startup_cost > path2->startup_cost * fuzz_factor)
	{
		/* ... but path1 fuzzily worse on startup, so path2 wins */
		return TOTAL_EQUAL_STARTUP_BETTER2; 
	}
	if (path2->startup_cost > path1->startup_cost * fuzz_factor)
	{
		/* ... but path2 fuzzily worse on startup, so path1 wins */
		return TOTAL_EQUAL_STARTUP_BETTER1; 
	}
	/* fuzzily the same on both costs */
	return COSTS_EQUAL;

#undef CONSIDER_PATH_STARTUP_COST
}

/*
 * Сравнение количества строк 
 */
static PathRowsComparison
compare_rows(Cardinality a, Cardinality b)
{
	if (a > b)
		return ROWS_BETTER2;
	else if (a < b)
		return ROWS_BETTER1;
	else 
		return ROWS_EQUAL;
}

/*
 * Сравнение поддержки параллельной безопасности 
 */
static PathParallelSafeComparison
compare_parallel_safe(bool a, bool b)
{
	if (a > b)
		return PARALLEL_SAFE_BETTER1;
	else if (a < b)
		return PARALLEL_SAFE_BETTER2;
	else 
		return PARALLEL_SAFE_EQUAL;
}

/*
 * Отметить старый путь как замещенный
 */
static void
mark_old_path_displaced(EEPath *old_eepath, EEPath *new_eepath, 
						PathCostComparison costcmp, 
						double fuzz_factor,
						PathKeysComparison keyscmp, 
						BMS_Comparison outercmp, 
						PathRowsComparison rowscmp, 
						PathParallelSafeComparison parallel_safe_cmp)
{
	old_eepath->add_path_result = APR_DISPLACED;
	old_eepath->displaced_by = new_eepath->id;
	old_eepath->fuzz_factor = fuzz_factor;

	old_eepath->cost_cmp = costcmp;
	old_eepath->pathkeys_cmp = keyscmp;
	old_eepath->bms_cmp = outercmp;
	old_eepath->rows_cmp = rowscmp;
	old_eepath->parallel_safe_cmp = parallel_safe_cmp;

}

/*
 * Пометить новый путь как удаленный 
 */
static void
mark_new_path_removed(EEPath *new_eepath)
{
	new_eepath->add_path_result = APR_REMOVED;
}