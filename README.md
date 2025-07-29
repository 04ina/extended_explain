### Install

## make
```sh
make install
```    
Если переменная окружения PATH не определена:
```sh
make PG_CONFIG=postgres_directory/bin/pg_config install
```

## Создание расширения

Создаем расширение посредством терминального клиента psql
```sql
CREATE EXTENSION extended_explain;
```

### Usage

После установки расширения при каждом вызове команды EXPLAIN в таблицу ee.result будут записываться все пути, которые оптимизатор запросов перебирал при поиске лучшего плана исполнения SQL запроса.

Например:
```
CREATE TABLE t1 AS SELECT generate_series(1,1000) AS att;
CREATE INDEX ON t1(att);

CREATE TABLE t2 AS SELECT generate_series(1,10000) att;

explain SELECT * FROM t1 JOIN t2 ON t1.att = t2.att;
                                     QUERY PLAN                                     
------------------------------------------------------------------------------------
 Merge Join  (cost=1369.41..2662.91 rows=81600 width=8)
   Merge Cond: (t1.att = t2.att)
   ->  Index Only Scan using t1_att_idx on t1  (cost=0.28..67.28 rows=1000 width=4)
   ->  Sort  (cost=1369.14..1409.94 rows=16320 width=4)
         Sort Key: t2.att
         ->  Seq Scan on t2  (cost=0.00..227.20 rows=16320 width=4)
```

Содержимое таблицы ee.result:

```sql
SELECT * FROM ee.result;
 level | path_name |   path_type    | child_paths |    startup_cost    |     total_cost      | rows  | is_del | rel_name 
-------+-----------+----------------+-------------+--------------------+---------------------+-------+--------+----------
     1 |         1 | SeqScan        |             |                  0 |                  18 |  1000 | f      | t1
     1 |         2 | IndexOnlyScan  |             |              0.275 |              67.275 |  1000 | f      | t1
     1 |         3 | IndexOnlyScan  |             |              0.275 | 0.36568627450980395 |     5 | f      | t1
     1 |         4 | BitmapHeapScan |             | 0.3149754901960784 |   4.377475490196078 |     5 | t      | t1
     1 |         5 | SeqScan        |             |                  0 |  227.20000000000002 | 16320 | f      | t2
     2 |         6 | MergeJoin      | {1,5}       |  1436.968161870994 |  2665.9681618709938 | 81600 | f      | 
     2 |         7 | MergeJoin      | {2,5}       | 1369.4142404476836 |  2662.9142404476834 | 81600 | f      | 
     2 |         8 | HashJoin       | {1,5}       | 431.20000000000005 |              3307.7 | 81600 | t      | 
     2 |         9 | HashJoin       | {5,1}       |               30.5 |              3154.5 | 81600 | t      | 
(9 rows)
```

Посредством [pg_path_tree_visualization](https://github.com/04ina/pg_path_tree_visualization) можно визуализировать представленную в таблице ee.result информацию.