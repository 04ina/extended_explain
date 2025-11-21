--
-- Проверка записи в таблицы ee.query и параметра get_paths
--

CREATE TABLE test_table(col integer); 
INSERT INTO test_table SELECT generate_series(1,1000);

EXPLAIN
SELECT * FROM test_table;

SELECT id, query_text FROM ee.query;

EXPLAIN (get_paths)
SELECT * FROM test_table;

SELECT id, query_text FROM ee.query;

DROP TABLE test_table;