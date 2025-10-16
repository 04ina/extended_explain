/*-------------------------------------------------------------------------
 *
 * extended_explain.c
 *
 *    Перехват и обработка путей
 *
 * # TODO:
 *
 * 1. В вывод добавить идентификаторы отношений и подзапросов. Это будет
 *    полезно при объединении путей и планов не по уровням, а по
 *    принадлежности к тем или иным отношениями и подзапросам. В таком
 *    случае механизм уровней можно упразднить.
 *
 * 2. Создать структуру для запросов/подзапросов. Благодаря этому можно
 *    будет упразднить глобальные списки путей и отношений в EEState.
 *    Поиск путей и отношений посредством функций search_eepath() и
 *    search_eerel() соответственно можно будет производит в локальных
 *    списках в структуре запросов/подзапросов. Также функцию
 *    insert_eerel_into_eepaths() необходимо переработать -- она должна
 *    вызываться не для конкретного отношения, а для запроса/подзапроса.
 *
 * 3. Необходим более надежный однозначный идентификатор для eepath.
 *    На данный момент при поиске eepath по соответствующему пути происходит
 *    по условию "eepath->path_pointer == path && eepath->rows == path->rows"
 *    (функция search_eepath), которое не гарантирует однозначную идентификацию.
 *
 *-------------------------------------------------------------------------
 */

#include "include/extended_explain.h"
#include "include/output_result.h"

PG_MODULE_MAGIC;

/*
 * Хуки для перехвата путей
 */
static ExplainOneQuery_hook_type prev_ExplainOneQuery_hook = NULL;
static add_path_hook_type prev_add_path_hook = NULL;
static set_rel_pathlist_hook_type prev_set_rel_pathlist_hook = NULL;

/*
 * global_ee_state сохраняет переменные расширения,
 * необходимые для обработки одного EXPLAIN запроса.
 *
 * Инициализируется каждый раз при вызове EXPLAIN.
 */
EEState    *global_ee_state = NULL;

void
_PG_init(void)
{
	prev_ExplainOneQuery_hook = ExplainOneQuery_hook;
	ExplainOneQuery_hook = ee_explain;

	prev_add_path_hook = add_path_hook;
	add_path_hook = ee_add_path_hook;

	prev_set_rel_pathlist_hook = set_rel_pathlist_hook;
	set_rel_pathlist_hook = ee_remember_rel_pathlist;
}

/* ----------------------------------------------------------------
 *				Функции для работы с global_ee_state
 * ----------------------------------------------------------------
 */

/*
 * Инициализация глобального состояния
 */
EEState *
init_ee_state(void)
{
	EEState    *ee_state;

	ee_state = (EEState *) palloc0(sizeof(EEState));

	ee_state->ctx = AllocSetContextCreate(TopMemoryContext,
										  "extended explain context",
										  ALLOCSET_DEFAULT_SIZES);

	ee_state->eerel_list = NIL;
	ee_state->eepath_list = NIL;

	ee_state->eepath_counter = 1;
	ee_state->init_level = 1;
	return ee_state;
}

/*
 * Удаление глобального состояния
 */
void
delete_ee_state(EEState * ee_state)
{
	/* а надо ли? */
	if (ee_state->eepath_list)
	{
		list_free_deep(ee_state->eepath_list);
		ee_state->eepath_list = NIL;
	}

	if (ee_state->eerel_list)
	{
		list_free_deep(ee_state->eerel_list);
		ee_state->eerel_list = NIL;
	}

	MemoryContextReset(ee_state->ctx);

	pfree(ee_state);
}

/* ----------------------------------------------------------------
 *				Функции-обработчики хуков
 * ----------------------------------------------------------------
 */

void
ee_explain(Query *query, int cursorOptions,
		   IntoClause *into, ExplainState *es,
		   const char *queryString, ParamListInfo params,
		   QueryEnvironment *queryEnv)
{
	ListCell   *br;

	global_ee_state = init_ee_state();

	standard_ExplainOneQuery(query, cursorOptions, into, es,
							 queryString, params, queryEnv);

	foreach(br, global_ee_state->eerel_list)
	{
		EERel	   *rel = (EERel *) lfirst(br);

		insert_eerel_into_eepaths(rel);
	}

	delete_ee_state(global_ee_state);
	global_ee_state = NULL;
}

/*
 * Функция для обработки хука add_path_hook.  Запоминает отношения и пути,
 * которые были рассмотрены оптимизатором при поиске наилучшего плана.
 */
void
ee_add_path_hook(RelOptInfo *parent_rel,
				 Path *new_path)
{

	EERel	   *eerel;
	MemoryContext old_ctx;

	if (global_ee_state == NULL)
		return;

	old_ctx = MemoryContextSwitchTo(global_ee_state->ctx);

	eerel = search_eerel(parent_rel);

	if (eerel == NULL)
	{
		eerel = init_eerel();
		fill_eerel(eerel, parent_rel);
	}

	create_eepath(new_path, eerel);

	MemoryContextSwitchTo(old_ctx);
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

	if (global_ee_state == NULL)
		return;

	old_ctx = MemoryContextSwitchTo(global_ee_state->ctx);

	eerel = search_eerel(rel);

	eerel->eref = makeNode(Alias);
	eerel->eref->aliasname = pstrdup(rte->eref->aliasname);
	eerel->eref->colnames = copyObject(rte->eref->colnames);

	MemoryContextSwitchTo(old_ctx);
}

