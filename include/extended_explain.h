/*-------------------------------------------------------------------------
 *
 * extended_explain.h
 *
 * IDENTIFICATION
 *        include/extended_explain.h
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
#include "optimizer/paths.h"
#include "commands/explain.h"
#include "optimizer/planmain.h"
#include "optimizer/pathnode.h"
#include "optimizer/planner.h"

typedef enum
{
	APR_SAVED,
	APR_DISPLACED, 
	APR_REMOVED,
} AddPathResult;

typedef enum
{
	ROWS_EQUAL,
	ROWS_BETTER1,
	ROWS_BETTER2,
} PathRowsComparison;

typedef enum
{
	PARALLEL_SAFE_EQUAL,
	PARALLEL_SAFE_BETTER1,
	PARALLEL_SAFE_BETTER2,
} PathParallelSafeComparison;

typedef enum
{
	COSTS_EQUAL,
	DISABLED_NODES_BETTER1,
	DISABLED_NODES_BETTER2,
	TOTAL_AND_STARTUP_BETTER1,
	TOTAL_AND_STARTUP_BETTER2,
	TOTAL_EQUAL_STARTUP_BETTER1,
	TOTAL_EQUAL_STARTUP_BETTER2,
	COSTS_DIFFERENT,
} PathCostComparison;

/*
 * EEPath -- информация об исходном пути
 *
 * Причем наибольшую сложность при заполнении представляет связывание родительского
 * пути с дочерними. Исходный родительский путь содержит указатели на дочерние
 * пути, а для связи соответствующих eepath путей необходимо некое однозначное
 * соответствие между исходным дочерним путем и дочерним eepath путем.
 *
 */
typedef struct EEPath
{
	/*
	 * Указатель на структуру исходного пути.
	 *
	 * Является частью составного однозначного
	 * идентификатора, который позволяет по исходному пути определить соответствующий
	 * eepath.
	 */
	Path	   *path_pointer;

	/*
	 * Однозначный идентификатор eepath	пути в пределах одного
	 * EXPLAIN запроса.
	 */
	int64		id;

	/* Список eepath путей */
	List	   *eepath_list;

	/*
	 * Тип пути, наследуемый от исходного пути
	 */
	NodeTag		pathtype;

	/* Количество дочерних путей */
	int			nsub;

	/* Указатели на дочерние пути */
	struct EEPath *sub_eepath_1;
	struct EEPath *sub_eepath_2;

	/* Стоимости и кардинальность */
	Cardinality rows;
	Cost		startup_cost;
	Cost		total_cost;

	/*
	 * Oid индекса, который был использован при чтении таблицы
	 */
	Oid			indexoid;

	/*
	 * Результат работы add_path
	 */
	AddPathResult			add_path_result;	// Результат фильтрации на уровне функции add_path (SAVED, DISPLACED, REMOVED)
	int64 					displaced_by;		// id пути, который вытеснил данный путь

	/* Результаты сравнения при вытеснении */
	PathCostComparison		cost_cmp;			
	double					fuzz_factor;
	PathKeysComparison		pathkeys_cmp;
	BMS_Comparison			bms_cmp;
	PathRowsComparison		rows_cmp;
	PathParallelSafeComparison parallel_safe_cmp;

}			EEPath;

/*
 * Информация об исходном отношении.
 *
 * Заполняется посредством add_path_hook, поскольку данный хук
 * предоставляет доступ к структуре RelOptInfo.
 *
 * Если же данное отношение является базовым, то структура EERel
 * дополнительно заполняется информацией из структуры RangeTblEntry посредством
 * хука set_rel_pathlist_hook, который вызывается после обработки всех путей отношения.
 * На данный момент из RangeTblEntry берется лишь название базового отношения (Alias).
 */
typedef struct EERel
{
	/*
	 * Указатель на RelOptInfo отношения. В отличие от
	 * EEPath, указатель на RelOptInfo является однозначным идентификатором для
	 * отношения.
	 */
	RelOptInfo *roi_pointer;

	/*
	 * Количество базовых отношений, которые необходимо соединнить для получения 
	 * данного отношения в пределах запроса/подзапроса.
	 * 
	 * Равен полю relids структуры RelOptInfo
	 */
	int joined_rel_num;

	/*
	 * Однозначный идентификатор eerel отношения в пределах одного
	 * EXPLAIN запроса.
	 */
	int64		id;

	/*
	 * Название отношения.
	 */
	char	   *name;

	/*
	 * Алиас отношения.
	 */
	char	   *alias;

	/* 
	 * Средний размер результирующих строк.
	 */
	int			width;

	/*
	 * Список путей, принадлежащих данному отношению.
	 *
	 * Определяет принадлежность путей к конкретному отношению.
	 */
	List	   *eepath_list;

}			EERel;

