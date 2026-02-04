/* extended_explain--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION extended_explain" to load this file. \quit

/*
 * Данная схема хранит все SQL объекты расширения
 */ 
CREATE SCHEMA ee;

/*
 * В таблицу ee.query записыватся общая инфомрация об EXPLAIN запросах,
 * выполненных с параметром get_paths
 */
CREATE TABLE ee.query 
(
	/* Однозначный идентификатор EXPLAIN запроса */
	id SERIAL PRIMARY KEY,

	/* Время исполнения EXPLAIN запроса */
	execution_ts timestamp,

	/* Текст EXPLAIN запроса */
	query_text TEXT
);

/*
 * В таблицу ee.paths записываются все пути, которые были рассмотрены 
 * планировщиком при исполнении запроса в режиме EXPLAIN.
 */ 
CREATE TABLE ee.paths
(
	/* Однозначный идентификатор EXPLAIN запроса */
	query_id bigint,

	/* Однозначный идентификатор запроса/подзапроса в рамках одного EXPLAIN запроса */
	subquery_id bigint,

	/* 
	 * Уровень вложенности подзапроса.
	 */
	subquery_level bigint,

	/* Однозначный идентификатор отношения в рамках одного EXPLAIN запроса */
	rel_id bigint,

	/* Однозначный идентификатор пути в рамках одного EXPLAIN запроса */
	path_id bigint,

	/* Название пути. Совпадает с названием соответствующего узла плана */
	path_type text,

	/* Идентификаторы дочерних путей */
	child_paths bigint[],

	/* Начальная и конечная стоимости путей */
	startup_cost float,
	total_cost float,

	/* Кардинальность пути */
	rows integer,

	/* Средний размер результирующих строк */
	width int,

	/* Имя отношения */
	rel_name text,

	/* Алиас отношения */	
	rel_alias text,

	/* Oid индекса, использованного при чтении таблицы */
	indexoid oid,

	/*
	 * Количество базовых отношений, которые необходимо соединнить для получения 
	 * данного отношения в пределах запроса/подзапроса.
	 * 
	 * Соответствует полю relids структуры RelOptInfo
	 */
	level int,

	/*
	 * Результат работы функции add_path. 
	 *
	 * Может принимать три значения: 
	 *	saved (путь сохранен в pathlist и не был вытеснен), 
	 *	displaced (путь вытеснен другим путем),
	 *  removed (путь оказался хуже других путей из pathlist).
	 */
	add_path_result text,

	/*
	 * id пути, который вытеснил текущий путь 
	 */
	displaced_by bigint,

	/* 
	 * Результаты сравнения характеристик вытесняемого и вытесняющего путей
	 */
	cost_cmp text,
	fuzz_factor double precision,
	pathkeys_cmp text,
	bms_cmp text,
	rows_cmp text,
	parallel_safe_cmp text,

	/*
	 * Количество отключенных узлов дерева путей
	 */
	disabled_nodes integer,

	FOREIGN KEY (query_id) REFERENCES  ee.query(id)
);

/* 
 * Функция очистки таблиц ee.query и ee.paths
 */
CREATE FUNCTION ee.clear()
RETURNS boolean AS $$
BEGIN
    TRUNCATE TABLE ee.query CASCADE;
	RETURN true;
END;
$$ LANGUAGE plpgsql;