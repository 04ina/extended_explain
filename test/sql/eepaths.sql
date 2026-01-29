--
-- Подготовка
--

CREATE EXTENSION extended_explain;

SET debug_parallel_query = off;
SET jit = off;

CREATE TABLE test_table(col integer); 
CREATE TABLE t1 (a int);
CREATE TABLE t2 (b int);
CREATE TABLE t3 (c int);

INSERT INTO test_table SELECT generate_series(1,1000);
INSERT INTO t1 SELECT generate_series(1, 100);
INSERT INTO t2 SELECT generate_series(1, 200);
INSERT INTO t3 SELECT generate_series(1, 50);

ANALYZE test_table, t1, t2, t3;
--
-- 1. Проверка параметра get_paths  
--

EXPLAIN
SELECT * FROM test_table;

SELECT 
	query_id, subquery_id, subquery_level, rel_id, path_id, path_type, child_paths,
	startup_cost, total_cost, rows, width, rel_name, rel_alias, indexoid, level,  
	add_path_result, displaced_by, cost_cmp, fuzz_factor, pathkeys_cmp, bms_cmp
	rows_cmp, parallel_safe_cmp
FROM ee.paths
ORDER BY path_id;

EXPLAIN (get_paths)
SELECT * FROM test_table;

SELECT 
	query_id, subquery_id, subquery_level, rel_id, path_id, path_type, child_paths,
	startup_cost, total_cost, rows, width, rel_name, rel_alias, indexoid, level,  
	add_path_result, displaced_by, cost_cmp, fuzz_factor, pathkeys_cmp, bms_cmp
	rows_cmp, parallel_safe_cmp
FROM ee.paths
ORDER BY path_id;

SELECT ee.clear();

SELECT 
	query_id, subquery_id, subquery_level, rel_id, path_id, path_type, child_paths,
	startup_cost, total_cost, rows, width, rel_name, rel_alias, indexoid, level,  
	add_path_result, displaced_by, cost_cmp, fuzz_factor, pathkeys_cmp, bms_cmp
	rows_cmp, parallel_safe_cmp
FROM ee.paths
ORDER BY path_id;

--
-- 2. SeqScan
--

EXPLAIN (get_paths)
SELECT * FROM test_table;

SELECT 
	query_id, subquery_id, subquery_level, rel_id, path_id, path_type, child_paths,
	startup_cost, total_cost, rows, width, rel_name, rel_alias, indexoid, level,  
	add_path_result, displaced_by, cost_cmp, fuzz_factor, pathkeys_cmp, bms_cmp
	rows_cmp, parallel_safe_cmp
FROM ee.paths
ORDER BY path_id;

SELECT ee.clear();

-- 
-- 3. IndexOnlyScan
--
CREATE INDEX ON test_table(col);

EXPLAIN (get_paths)
SELECT * FROM test_table WHERE col = 3;

SELECT 
	query_id, subquery_id, subquery_level, rel_id, path_id, path_type, child_paths,
	startup_cost, total_cost, rows, width, rel_name, rel_alias, level,  
	add_path_result, displaced_by, cost_cmp, fuzz_factor, pathkeys_cmp, bms_cmp
	rows_cmp, parallel_safe_cmp
FROM ee.paths
ORDER BY path_id;

SELECT ee.clear();
DROP INDEX test_table_col_idx;

--
-- 4. CteScan
--
EXPLAIN (get_paths)
WITH cte AS MATERIALIZED
(
	SELECT * FROM test_table
)
SELECT * FROM cte;

SELECT 
	query_id, subquery_id, subquery_level, rel_id, path_id, path_type, child_paths,
	startup_cost, total_cost, rows, width, rel_name, rel_alias, indexoid, level,  
	add_path_result, displaced_by, cost_cmp, fuzz_factor, pathkeys_cmp, bms_cmp
	rows_cmp, parallel_safe_cmp
FROM ee.paths
ORDER BY path_id;

SELECT ee.clear();

--
-- 5. HashJoin
--
EXPLAIN (get_paths)
SELECT * FROM t1 JOIN t2 ON t1.a = t2.b;

SELECT 
	query_id, subquery_id, subquery_level, rel_id, path_id, path_type, child_paths,
	startup_cost, total_cost, rows, width, rel_name, rel_alias, indexoid, level,  
	add_path_result, displaced_by, cost_cmp, fuzz_factor, pathkeys_cmp, bms_cmp
	rows_cmp, parallel_safe_cmp
FROM ee.paths
ORDER BY path_id;

SELECT ee.clear();

--
-- 6. Агрегаты (Agg)
--
EXPLAIN (get_paths)
SELECT COUNT(*) FROM t1;

SELECT 
	query_id, subquery_id, subquery_level, rel_id, path_id, path_type, child_paths,
	startup_cost, total_cost, rows, width, rel_name, rel_alias, indexoid, level,  
	add_path_result, displaced_by, cost_cmp, fuzz_factor, pathkeys_cmp, bms_cmp
	rows_cmp, parallel_safe_cmp
FROM ee.paths
ORDER BY path_id;

SELECT ee.clear();

--
-- 7. Сортировка (Sort)
--
EXPLAIN (get_paths)
SELECT * FROM t1 ORDER BY a;

SELECT 
	query_id, subquery_id, subquery_level, rel_id, path_id, path_type, child_paths,
	startup_cost, total_cost, rows, width, rel_name, rel_alias, indexoid, level,  
	add_path_result, displaced_by, cost_cmp, fuzz_factor, pathkeys_cmp, bms_cmp
	rows_cmp, parallel_safe_cmp
FROM ee.paths
ORDER BY path_id;

SELECT ee.clear();

--
-- 8. LIMIT
--
EXPLAIN (get_paths)
SELECT * FROM t1 LIMIT 5;

SELECT 
	query_id, subquery_id, subquery_level, rel_id, path_id, path_type, child_paths,
	startup_cost, total_cost, rows, width, rel_name, rel_alias, indexoid, level,  
	add_path_result, displaced_by, cost_cmp, fuzz_factor, pathkeys_cmp, bms_cmp
	rows_cmp, parallel_safe_cmp
FROM ee.paths
ORDER BY path_id;

SELECT ee.clear();

--
-- 9. Append
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
	query_id, subquery_id, subquery_level, rel_id, path_id, path_type, child_paths,
	startup_cost, total_cost, rows, width, rel_name, rel_alias, indexoid, level,  
	add_path_result, displaced_by, cost_cmp, fuzz_factor, pathkeys_cmp, bms_cmp
	rows_cmp, parallel_safe_cmp
FROM ee.paths
ORDER BY path_id;

SELECT ee.clear();

--
-- Очистка
--
DROP TABLE test_table, t1, t2, t3;