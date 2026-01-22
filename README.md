extended_explain -- расширение для СУБД PostgreSQL, позволяющее исследовать все пути (узлы), которые оптимизатор/планировщик перебирает в процессе поиска наилучшего плана исполнения запроса. 

Содержание:

1. [Инсталляция](#инсталляция)
     1. [make](#make)
     2. [meson](#meson)
2. [Примеры использования](#примеры-использования)
3. [Тесты](#тесты)

# Инсталляция

Перед инсталляцией необходимо применить патч для postgres-а. В репозитории есть два патча: 
1. after_24225ad9aaf.patch - для postgres-а после патча 24225ad9aaf включительно
2. before_24225ad9aaf.patch - для postgres-a до патча 24225ad9aaf

```sh
git apply patch_name.patch
```

Инсталлировать расширение можно двумя способами: с помощью make и meson.

## make
```sh
make install
```    
Если переменная окружения PATH не определена:
```sh
make PG_CONFIG=postgres_directory/bin/pg_config install
```
## meson 
```sh
meson build
cd build
ninja install
```
Если переменная окружения PATH не определена:
```sh
meson build -Dpg_config=postgres_directory/bin/pg_config
cd build
ninja install
```

### Создание расширения

Добавляем расширение в shared_preload_libraries в конфигурационном файле postgresql.conf:
```
shared_preload_libraries = 'extended_explain'
```

Далее создаем расширение посредством терминального клиента psql.
```sql
CREATE EXTENSION extended_explain;
```

# Примеры использования

При каждом вызове команды EXPLAIN с параметром get_paths в таблицу ee.paths будут записываться все пути, которые оптимизатор запросов рассматривал при поиске лучшего плана исполнения SQL запроса. Помимо этого в таблицу ee.query будет записана общая инфомрация об исполннеии команды EXPLAIN.

В качестве примера рассмотрим запрос с соединением двух таблиц t1 и t2, каждая из которых имеет по одному столбцу типа integer. Также на столбец таблицы t1 "навесим" btree индекс.
```sql
CREATE TABLE t1 AS SELECT generate_series(1,1000) AS att;
CREATE INDEX ON t1(att);

CREATE TABLE t2 AS SELECT generate_series(1,10000) att;

ANALYZE;
```

Выполним команду EXPLAIN для запроса с соединением:

```sql
EXPLAIN (get_paths) SELECT * FROM t1 JOIN t2 ON t1.att = t2.att;
                            QUERY PLAN                            
------------------------------------------------------------------
 Hash Join  (cost=30.50..223.00 rows=1000 width=8)
   Hash Cond: (t2.att = t1.att)
   ->  Seq Scan on t2  (cost=0.00..145.00 rows=10000 width=4)
   ->  Hash  (cost=18.00..18.00 rows=1000 width=4)
         ->  Seq Scan on t1  (cost=0.00..18.00 rows=1000 width=4)
```

После завершения исполнения команды все пути сохраняются в таблицу расширения ee.paths:

```sql
 query_id | subquery_id | subquery_level | rel_id | path_id |   path_type    | child_paths |    startup_cost     |     total_cost      | rows  | width | rel_name | rel_alias | indexoid | level | add_path_result | displaced_by |           cost_cmp            | fuzz_factor | pathkeys_cmp | bms_cmp | rows_cmp | parallel_safe_cmp 
----------+-------------+----------------+--------+---------+----------------+-------------+---------------------+---------------------+-------+-------+----------+-----------+----------+-------+-----------------+--------------+-------------------------------+-------------+--------------+---------+----------+-------------------
        1 |           1 |              1 |      1 |       1 | SeqScan        |             |                   0 |                  18 |  1000 |     4 | t1       |           |          |     1 | saved           |              |                               |             |              |         |          | 
        1 |           1 |              1 |      1 |       2 | IndexOnlyScan  |             |               0.275 |              46.275 |  1000 |     4 | t1       |           |   258926 |     1 | saved           |              |                               |             |              |         |          | 
        1 |           1 |              1 |      1 |       3 | IndexOnlyScan  |             |               0.275 | 0.29769999999999996 |     1 |     4 | t1       |           |   258926 |     1 | saved           |              |                               |             |              |         |          | 
        1 |           1 |              1 |      1 |       4 | IndexOnlyScan  |             |               0.275 |              46.275 |  1000 |     4 | t1       |           |   258922 |     1 | removed         |              |                               |             |              |         |          | 
        1 |           1 |              1 |      1 |       5 | IndexOnlyScan  |             |               0.275 | 0.29769999999999996 |     1 |     4 | t1       |           |   258922 |     1 | removed         |              |                               |             |              |         |          | 
        1 |           1 |              1 |      1 |       6 | BitmapHeapScan |             | 0.28474999999999995 |             4.29725 |     1 |     4 | t1       |           |          |     1 | removed         |              |                               |             |              |         |          | 
        1 |           1 |              1 |      2 |       7 | SeqScan        |             |                   0 |                 145 | 10000 |     4 | t2       |           |          |     1 | saved           |              |                               |             |              |         |          | 
        1 |           1 |              1 |      3 |       8 | MergeJoin      | {1,7}       |   877.2195404007834 |   897.2145404007834 |  1000 |     8 |          |           |          |     2 | displaced       |            9 | total and startup costs worse |        1.01 | equal        | equal   | equal    | equal
        1 |           1 |              1 |      3 |       9 | MergeJoin      | {2,7}       |    809.665618977473 |    873.160618977473 |  1000 |     8 |          |           |          |     2 | displaced       |           10 | total and startup costs worse |        1.01 | equal        | equal   | equal    | equal
        1 |           1 |              1 |      3 |      10 | HashJoin       | {1,7}       |                 270 |              301.75 |  1000 |     8 |          |           |          |     2 | displaced       |           11 | total and startup costs worse |        1.01 | equal        | equal   | equal    | equal
        1 |           1 |              1 |      3 |      11 | HashJoin       | {7,1}       |                30.5 |                 223 |  1000 |     8 |          |           |          |     2 | saved           |              |                               |             |              |         |          | 
        1 |           1 |              1 |      4 |      12 | HashJoin       | {7,1}       |                30.5 |                 223 |  1000 |     0 |          |           |          |     0 | saved           |              |                               |             |              |         |          | 
(12 rows)
```

Описание столбцов таблицы ee.paths можно посмотреть в файле "extended_explain--1.0.sql".

С помощью данного расширения можно увидеть, что планировщик рассматривал 6 вариантов сканирования отношения t1. Также можно обратить внимание, что планировщик не рассматривал использование NestLoop соединения. В свою очередь, MergeJoin пути показали наихудшую общую стоимость в сравнении с HashJoin путями. 

Информацию из таблицы ee.paths можно по-разному интерпретировать. Примером может послужить тестовый проект [ee_visualizer](https://github.com/04ina/ee_visualizer), визуализирующий пути в удобном для анализа формате.

После исполнения команды в таблицу ee.query добавляется общая информация об исполнении планирования:

```sql
SELECT * FROM ee.query;
 id |        execution_ts        |                            query_text                            
----+----------------------------+------------------------------------------------------------------
  1 | 2025-11-01 17:33:01.978709 | EXPLAIN (get_paths) SELECT * FROM t1 JOIN t2 ON t1.att = t2.att;
(1 row)
```

Помимо этого в расширении реализована функция ee.clear(), очищающая таблицы ee.paths и ee.query.

# Тесты 

Произвести тестирование расширения можно посредством make и meson.

## make
```sh
make installcheck
```    
Если же переменная окружения PATH не определена:
```sh
make PG_CONFIG=postgres_directory/bin/pg_config installcheck
```
## meson 
```sh
cd build
ninja test 
```