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

	/* 
	 * Тип подзапроса.
	 *
	 * На данный момент не реализовано.
	 */
	subquery_type text,

	/* Однозначный идентификатор запроса/подзапроса в рамках одного EXPLAIN запроса */
	subquery_id bigint,

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

	/* 
	 * Был ли путь отсечен функцией add_path
	 *
	 * На данный момент не реализовано.
	 */
	is_del bool,

	/* 
	 * Имя отношения. Существует лишь для таблиц.
     *	
	 * TODO:
	 *  Имеет смысл заменить на oid 
	 */
	rel_name text,

	/* Oid индекса, использованного при чтении таблицы */
	indexoid oid,

	/*
	 * Количество базовых отношений, которые необходимо соединнить для получения 
	 * данного отношения в пределах запроса/подзапроса.
	 * 
	 * Соответствует полю relids структуры RelOptInfo
	 */
	joined_rel_num int,

	FOREIGN KEY (query_id) REFERENCES  ee.query(id)
);
