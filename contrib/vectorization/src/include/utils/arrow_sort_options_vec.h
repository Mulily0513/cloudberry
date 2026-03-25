/*-------------------------------------------------------------------------
 *
 * arrow_sort_options_vec.h
 *	  Helpers for creating GArrowSortOptions with external-sort GUC settings.
 *
 * Portions Copyright (c) 2023-2025, HashData Technology Limited.
 *
 * IDENTIFICATION
 *		contrib/vectorization/src/include/utils/arrow_sort_options_vec.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef ARROW_SORT_OPTIONS_VEC_H
#define ARROW_SORT_OPTIONS_VEC_H

#include "arrow-glib/compute.h"

#include "utils/guc_vec.h"

static inline void
vec_apply_external_sort_options(GArrowSortOptions *sort_options)
{
	if (sort_options == NULL)
		return;

	garrow_sort_options_set_use_external_sort(sort_options, sort_use_external_sort);
	if (!sort_use_external_sort)
		return;

	garrow_sort_options_set_external_sort_batch_size(sort_options,
										 sort_external_batch_size);
	garrow_sort_options_set_external_sort_num_threads(sort_options,
										 sort_external_num_threads);
	garrow_sort_options_set_external_sort_max_rows(sort_options,
									  sort_external_max_rows);
	if (sort_external_temp_file_base != NULL &&
		sort_external_temp_file_base[0] != '\0')
	{
		garrow_sort_options_set_external_sort_temp_file_base(
			sort_options,
			sort_external_temp_file_base);
	}
}

static inline GArrowSortOptions *
vec_sort_options_new(GList *sort_keys,
				 guint64 limit,
				 gint64 take_thread_num,
				 gboolean two_phase_take)
{
	GArrowSortOptions *sort_options;

	sort_options = garrow_sort_options_new(sort_keys, limit,
							 take_thread_num, two_phase_take);
	vec_apply_external_sort_options(sort_options);
	return sort_options;
}

#endif   /* ARROW_SORT_OPTIONS_VEC_H */