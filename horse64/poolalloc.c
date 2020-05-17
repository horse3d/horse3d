
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "poolalloc.h"

#define POOLAREAITEMS 256

typedef struct poolsubarea {
    char *poolarea;
    int *slotused;
    int possiblyfreeindex;
} poolsubarea;

typedef struct poolalloc {
    int allocsize;
    int areas_count;
    poolsubarea *areas;
    int lastusedareaindex;

    int64_t totalitems;
    int64_t freeitems;
} poolalloc;


int poolalloc_AddArea(poolalloc *pool) {
    poolsubarea *new_areas = realloc(
        pool->areas, sizeof(*pool->areas) * (
        pool->lastusedareaindex + 1));
    if (!new_areas)
        return 0;
    pool->areas = new_areas;
    pool->areas_count++;
    memset(&pool->areas[pool->areas_count - 1], 0, sizeof(*pool->areas));
    pool->areas[pool->areas_count - 1].poolarea = malloc(
        POOLAREAITEMS * pool->allocsize
    );
    if (!pool->areas[pool->areas_count - 1].poolarea) {
        pool->areas_count--;
        return 0;
    }
    pool->areas[pool->areas_count - 1].slotused = malloc(
        sizeof(*pool->areas[pool->areas_count - 1].slotused) *
        POOLAREAITEMS
    );
    if (!pool->areas[pool->areas_count - 1].slotused) {
        free(pool->areas[pool->areas_count - 1].poolarea);
        pool->areas_count--;
        return 0;
    }
    memset(pool->areas[pool->areas_count - 1].slotused, 0,
           sizeof(*pool->areas[pool->areas_count - 1].slotused));
    pool->freeitems += POOLAREAITEMS;
    pool->totalitems += POOLAREAITEMS;
    pool->lastusedareaindex = pool->areas_count - 1;
    return 1;
}

void poolalloc_Destroy(poolalloc *pool) {
    if (!pool)
        return;
    if (pool->areas) {
        int i = 0;
        while (i < pool->areas_count) {
            free(pool->areas[i].poolarea);
            free(pool->areas[i].slotused);
            i++;
        }
        free(pool->areas);
    }
    free(pool);
}

poolalloc *poolalloc_New(int itemsize) {
    if (itemsize <= 0)
        return NULL;
    poolalloc *pool = malloc(sizeof(*pool));
    if (!pool)
        return NULL;
    memset(pool, 0, sizeof(*pool));
    pool->allocsize = itemsize;
    if (!poolalloc_AddArea(pool)) {
        poolalloc_Destroy(pool);
        return NULL;
    }
    return pool;
}

void poolalloc_free(poolalloc *pool, void *ptr) {
    int j = 0;
    while (j < pool->areas_count) {
        int k = 0;
        while (k < POOLAREAITEMS) {
            if ((char*)ptr >= pool->areas[j].poolarea &&
                    (char*)ptr < pool->areas[j].poolarea +
                    pool->allocsize * POOLAREAITEMS) {
                int64_t offset = ((char*)ptr - pool->areas[j].poolarea);
                offset /= pool->allocsize;
                assert(offset >= 0 && offset < POOLAREAITEMS);
                assert(pool->areas[j].slotused[offset]);
                pool->areas[j].slotused[offset] = 0;
                pool->areas[j].possiblyfreeindex = offset;
                pool->lastusedareaindex = j;
                pool->freeitems++;
                assert(pool->freeitems <= pool->totalitems);
                return;
            }
            k++;
        }
        j++;
    }
    assert(0 && "failed to process free of poolalloc ptr");
}

void *poolalloc_malloc(poolalloc *pool, int can_use_emergency_margin) {
    if (!can_use_emergency_margin && pool->freeitems < 10) {
        if (!poolalloc_AddArea(pool))
            return 0;
    }
    if (pool->freeitems <= 0)
        return 0;
    if (pool->lastusedareaindex >= 0 &&
            pool->lastusedareaindex < pool->areas_count) {
        int k = pool->areas[pool->lastusedareaindex].possiblyfreeindex;
        if (k >= 0 && k < POOLAREAITEMS &&
                !pool->areas[pool->lastusedareaindex].slotused[k]) {
            pool->freeitems--;
            assert(pool->freeitems >= 0);
            pool->areas[pool->lastusedareaindex].slotused[k] = 1;
            pool->areas[pool->lastusedareaindex].possiblyfreeindex = k + 1;
            return pool->areas[pool->lastusedareaindex].poolarea + (
                pool->allocsize * k
            );
        }
        k = 0;
        while (k < POOLAREAITEMS) {
            if (!pool->areas[pool->lastusedareaindex].slotused[k]) {
                pool->freeitems--;
                assert(pool->freeitems >= 0);
                pool->areas[pool->lastusedareaindex].slotused[k] = 1;
                pool->areas[pool->lastusedareaindex].
                    possiblyfreeindex = k + 1;
                return pool->areas[pool->lastusedareaindex].poolarea + (
                    pool->allocsize * k
                );
            }
            k++;
        }
    }
    int j = 0;
    while (j < pool->areas_count) {
        int k = 0;
        while (k < POOLAREAITEMS) {
            if (!pool->areas[j].slotused[k]) {
                pool->freeitems--;
                assert(pool->freeitems >= 0);
                pool->lastusedareaindex = j;
                pool->areas[j].slotused[k] = 1;
                pool->areas[j].possiblyfreeindex = k + 1;
                return pool->areas[j].poolarea + (
                    pool->allocsize * k
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
