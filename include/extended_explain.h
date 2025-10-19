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


/*
 * EEPath -- информация об исходном пути
 *
 * Данная структура заполняется посредством хука add_path_hook.
 *
 * Причем наибольшую сложность при заполнении представляет связывание родительского
 * пути с дочерними. Исходный родительский путь содержит указатели на дочерние
 * пути, а для связи соответствующих eepath путей необходимо некое однозначное
 * соответствие между исходным дочерним путем и дочерним eepath путем.
 *
 *
 * Для поиска по исходному пути соответствующего eepath используется перебор
 * всех путей из global_ee_state->eepath_list: если указатель на исходный путь
 * и значение eepath->path_pointer, а также соответствующие кардинальности совпадают,
 * значит пути соответствуют друг другу. Однако мы не можем полагаться лишь на
 * указатель на исходный путь, поскольку функция add_path может по указателю удалить
 * исходный путь, а затем дать этот указатель уже другому пути. Во Избежание этого
 * есть второй критерий в виде кардинальности, однако и в этом случае однозначная
 * идентификации не гарантируется, вследствие чего необходимо разработать более
 * продвинутый метод однозначной идентификации.
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
	 * EXPLAIN
	 */
	int64		id;

	/* Список eepath путей */
	List	   *eepath_list;

	/*
	 * Тип пути, наследуемый от исходного пути
	 */
	NodeTag		pathtype;

	/*
	 * Уровень пути. Необходим для иерархии путей в таблице
	 * ee.paths (может быть полезно при
	 * визуализации). В дальнейшем планируется отказаться от данного уровня в пользу иерархии на основе принадлежности к конкретным отношениям и
	 * запросам/подзапросам.
	 *
	 */
	int			level;

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

	/* Был ли путь отфильтрован функцией add_path */
	bool		is_del;
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

	int64		id;

	/*
	 * Название отношения. На данный момент существует лишь для базовых отношений
	 */
	Alias	   *eref;

	/*
	 * Список путей, принадлежащих данному
	 * отношению.
	 *
	 * Позволяет определить принадлежность путей к конкретному отношению
	 *
	 * Также используется при выводе путей в сгруппированном виде
	 * (все пути, принадлежащие одному
	 * отношению, расположены рядом в таблице
	 * ee.paths)
	 */
	List	   *eepath_list;
}			EERel;

/*
 *
 */
typedef struct EESubQuery
{
	//PlannerInfo	*pi_pointer;

	int64		id;

	/* Список eerel отношений */
	List	   *eerel_list;

}			EESubQuery;

/*
 * EEState определяет основные переменные расширения extended_explain
 */
typedef struct EEState
{
	MemoryContext ctx;

	List	   *eesubquery_list;

	EESubQuery		*current_eesubquery;
	
	/*
	 * Счетчик путей для однозначной идентификации путей
	 *
	 * Не путать с идентификацией для поиска
	 * eepath по path
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

extern EEState * init_ee_state(void);
extern void delete_ee_state(EEState * ee_state);

extern EEPath * create_eepath(EERel * eerel, Path *new_path);
extern int	get_subpath_num(Path *path);

extern EEPath * init_eepath(EERel *eerel);
extern EEPath * search_eepath(EERel *eerel, Path *path);
extern void fill_eepath(EEPath * eepath, Path *path);

extern EERel * init_eerel(EESubQuery *eesubquery);
extern EERel * search_eerel(EESubQuery *eesubquery, RelOptInfo *roi);
extern void fill_eerel(EERel * eerel, RelOptInfo *roi);

extern void init_eesubquery(void);

#endif							/* EXTENDED_EXPLAIN_H */
