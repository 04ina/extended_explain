/*-------------------------------------------------------------------------
 *
 * output_result.h
 *
 * IDENTIFICATION
 *        include/output_result.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef EE_OUTPUT_RESULT_H
#define EE_OUTPUT_RESULT_H

#include "extended_explain.h"

extern void insert_paths_into_eepaths(int64 query_id, EEState *ee_state);

extern int64 insert_query_info_into_eequery(const char *queryString);

#endif							/* EE_OUTPUT_RESULT_H */