/*
 *
 */
typedef struct EESubQuery
{
	/*
	 * Однозначный идентификатор запроса/подзапроса в пределах одного
	 * EXPLAIN.
	 */
	int64		id;

	/*
	 * Уровень вложенности подзапроса (PlannerInfo->query_level)
	 */
	Index	subquery_level;

	/* 
	 * Список eerel отношений.
	 *
	 * Определяет принадлежность отношений к конкретному запросу/подзапросу.
	 */
	List	   *eerel_list;
}			EESubQuery;

typedef struct EERelHashEntry
{
    void		*roi_ptr;
    EERel		*eerel;
} EERelHashEntry;

typedef struct EEPathHashKey
{
    void		*path_ptr;
	Cardinality rows;
	Cost		startup_cost;
	Cost		total_cost;
} EEPathHashKey;

typedef struct EEPathHashEntry
{
	EEPathHashKey	key;
    EEPath     		*eepath;
} EEPathHashEntry;

/*
 * EEState определяет основные переменные расширения extended_explain
 */
typedef struct EEState
{
	/*
	 * Список запросов/подзапросов.
	 */
	List	   *eesubquery_list;

	/*
	 * Определяет текущий запрос/подзапрос, который планирует оптимизатор 
	 */
	EESubQuery		*current_eesubquery;
	
	/*
	 * Счетчик путей, отношений и запросов/подзапросов для однозначной идентификации.
	 */
	int64		eepath_counter;
	int64		eerel_counter;
	int64		eesubquery_counter;

	/*
	 * Определяет минимальный уровень текущего обрабатываемого
	 * подзапроса.
	 *
	 * Для первого обрабатываемого подзапроса равен
	 * единице. Для последующих подзапросов равен наибольшему уровню предыдущего подзапроса плюс
	 * один.
	 */
	int64		init_level;

	RelOptInfo	*cached_current_rel;
	EERel		*cached_current_eerel;

	HTAB	*eerel_by_roi;
	HTAB	*eepath_by_path;
}			EEState;

/*
 * Типы PathWithOneSubPath и PathWithTwoSubPaths и макросы GET_SUB_PATH,
 * GET_OUTER_PATH, GET_INNER_PATH позволяют обращаться к дочерним путям.
 */
typedef struct PathWithOneSubPath
{
	Path		path;
	Path	   *subpath;
}			PathWithOneSubPath;

typedef JoinPath PathWithTwoSubPaths;

#define GET_SUB_PATH(path) (((PathWithOneSubPath *) path)->subpath)
#define GET_OUTER_PATH(path) (((PathWithTwoSubPaths *) path)->outerjoinpath)
#define GET_INNER_PATH(path) (((PathWithTwoSubPaths *) path)->innerjoinpath)

/*-------------------------------------------------------------------------
 * 								Заголовки функций
 *-------------------------------------------------------------------------
 */

extern void ee_add_path_hook(RelOptInfo *parent_rel,
							 Path *new_path);

extern void ee_explain(Query *query, int cursorOptions,
					   IntoClause *into, struct ExplainState *es,
					   const char *queryString, ParamListInfo params,
					   QueryEnvironment *queryEnv);

extern void ee_remember_rel_pathlist(PlannerInfo *root,
									 RelOptInfo *rel,
									 Index rti,
									 RangeTblEntry *rte);

extern void ee_process_upper_paths(PlannerInfo *root,
								   UpperRelationKind stage,
								   RelOptInfo *input_rel,
								   RelOptInfo *output_rel,
								   void *extra);

extern EEState * create_ee_state(void);

extern int	get_subpath_num(Path *path);

extern EEPath * create_eepath(Path *path, EERel *eerel);
extern EEPath * search_eepath(Path *path);
extern EEPath * record_eepath(EERel * eerel, Path *new_path);

extern EERel * create_eerel(RelOptInfo *roi);
extern EERel * search_eerel(RelOptInfo *roi);

extern void init_eesubquery(void);

#endif							/* EXTENDED_EXPLAIN_H */