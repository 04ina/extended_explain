--
-- Проверка записи в таблицы ee.query и параметра get_paths
--

EXPLAIN
SELECT * FROM test_table;

SELECT id, query_text FROM ee.query;

EXPLAIN (get_paths)
SELECT * FROM test_table;

SELECT id, query_text FROM ee.query;

