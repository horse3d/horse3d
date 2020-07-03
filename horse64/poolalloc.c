// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "poolalloc.h"

#define FIRSTPOOLSIZE 256

typedef struct pool {
    int item_count;
    char *poolarea;
    int *slotused;
    int possiblyfreeindex;
} pool;

typedef struct poolalloc {
    int allocsize;
    int pools_count;
    pool *pools;
    int lastusedareaindex;

    int64_t totalitems;
    int64_t freeitems;
} poolalloc;


int poolalloc_AddArea(poolalloc *poolac) {
    pool *new_areas = realloc(
        poolac->pools, sizeof(*poolac->pools) * (
        poolac->pools_count + 1));
    if (!new_areas)
        return 0;
    poolac->pools = new_areas;
    poolac->pools_count++;
    memset(
        &poolac->pools[poolac->pools_count - 1], 0,
        sizeof(*poolac->pools)
    );
    int pool_size = FIRSTPOOLSIZE;
    if (poolac->pools_count > 1) {
        assert(
            poolac->pools[poolac->pools_count - 2].
            item_count > 0
        );
        pool_size = poolac->pools[poolac->pools_count - 2].
            item_count * 2;
    }
    assert(pool_size > 0);
    poolac->pools[poolac->pools_count - 1].item_count = pool_size;
    poolac->pools[poolac->pools_count - 1].poolarea = malloc(
        pool_size * poolac->allocsize
    );
    if (!poolac->pools[poolac->pools_count - 1].poolarea) {
        poolac->pools_count--;
        return 0;
    }
    poolac->pools[poolac->pools_count - 1].slotused = malloc(
        sizeof(*poolac->pools[poolac->pools_count - 1].slotused) *
        pool_size
    );
    if (!poolac->pools[poolac->pools_count - 1].slotused) {
        free(poolac->pools[poolac->pools_count - 1].poolarea);
        poolac->pools_count--;
        return 0;
    }
    memset(
        poolac->pools[poolac->pools_count - 1].slotused, 0,
        sizeof(*poolac->pools[poolac->pools_count - 1].slotused) *
        pool_size
    );
    assert(pool_size > 0);
    poolac->freeitems += pool_size;
    poolac->totalitems += pool_size;
    poolac->lastusedareaindex = poolac->pools_count - 1;
    return 1;
}

void poolalloc_Destroy(poolalloc *poolac) {
    if (!poolac)
        return;
    if (poolac->pools) {
        int i = 0;
        while (i < poolac->pools_count) {
            free(poolac->pools[i].poolarea);
            free(poolac->pools[i].slotused);
            i++;
        }
        free(poolac->pools);
    }
    free(poolac);
}

poolalloc *poolalloc_New(int itemsize) {
    if (itemsize <= 0)
        return NULL;
    poolalloc *poolac = malloc(sizeof(*poolac));
    if (!poolac)
        return NULL;
    memset(poolac, 0, sizeof(*poolac));
    poolac->allocsize = itemsize;
    if (!poolalloc_AddArea(poolac)) {
        poolalloc_Destroy(poolac);
        return NULL;
    }
    return poolac;
}

void poolalloc_free(poolalloc *poolac, void *ptr) {
    int j = 0;
    while (j < poolac->pools_count) {
        const int c = poolac->pools[j].item_count;
        int k = 0;
        while (k < c) {
            if ((char*)ptr >= poolac->pools[j].poolarea &&
                    (char*)ptr < poolac->pools[j].poolarea +
                    poolac->allocsize * c) {
                int64_t offset = (
                    ((char*)ptr - poolac->pools[j].poolarea)
                );
                offset /= poolac->allocsize;
                assert(offset >= 0 && offset < c);
                assert(poolac->pools[j].slotused[offset]);
                poolac->pools[j].slotused[offset] = 0;
                poolac->pools[j].possiblyfreeindex = offset;
                poolac->lastusedareaindex = j;
                poolac->freeitems++;
                assert(poolac->freeitems <= poolac->totalitems);
                return;
            }
            k++;
        }
        j++;
    }
    assert(0 && "failed to process free of poolalloc ptr");
}

void *poolalloc_malloc(poolalloc *poolac,
                       int can_use_emergency_margin) {
    // Add more free items if necessary:
    if (!can_use_emergency_margin && poolac->freeitems < 10) {
        if (!poolalloc_AddArea(poolac))
            return 0;
    }
    if (poolac->freeitems <= 0)  // Adding new free items failed
        return 0;

    if (poolac->lastusedareaindex >= 0 &&
            poolac->lastusedareaindex < poolac->pools_count) {
        // Try to add into last used pool area:
        const int c = poolac->pools[poolac->lastusedareaindex].item_count;
        int k = poolac->pools[poolac->lastusedareaindex].possiblyfreeindex;
        if (k >= 0 && k < c &&
                !poolac->pools[poolac->lastusedareaindex].slotused[k]) {
            poolac->freeitems--;
            assert(poolac->freeitems >= 0);
            poolac->pools[poolac->lastusedareaindex].slotused[k] = 1;
            poolac->pools[poolac->lastusedareaindex].
                possiblyfreeindex = k + 1;
            return poolac->pools[poolac->lastusedareaindex].poolarea + (
                poolac->allocsize * k
            );
        }
        k = 0;
        while (k < c) {
            if (!poolac->pools[poolac->lastusedareaindex].slotused[k]) {
                poolac->freeitems--;
                assert(poolac->freeitems >= 0);
                poolac->pools[poolac->lastusedareaindex].slotused[k] = 1;
                poolac->pools[poolac->lastusedareaindex].
                    possiblyfreeindex = k + 1;
                return poolac->pools[poolac->lastusedareaindex].poolarea + (
                    poolac->allocsize * k
                );
            }
            k++;
        }
    }
    // See what pool area we can add this to:
    int j = 0;
    while (j < poolac->pools_count) {
        const int c = poolac->pools[j].item_count;
        int k = 0;
        while (k < c) {
            if (!poolac->pools[j].slotused[k]) {
                poolac->freeitems--;
                assert(poolac->freeitems >= 0);
                poolac->lastusedareaindex = j;
                poolac->pools[j].slotused[k] = 1;
                poolac->pools[j].possiblyfreeindex = k + 1;
                return poolac->pools[j].poolarea + (
                    poolac->allocsize * k
                );
            }
            k++;
        }
        j++;
    }
    assert(0 && "invalid pool, item used count is below"
           " total but no slot found");
    return NULL;
}
