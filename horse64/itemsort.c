// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "itemsort.h"
#include "secrandom.h"
#include "valuecontentstruct.h"


typedef struct _itemsort_quicksortjob {
    int64_t start, end;
} _itemsort_quicksortjob;


#define SORT_PUSHJOB(start_idx, end_idx) \
    if (end_idx > start_idx) {\
        if (jobs_count + 1 > jobs_alloc) {\
            int64_t new_jobs_alloc = jobs_alloc * 32;\
            if (new_jobs_alloc < jobs_count + 16)\
                new_jobs_alloc = jobs_count + 16;\
            _itemsort_quicksortjob *jobs_new = NULL;\
            if (jobs_heap) {\
                jobs_new = realloc(\
                    jobs,\
                    sizeof(*jobs) * new_jobs_alloc\
                );\
            } else {\
                jobs_new = malloc(\
                    sizeof(*jobs) * new_jobs_alloc\
                );\
                if (jobs_new && jobs_count > 0)\
                    memcpy(jobs_new, jobs,\
                        sizeof(*jobs) * jobs_count);\
            }\
            if (!jobs_new) {\
                if (jobs_heap)\
                    free(jobs);\
                return 0;\
            }\
            jobs = jobs_new;\
        }\
        memset(&jobs[jobs_count], 0, sizeof(*jobs));\
        jobs[jobs_count].start = start_idx;\
        jobs[jobs_count].end = end_idx;\
        jobs_count++;\
    }\

#define SORT_GETITEM(idx) \
    (\
        ((char*) sortdata) + itemsize * (int64_t)(idx)\
    )

int itemsort_Do(
        void *sortdata, int64_t sortdatabytes, int64_t itemsize,
        int (*compareFunc)(void *item1, void *item2)
        ) {
    if (sortdatabytes <= itemsize)
        return 1;
    int64_t itemcount = (sortdatabytes / itemsize);
    _itemsort_quicksortjob _jobbuf[32];
    _itemsort_quicksortjob *jobs = _jobbuf;
    int64_t jobs_alloc = 32;
    int64_t jobs_heap = 0;
    int64_t jobs_count = 0;

    char switchbuf[64];
    if (itemsize > (int64_t)sizeof(switchbuf))
        return 0;

    SORT_PUSHJOB(0, itemcount);
    assert(jobs_count > 0);
    int64_t k = 0;
    while (k < jobs_count) {
        int64_t curr_start = jobs[k].start;
        int64_t curr_end = jobs[k].end;
        assert(curr_start <= curr_end);
        if (curr_end - curr_start <= 1) {
            k++;
            continue;
        }
        if (curr_end == curr_start + 2) {
            int cmp_1to2 = compareFunc(
                SORT_GETITEM(curr_start),
                SORT_GETITEM(curr_start + 1)
            );
            if (cmp_1to2 < -1) {
                if (jobs_heap)
                    free(jobs);
                return 0;
            }
            if (cmp_1to2 > 0) {
                // Need to switch.
                memcpy(switchbuf, SORT_GETITEM(curr_start + 1),
                       itemsize);
                memcpy(SORT_GETITEM(curr_start + 1),
                       SORT_GETITEM(curr_start), itemsize);
                memcpy(SORT_GETITEM(curr_start), switchbuf, itemsize);
            }
            k++;
            continue;
        }
        int64_t pivot = secrandom_RandIntRange(
            curr_start, curr_end
        );
        assert(pivot >= curr_start && pivot <= curr_end);
        
        int64_t z = curr_start;
        while (z < curr_end) {
            if (z == pivot) {
                z++;
                continue;
            }
            int cmp_1to2 = compareFunc(
                SORT_GETITEM(z),
                SORT_GETITEM(pivot)
            );
            if (cmp_1to2 < -1) {
                if (jobs_heap)
                    free(jobs);
                return 0;
            }
            if ((cmp_1to2 == 1 && z < pivot) ||
                    (cmp_1to2 == -1 && z > pivot)) {
                // Need to switch.
                if (z <= pivot + 1) {
                    // Direct swap is enough.
                    memcpy(switchbuf, SORT_GETITEM(z),
                        itemsize);
                    memcpy(SORT_GETITEM(z),
                        SORT_GETITEM(pivot), itemsize);
                    memcpy(SORT_GETITEM(pivot),
                        switchbuf, itemsize);
                    pivot = z;
                } else {
                    // Swap pivot with neighbor in right direction,
                    // then swap wrong element with that neighbor.
                    int64_t dir = (z - pivot);
                    assert(dir > 0);
                    assert(pivot + 1 < curr_end);
                    memcpy(switchbuf, SORT_GETITEM(pivot + 1),
                        itemsize);
                    memcpy(SORT_GETITEM(pivot + 1),
                        SORT_GETITEM(pivot), itemsize);
                    memcpy(SORT_GETITEM(pivot),
                        switchbuf, itemsize);
                    pivot += 1;
                    memcpy(switchbuf, SORT_GETITEM(pivot - 1),
                        itemsize);
                    memcpy(SORT_GETITEM(pivot - 1),
                        SORT_GETITEM(z), itemsize);
                    memcpy(SORT_GETITEM(z),
                        switchbuf, itemsize);
                }
            }
            z++;
        }
        SORT_PUSHJOB(curr_start, pivot);
        SORT_PUSHJOB(pivot, curr_end);
        k++;
    }

    if (jobs_heap)
        free(jobs);

    return 1;
}