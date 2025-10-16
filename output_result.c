/*-------------------------------------------------------------------------
 *
 * output_result.c
 *    Вывод результата работы расширения
 *
 * На данный момент вывод осуществляется только посредством
 * таблицы ee.paths.
 *
 *-------------------------------------------------------------------------
 */

#include "include/output_result.h"

#include "access/heapam.h"
#include "access/relation.h"
#include "access/table.h"
#include "access/htup_details.h"
#include "catalog/pg_type.h"
#include "nodes/makefuncs.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/rel.h"
#include "executor/executor.h"
#include "catalog/namespace.h"

/*
 * Получает название узла плана по его NodeTag
 */
static const char *
nodetag_to_string(NodeTag pathtype)
{
	switch (pathtype)
	{
		case T_SeqScan:
			return "SeqScan";
		case T_IndexScan:
			return "IndexScan";
		case T_IndexOnlyScan:
			return "IndexOnlyScan";
		case T_BitmapHeapScan:
			return "BitmapHeapScan";
		case T_TidScan:
			return "TidScan";
		case T_MergeJoin:
			return "MergeJoin";
		case T_HashJoin:
			return "HashJoin";
		case T_NestLoop:
			return "NestLoop";
		case T_Material:
			return "Material";
		case T_Memoize:
			return "Memoize";
		case T_Result:
			return "Result";
		case T_Limit:
			return "Limit";
		case T_SubqueryScan:
			return "SubqueryScan";
		case T_Agg:
			return "Agg";
		default:
			return "Unknown";
	}
}

/*
 * 
 */
static int64
get_next_query_id(void)
{
	Oid seqoid;
	int64 query_id;

	seqoid = get_relname_relid("query_id_seq", get_namespace_oid("ee",false));
	if (!OidIsValid(seqoid))
		elog(ERROR, "sequence ee.paths_id_seq not found");

	query_id = DatumGetInt64(DirectFunctionCall1(nextval_oid, ObjectIdGetDatum(seqoid)));

	return query_id;
}

/*
 * Записывает все пути eerel отношения в таблицу ee.paths
 */
void
insert_eerel_into_eepaths(EERel * eerel, int64 query_id)
{
	Relation	rel;
	TupleDesc	tupdesc;
	HeapTuple	tuple;
	Datum		values[11];
	bool		nulls[11] = {false, false, false, false, false, false, false, false, false, false, false};
	ListCell   *lc;
	EState	   *estate;

	estate = CreateExecutorState();

	rel = table_openrv(makeRangeVar("ee", "paths", -1), RowExclusiveLock);

	/* Get the tuple descriptor for the table */
	tupdesc = RelationGetDescr(rel);

	/* For each path in the pathlist */
	foreach(lc, eerel->eepath_list)
	{
		EEPath	   *eepath = (EEPath *) lfirst(lc);

		values[0] = Int64GetDatum(query_id);

		values[1] = Int32GetDatum(eepath->level);

		values[2] = Int64GetDatum(eepath->id);

		values[3] = CStringGetTextDatum(nodetag_to_string(eepath->pathtype));

		if (eepath->nsub == 0)
		{
			nulls[4] = true;
			values[4] = (Datum) 0;

		}
		else if (eepath->nsub == 1)
		{
			Datum		sub_ids[1];

			nulls[4] = false;
			sub_ids[0] = Int64GetDatum(eepath->sub_eepath_1->id);
			values[4] = PointerGetDatum(construct_array(sub_ids,
														1,
														INT8OID,
														8,
														true,
														'd'));
		}
		else if (eepath->nsub == 2)
		{
			Datum		sub_ids[2];

			nulls[4] = false;
			sub_ids[0] = Int64GetDatum(eepath->sub_eepath_1->id);
			sub_ids[1] = Int64GetDatum(eepath->sub_eepath_2->id);
			values[4] = PointerGetDatum(construct_array(sub_ids,
														2,
														INT8OID,
														8,
														true,
														'd'));
		}

		values[5] = Float8GetDatum(eepath->startup_cost);
		values[6] = Float8GetDatum(eepath->total_cost);
		values[7] = Int64GetDatum(eepath->rows);
		values[8] = BoolGetDatum(eepath->is_del);

		if (eerel->eref == NULL)
		{
			nulls[9] = true;
			values[9] = (Datum) 0;
		}
		else
		{
			nulls[9] = false;
			values[9] = CStringGetTextDatum(eerel->eref->aliasname);
		}

		if (eepath->indexoid == 0)
		{
			nulls[10] = true;
			values[10] = (Datum) 0;
		}
		else
		{
			nulls[10] = false;
			values[10] = ObjectIdGetDatum(eepath->indexoid);
		}

		/* Создание и вставка тапла */
		tuple = heap_form_tuple(tupdesc, values, nulls);
		simple_heap_insert(rel, tuple);
		heap_freetuple(tuple);

	}

	FreeExecutorState(estate);
	table_close(rel, RowExclusiveLock);
}

/*
 *
 */
int64
insert_query_info_into_eequery(const char *queryString)
{
	Relation	rel;
	TupleDesc	tupdesc;
	HeapTuple	tuple;
	Datum		values[3];
	bool		nulls[3] = {false, false, false};
	EState	   *estate;
	int64		query_id;
	TimestampTz execution_ts;

	estate = CreateExecutorState();

	rel = table_openrv(makeRangeVar("ee", "query", -1), RowExclusiveLock);

	/* Get the tuple descriptor for the table */
	tupdesc = RelationGetDescr(rel);

	query_id = get_next_query_id();

	execution_ts = GetCurrentTimestamp();
		
	values[0] = Int64GetDatum(query_id);

	values[1] = DirectFunctionCall1(timestamptz_timestamp, TimestampTzGetDatum(execution_ts));

	values[2] = CStringGetTextDatum(queryString);

	/* Создание и вставка тапла */
	tuple = heap_form_tuple(tupdesc, values, nulls);
	simple_heap_insert(rel, tuple);
	heap_freetuple(tuple);

	FreeExecutorState(estate);
	table_close(rel, RowExclusiveLock);

	return query_id;
}
