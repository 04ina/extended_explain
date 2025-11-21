--
-- Подготовка
--

CREATE EXTENSION extended_explain;

CREATE TABLE test_table(col integer); 
INSERT INTO test_table SELECT generate_series(1,1000);
ANALYZE test_table;

--
-- Проверка параметра get_paths  
--

EXPLAIN
SELECT * FROM test_table;

SELECT 
	query_id, subquery_type, subquery_id, rel_id, path_id, path_type, child_paths,
	startup_cost, total_cost, rows, rel_name, joined_rel_num
FROM ee.paths;

EXPLAIN (get_paths)
SELECT * FROM test_table;

SELECT 
	query_id, subquery_type, subquery_id, rel_id, path_id, path_type, child_paths,
	startup_cost, total_cost, rows, rel_name, joined_rel_num
FROM ee.paths;

TRUNCATE TABLE ee.paths, ee.query;

--
-- Проверка простейших случаев
--

-- SeqScan
EXPLAIN (get_paths)
SELECT * FROM test_table;

SELECT 
	query_id, subquery_type, subquery_id, rel_id, path_id, path_type, child_paths,
	startup_cost, total_cost, rows, rel_name, joined_rel_num
FROM ee.paths;

TRUNCATE TABLE ee.paths, ee.query;

-- IndexOnlyScan
CREATE INDEX ON test_table(col);

EXPLAIN (get_paths)
SELECT * FROM test_table;

SELECT 
	query_id, subquery_type, subquery_id, rel_id, path_id, path_type, child_paths,
	startup_cost, total_cost, rows, rel_name, joined_rel_num
FROM ee.paths;

TRUNCATE TABLE ee.paths, ee.query;
DROP INDEX test_table_col_idx;

-- CteScan, проверка связи между запросом и подзапросом 
EXPLAIN (get_paths)
WITH cte AS MATERIALIZED
(
	SELECT * FROM test_table
)
SELECT * FROM cte;

SELECT 
	query_id, subquery_type, subquery_id, rel_id, path_id, path_type, child_paths,
	startup_cost, total_cost, rows, rel_name, joined_rel_num
FROM ee.paths;

TRUNCATE TABLE ee.paths, ee.query;

--
-- Подготовка таблиц для тестирования соединений и других операторов
--
CREATE TABLE t1 (a int);
CREATE TABLE t2 (b int);
CREATE TABLE t3 (c int);

INSERT INTO t1 SELECT generate_series(1, 100);
INSERT INTO t2 SELECT generate_series(1, 200);
INSERT INTO t3 SELECT generate_series(1, 50);

ANALYZE t1, t2, t3;

--
-- 1. HashJoin (без индексов → SeqScan + HashJoin)
--
EXPLAIN (get_paths)
SELECT * FROM t1 JOIN t2 ON t1.a = t2.b;

SELECT 
	query_id, subquery_type, subquery_id, rel_id, path_id, path_type, child_paths,
	startup_cost, total_cost, rows, rel_name, joined_rel_num
FROM ee.paths
ORDER BY path_id;

TRUNCATE TABLE ee.paths, ee.query;

--
-- 2. MergeJoin (требует сортировки или индексов)
--
CREATE INDEX ON t1(a);
CREATE INDEX ON t2(b);

EXPLAIN (get_paths)
SELECT * FROM t1 JOIN t2 ON t1.a = t2.b;

SELECT 
	query_id, subquery_type, subquery_id, rel_id, path_id, path_type, child_paths,
	startup_cost, total_cost, rows, rel_name, joined_rel_num
FROM ee.paths
ORDER BY path_id;

TRUNCATE TABLE ee.paths, ee.query;

DROP INDEX t1_a_idx, t2_b_idx;

--
-- 3. Nested Loop Join (маленькие таблицы)
--
DELETE FROM t1 WHERE a > 10;
DELETE FROM t2 WHERE b > 10;
ANALYZE t1, t2;

EXPLAIN (get_paths)
SELECT * FROM t1 JOIN t2 ON t1.a = t2.b;

SELECT 
	query_id, subquery_type, subquery_id, rel_id, path_id, path_type, child_paths,
	startup_cost, total_cost, rows, rel_name, joined_rel_num
