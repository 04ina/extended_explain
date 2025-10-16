/* extended_explain--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION extended_explain" to load this file. \quit

/*
 * Данная схема хранит все SQL объекты расширения
 */ 
CREATE SCHEMA ee;

CREATE TABLE ee.query 
(
	id SERIAL PRIMARY KEY,
	execution_ts timestamp,
	query_text TEXT
);

/*
 * В таблицу ee.paths записываются все пути, которые были рассмотрены 
 * планировщиком при исполнении запроса в режиме EXPLAIN.
 */ 
CREATE TABLE ee.paths
(
	query_id bigint,

	/* Уровень, на котором находится путь. Нужен для наглядной иерархии путей */
	level integer, 		

	/* Однозначный идентификатор пути в рамках одного запроса */
	path_name bigint,

	/* Название пути. Совпадает с названием соответствующего узла плана */
	path_type text,

	/* Идентификаторы дочерних путей */
	child_paths bigint[],

	/* Начальная и конечная стоимости путей */
	startup_cost float,
	total_cost float,

	/* Кардинальность пути */
	rows integer,

	/* Был ли путь отсечен функцией add_path */
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

	FOREIGN KEY (query_id) REFERENCES  ee.query(id)
);