/* ----------------------------------------------------------------
 *				Функции для работы с eepath
 * ----------------------------------------------------------------
 */

/*
 * Функция инициализации eepath пути
 */
EEPath *
init_eepath(void)
{
	EEPath	   *eepath = (EEPath *) palloc0(sizeof(EEPath));

	global_ee_state->eepath_list = lappend(global_ee_state->eepath_list, eepath);
	eepath->id = global_ee_state->eepath_counter++;

	return eepath;
}

/*
 * Функция поиска пути eepath, соответствующего исходному пути path.
 *
 * Если функция не нашла путь, то она вернет NULL.
 *
 * TODO:
 * 1. На данный момент поиск реализован перебором списка global_ee_state->eepath_list.
 * В будущем имеет смысл использовать более быстрый алгоритм.
 *
 * 2. Требуется более надежная идентификация путей.
 */
EEPath *
search_eepath(Path *path)
{
	ListCell   *cl;

	foreach(cl, global_ee_state->eepath_list)
	{
		EEPath	   *eepath = (EEPath *) lfirst(cl);

		if (eepath->path_pointer == path && eepath->rows == path->rows)
		{
			return eepath;
		}
	}
	return NULL;
}

/*
 * Функция заполнения пути eepath
 */
void
fill_eepath(EEPath * eepath, Path *path)
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
			/* Путь не имеет дочерних путей */
			subpath_num = 0;
			break;
		case T_SubqueryScanPath:
		case T_ProjectionPath:
		case T_LimitPath:
		case T_MaterialPath:
		case T_AggPath:
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
 * create_eepath -- функция сохранения пути Path в путь EEPath
 *
 * Для создания необходим исходный путь из планировщика и EERel -- отношение со списком eepath путей
 *
 */
EEPath *
create_eepath(Path *new_path, EERel * eerel)
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
	eepath = init_eepath();
	fill_eepath(eepath, new_path);

	/*
	 * Добавляем eepath в список путей отношения
	 * eerel
	 */
	eerel->eepath_list = lappend(eerel->eepath_list, eepath);

	/*
	 * Получаем количество возможных дочерних путей
	 */
	eepath->nsub = get_subpath_num(new_path);

	/*
	 * Связываем eepath с дочерними путями, если они есть
	 */
	switch (eepath->nsub)
	{
		case 0:					/* Дочерних путей нет */
			eepath->level = global_ee_state->init_level;
			break;
		case 1:					/* Дочерний путь один */
			/* Связываем eepath с дочерним путем */
			eepath->sub_eepath_1 = search_eepath(GET_SUB_PATH(new_path));

			/*
			 * Если мы не находим дочерний узел в
			 * списке, значит он не был обработан функцией
			 * add_path, а значит и хуком ee_remember_path.  В таком случае создаем дочерний путь
			 * отдельно.
			 */
			if (eepath->sub_eepath_1 == NULL)
				eepath->sub_eepath_1 = create_eepath(GET_SUB_PATH(new_path), NULL);

			eepath->level = eepath->sub_eepath_1->level + 1;
			if (new_path->type == T_SubqueryScanPath)
				global_ee_state->init_level = eepath->level + 1;
			break;
		case 2:					/* Два дочерних пути */
			/* Связываем eepath с дочернии путями */
			eepath->sub_eepath_1 = search_eepath(GET_OUTER_PATH(new_path));
			if (eepath->sub_eepath_1 == NULL)
				eepath->sub_eepath_1 = create_eepath(GET_OUTER_PATH(new_path), NULL);

			eepath->sub_eepath_2 = search_eepath(GET_INNER_PATH(new_path));
			if (eepath->sub_eepath_2 == NULL)
				eepath->sub_eepath_2 = create_eepath(GET_INNER_PATH(new_path), NULL);

			eepath->level = Max(eepath->sub_eepath_1->level, eepath->sub_eepath_2->level) + 1;
			break;
	}

	return eepath;
}

/* ----------------------------------------------------------------
 *				Функции для работы с eerel
 * ----------------------------------------------------------------
 */

/*
 * Функция инициализации eerel отношения.
 */
EERel *
init_eerel(void)
{
	EERel	   *eerel = (EERel *) palloc0(sizeof(EERel));

	global_ee_state->eerel_list = lappend(global_ee_state->eerel_list, eerel);
	eerel->eref = NULL;

	return eerel;
}

/*
 * Функция поиска eerel по структуре RelOptInfo
 *
 * Если функция не нашла отношение, то она вернет NULL.
 *
 * TODO:
 * Реализовать более быстрый алгоритм поиска
 */
EERel *
search_eerel(RelOptInfo *roi)
{
	ListCell   *cell;

	foreach(cell, global_ee_state->eerel_list)
	{
		EERel	   *eerel = (EERel *) lfirst(cell);

		if (eerel->roi_pointer == roi)
		{
			return eerel;
		}
	}
	return NULL;
}

/*
 * Функция заполнения отношения eerel
 *
 * roi_pointer является однозначным идентификатором eerel отношения,
 * поскольку отношения не могут удалиться в процессе перебора путей.
 *
 */
void
fill_eerel(EERel * eerel, RelOptInfo *roi)
{
	eerel->roi_pointer = roi;
}