FROM ee.paths
ORDER BY path_id;

TRUNCATE TABLE ee.paths, ee.query;

-- Восстановление данных
INSERT INTO t1 SELECT generate_series(11, 100);
INSERT INTO t2 SELECT generate_series(11, 200);
ANALYZE t1, t2;

--
-- 4. Агрегаты (Agg)
--
EXPLAIN (get_paths)
SELECT COUNT(*) FROM t1;

SELECT 
	query_id, subquery_type, subquery_id, rel_id, path_id, path_type, child_paths,
	startup_cost, total_cost, rows, rel_name, joined_rel_num
FROM ee.paths
ORDER BY path_id;

TRUNCATE TABLE ee.paths, ee.query;

--
-- 5. Сортировка (Sort)
--
EXPLAIN (get_paths)
SELECT * FROM t1 ORDER BY a;

SELECT 
	query_id, subquery_type, subquery_id, rel_id, path_id, path_type, child_paths,
	startup_cost, total_cost, rows, rel_name, joined_rel_num
FROM ee.paths
ORDER BY path_id;

TRUNCATE TABLE ee.paths, ee.query;

--
-- 6. LIMIT (Result, Limit)
--
EXPLAIN (get_paths)
SELECT * FROM t1 LIMIT 5;

SELECT 
	query_id, subquery_type, subquery_id, rel_id, path_id, path_type, child_paths,
	startup_cost, total_cost, rows, rel_name, joined_rel_num
FROM ee.paths
ORDER BY path_id;

TRUNCATE TABLE ee.paths, ee.query;

--
-- 7. Подзапросы (SubqueryScan / InitPlan)
--
EXPLAIN (get_paths)
SELECT * FROM t1 WHERE a IN (SELECT b FROM t2 WHERE b < 10);

SELECT 
	query_id, subquery_type, subquery_id, rel_id, path_id, path_type, child_paths,
	startup_cost, total_cost, rows, rel_name, joined_rel_num
FROM ee.paths
ORDER BY path_id;

TRUNCATE TABLE ee.paths, ee.query;

--
-- 8. Материализация через UNION (Materialize)
--
EXPLAIN (get_paths)
WITH cte AS NOT MATERIALIZED
(
    SELECT * FROM t1
)
SELECT * FROM cte
UNION ALL
SELECT * FROM cte;

SELECT 
	query_id, subquery_type, subquery_id, rel_id, path_id, path_type, child_paths,
	startup_cost, total_cost, rows, rel_name, joined_rel_num
FROM ee.paths
ORDER BY path_id;

TRUNCATE TABLE ee.paths, ee.query;

--
-- 9. Трёхтабличное соединение (проверка многоуровневых joined_rel)
--
EXPLAIN (get_paths)
SELECT * FROM t1 JOIN t2 ON t1.a = t2.b JOIN t3 ON t2.b = t3.c;

SELECT 
	query_id, subquery_type, subquery_id, rel_id, path_id, path_type, child_paths,
	startup_cost, total_cost, rows, rel_name, joined_rel_num
FROM ee.paths
ORDER BY path_id;

TRUNCATE TABLE ee.paths, ee.query;

--
-- 10. Рекурсивный CTE (RecursiveUnion, WorkTableScan)
--
CREATE TABLE graph (id int, parent int);
INSERT INTO graph VALUES (1, NULL), (2, 1), (3, 2), (4, 3);
ANALYZE graph;

EXPLAIN (get_paths)
WITH RECURSIVE r AS (
    SELECT id, parent FROM graph WHERE parent IS NULL
    UNION ALL
    SELECT g.id, g.parent FROM graph g JOIN r ON g.parent = r.id
)
SELECT * FROM r;

SELECT 
	query_id, subquery_type, subquery_id, rel_id, path_id, path_type, child_paths,
	startup_cost, total_cost, rows, rel_name, joined_rel_num
FROM ee.paths
ORDER BY path_id;

TRUNCATE TABLE ee.paths, ee.query;

--
-- Очистка
--
DROP TABLE test_table, t1, t2, t3, graph;