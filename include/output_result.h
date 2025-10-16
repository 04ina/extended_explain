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

void		insert_eerel_into_eepaths(EERel * eerel, int64 query_id);

int64       insert_query_info_into_eequery(const char *queryString);

#endif							/* EE_OUTPUT_RESULT_H */
