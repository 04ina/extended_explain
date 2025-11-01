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
	query_id, 
	subquery_type, 
	subquery_id, 
	rel_id, 
	path_id, 
	path_type, 
	child_paths, 
	startup_cost,
	total_cost,
	rows,
	rel_name 
FROM 
	ee.paths;

EXPLAIN (get_paths)
SELECT * FROM test_table;

SELECT 
	query_id, 
	subquery_type, 
	subquery_id, 
	rel_id, 
	path_id, 
	path_type, 
	child_paths, 
	startup_cost,
	total_cost,
	rows,
	rel_name 
FROM 
	ee.paths;

TRUNCATE TABLE ee.paths, ee.query;
--
-- Проверка простейших случаев
--

-- SeqScan
EXPLAIN (get_paths)
SELECT * FROM test_table;

SELECT 
	query_id, 
	subquery_type, 
	subquery_id, 
	rel_id, 
	path_id, 
	path_type, 
	child_paths, 
	startup_cost,
	total_cost,
	rows,
	rel_name 
FROM 
	ee.paths;

TRUNCATE TABLE ee.paths, ee.query;


-- IndexOnlyScan
CREATE INDEX ON test_table(col);

EXPLAIN (get_paths)
SELECT * FROM test_table;

SELECT 
	query_id, 
	subquery_type, 
	subquery_id, 
	rel_id, 
	path_id, 
	path_type, 
	child_paths, 
	startup_cost,
	total_cost,
	rows,
	rel_name 
FROM 
	ee.paths;

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
	query_id, 
	subquery_type, 
	subquery_id, 
	rel_id, 
	path_id, 
	path_type, 
	child_paths, 
	startup_cost,
	total_cost,
	rows,
	rel_name 
FROM 
	ee.paths;


TRUNCATE TABLE ee.paths, ee.query;

