//#include "output_result.h"

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

// ExplainNode????
static const char *
EEPathTypeToString(NodeTag pathtype)
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
        default:
            return "Unknown";
    }
}


void
FillPathsTable(EERel *eerel)
{
    Relation    rel;
    TupleDesc   tupdesc;
    HeapTuple   tuple;
    Datum       values[8];
    bool        nulls[8] = {false};
    ListCell   *lc;
    EState     *estate;
    MemoryContext oldcontext;

	/* Create execution state */
	estate = CreateExecutorState();

    /* Open the temporary table for writing */
    rel = table_openrv(makeRangeVar("ee", "ee_result2", -1), RowExclusiveLock);

    /* Get the tuple descriptor for the table */
    tupdesc = RelationGetDescr(rel);

    /* For each path in the pathlist */
    foreach(lc, eerel->eepath_list)
    {
        EEPath    *eepath = (EEPath *) lfirst(lc);
        
        /* If EEPath has child paths, prepare the array 
        if (eepath->children)  
        {
            ListCell   *lc2;
            
            foreach(lc2, eepath->children)
            {
                EEPath    *child = (EEPath *) lfirst(lc2);
                astate = accumArrayResult(astate, 
                                        CStringGetTextDatum(EEPathTypeToString(child->pathtype)),
                                        false,
                                        TEXTOID,
                                        CurrentMemoryContext);
            }
        }
		*/
        /* Fill the values array */
		values[0] = Int32GetDatum(eepath->level);

        //values[1] = CStringGetTextDatum(eepath->path_name);
		values[1] = Int64GetDatum(eepath->id);

        values[2] = CStringGetTextDatum(EEPathTypeToString(eepath->pathtype));

       // values[3] = astate ? makeArrayResult(astate, CurrentMemoryContext) 
         //                 : PointerGetDatum(construct_empty_array(TEXTOID));

		if (eepath->nsub == 0)
		{
			nulls[3] = true;  
			values[3] = (Datum) 0; 

		}
		else if (eepath->nsub == 1)
		{
			values[3] = PointerGetDatum(construct_array(
				(Datum[]){
					Int64GetDatum(eepath->sub_eepath_1->id)
				},
				1,  
				INT8OID,  
				8,
				true, 
				'd'  
			));

		}
		else if (eepath->nsub == 2)
		{
			values[3] = PointerGetDatum(construct_array(
				(Datum[]){
					Int64GetDatum(eepath->sub_eepath_1->id),
					Int64GetDatum(eepath->sub_eepath_2->id)
				},
				2,  
				INT8OID,  
				8,  
				true,  
				'd'  
			));
		}

        values[4] = Float8GetDatum(eepath->startup_cost);
        values[5] = Float8GetDatum(eepath->total_cost);
        values[6] = Int64GetDatum(eepath->rows);
        values[7] = BoolGetDatum(eepath->is_del);
		//nulls[7] = true;
		//values[7] = (Datum) 0;

		oldcontext = MemoryContextSwitchTo(estate->es_query_cxt);

        /* Create and insert the tuple */
        tuple = heap_form_tuple(tupdesc, values, nulls);
        simple_heap_insert(rel, tuple);
        heap_freetuple(tuple);

    	MemoryContextSwitchTo(oldcontext);
    }

    /* Clean up */
    FreeExecutorState(estate);
    table_close(rel, RowExclusiveLock);
}