
#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scriptcore.h"

//#define DEBUGHORSEMEMRESIZE

#define HORSEMEMCONF_POOLAREAS_WANT_FREE 2
#define HORSEMEMCONF_POOLAREA_ITEMS 256
#define HORSEMEMCONF_LARGEITEMSHEAP_WANT_FREEBYTES (1024ULL * 1024ULL)
#define HORSEMEMCONF_LARGEITEMSHEAP_SIZE (1024ULL * 1024ULL * 50ULL)
#define HORSEMEMCONF_LARGEITEMSHEAP_WANT_FREESLOTS 64

typedef struct horseheappoolarea {
    char *area;
    int occupiedslotcount;
    int *occupiedslots;
} horseheappoolarea;

typedef struct horseheappool {
    int singleitemsize;
    int poolareaitemcount;
    int poolareas_count;
    horseheappoolarea **poolareas_ptrs;
} horseheappool;

typedef struct horseheapptrinfo {
    uint64_t alloc_size;
    int poolindex;
    int16_t poolareaindex;
    int16_t poolareaslotindex;
    int largeheapindex;
    int largeheapslotindex;
} horseheapptrinfo;

typedef struct horseheaplargeitemsheap {
    char *heap;
    int64_t heap_length;
    int itemsinusecount;
    int slotcount;
    size_t *slotsize;
    int64_t *slotoffset;
} horseheaplargeitemsheap;

typedef struct horseheap {
    int64_t memory_use;

    horseheaplargeitemsheap **largeitemsheap;
    int largeitemsheap_count;

    int poolscount;
    horseheappool *pools;
} horseheap;

static int horseheap_chunksizes[] = {
    32, 64, 256, 1024, 4096, 0
};

static int _horseheap_RequireHeapMinsize(
        horseheap *hheap, uint64_t freebytes,
        int freeslots
        ) {
    assert(hheap);
    assert(hheap->largeitemsheap_count == 0 ||
           hheap->largeitemsheap);
    if (freebytes > HORSEMEMCONF_LARGEITEMSHEAP_SIZE / 3)
        freebytes = HORSEMEMCONF_LARGEITEMSHEAP_SIZE / 3;
    while (1) {
        int total_free_slots = 0;
        int64_t guaranteed_free_continuous_area = 0;
        int i = hheap->largeitemsheap_count - 1;
        while (i >= 0) {
            total_free_slots += (
                hheap->largeitemsheap[i]->slotcount -
                hheap->largeitemsheap[i]->itemsinusecount
            );
            int64_t end_area = (
                hheap->largeitemsheap[i]->heap_length
            );
            if (hheap->largeitemsheap[i]->itemsinusecount > 0) {
                int64_t lastoffset = hheap->largeitemsheap[i]->slotoffset[
                    hheap->largeitemsheap[i]->itemsinusecount - 1
                ];
                size_t lastsize = hheap->largeitemsheap[i]->slotsize[
                    hheap->largeitemsheap[i]->itemsinusecount - 1
                ];
                end_area -= (int64_t)(lastoffset + lastsize);
            }
            if (end_area > guaranteed_free_continuous_area)
                guaranteed_free_continuous_area = end_area;
            if (total_free_slots > freeslots &&
                    guaranteed_free_continuous_area > freebytes)
                return 1;
            i--;
        } 
        if (guaranteed_free_continuous_area < freebytes) {
            horseheaplargeitemsheap *newlargeitems = malloc(
                sizeof(*newlargeitems)
            );
            if (!newlargeitems)
                return 0;
            memset(newlargeitems, 0, sizeof(*newlargeitems));
            newlargeitems->heap = malloc(
                HORSEMEMCONF_LARGEITEMSHEAP_SIZE
            );
            newlargeitems->heap_length = HORSEMEMCONF_LARGEITEMSHEAP_SIZE;
            if (!newlargeitems->heap)
                goto errorfreenewlih;
            newlargeitems->slotcount = 32;
            newlargeitems->slotsize = malloc(
                sizeof(*newlargeitems->slotsize) * 32
            );
            if (!newlargeitems->slotsize)
                goto errorfreenewlih;
            memset(
                newlargeitems->slotsize, 0,
                sizeof(*newlargeitems->slotsize) * 32
            );
            newlargeitems->slotoffset = malloc(
                sizeof(*newlargeitems->slotoffset) * 32
            );
            if (!newlargeitems->slotoffset)
                goto errorfreenewlih;
            memset(
                newlargeitems->slotoffset, 0,
                sizeof(*newlargeitems->slotoffset) * 32
            );

            horseheaplargeitemsheap **largeitemsheap_new = realloc(
                hheap->largeitemsheap,
                sizeof(*largeitemsheap_new) *
                (hheap->largeitemsheap_count + 1)
            );
            if (!largeitemsheap_new) {
                errorfreenewlih:
                if (newlargeitems) {
                    if (newlargeitems->slotoffset)
                        free(newlargeitems->slotoffset);
                    if (newlargeitems->slotsize)
                        free(newlargeitems->slotsize);
                    if (newlargeitems->heap)
                        free(newlargeitems->heap);
                    free(newlargeitems);
                }
                return 0;
            }

            hheap->largeitemsheap = largeitemsheap_new; 
            largeitemsheap_new[hheap->largeitemsheap_count] = (
                newlargeitems
            );
            hheap->largeitemsheap_count++;
            #if defined(DEBUGHORSEMEMRESIZE) && !defined(NDEBUG)
            printf("horse3d/horseheap.c: verbose: alloc new LIH\n");
            #endif
            continue;
        }
        if (total_free_slots < freeslots) {
            int wantedspace = round((
                freeslots / (double)hheap->largeitemsheap_count
            ) + 0.6);
            if (wantedspace < 1)
                wantedspace = 1;
            int haveaddedslots = 0;
            i = 0;
            while (i < hheap->largeitemsheap_count) {
                horseheaplargeitemsheap *lih = (
                    hheap->largeitemsheap[i]
                );
                int oldcount = lih->slotcount;
                if (lih->itemsinusecount + wantedspace <
                        lih->slotcount) {
                    i++;
                    continue;
                }
                size_t *slotsize_new = realloc(
                    lih->slotsize,
                    sizeof(*(lih->slotsize)) *
                    (lih->itemsinusecount + wantedspace)
                );
                if (!slotsize_new)
                    return 0;
                lih->slotsize = slotsize_new;
                size_t *slotoffset_new = realloc(
                    lih->slotoffset,
                    sizeof(*(lih->slotoffset)) *
                    (lih->itemsinusecount + wantedspace)
                );
                if (!slotoffset_new)
                    return 0;
                haveaddedslots = 1;
                lih->slotoffset = slotoffset_new;
                lih->slotcount = lih->itemsinusecount + wantedspace;
                int k = oldcount;
                while (k < lih->slotcount) {
                    lih->slotsize[k] = 0;
                    lih->slotoffset[k] = 0;
                    k++;
                }
                i++;
            }
            #if defined(DEBUGHORSEMEMRESIZE) && !defined(NDEBUG)
            printf("horse3d/horseheap.c: verbose: "
                   "alloc new LIH slots\n");
            #endif
            assert(haveaddedslots);
            continue;
        }
        break;
    }
    return 1;
}

static int _horseheap_RequireFreePoolAreas(
        horseheap *hheap, int poolindex,
        int wantfreepools
        ) {
    int occupiedareas = 0;
    int freeareas = 0;
    int allocedareas = hheap->pools[poolindex].poolareas_count;
    int i = 0;
    while (i < allocedareas) {
        if (hheap->pools[poolindex].poolareas_ptrs[i] &&
                hheap->pools[poolindex].poolareas_ptrs[i]
                    ->occupiedslots == 0) {
            freeareas++;
        } else {
            occupiedareas++;
        }
        i++;
    }
    if (allocedareas - occupiedareas < wantfreepools) {
        #if defined(DEBUGHORSEMEMRESIZE) && !defined(NDEBUG)
        printf("horse3d/horseheap.c: verbose: "
               "alloc %d new pool areas for pool%d\n",
               (wantfreepools + occupiedareas) - allocedareas,
               hheap->pools[poolindex].singleitemsize);
        #endif
        horseheappoolarea **poolareas_ptrs_new = realloc(
            hheap->pools[poolindex].poolareas_ptrs,
            (wantfreepools + occupiedareas) *
            sizeof(*(hheap->pools[poolindex].poolareas_ptrs)) 
        );
        if (!poolareas_ptrs_new)
            return 0;
        int oldcount = allocedareas;
        hheap->pools[poolindex].poolareas_count = (
            wantfreepools + occupiedareas
        );
        allocedareas = wantfreepools + occupiedareas;
        hheap->pools[poolindex].poolareas_ptrs = poolareas_ptrs_new;
        i = oldcount;
        while (i < allocedareas) {
            hheap->pools[poolindex].poolareas_ptrs[i] = NULL;
            i++;
        }
    }
    i = 0;
    while (i < allocedareas) {
        if (hheap->pools[poolindex].poolareas_ptrs[i]) {
            i++;
            continue;
        }
        horseheappoolarea *newarea = malloc(sizeof(*newarea));
        if (!newarea)
            return 0;
        memset(newarea, 0, sizeof(*newarea));
        newarea->area = malloc(
            hheap->pools[poolindex].singleitemsize *
            hheap->pools[poolindex].poolareaitemcount
        );
        if (!newarea->area) {
            free(newarea);
            return 0;
        }
        newarea->occupiedslots = malloc(
            sizeof(*newarea->occupiedslots) *
            hheap->pools[poolindex].poolareaitemcount
        );
        if (!newarea->occupiedslots) {
            free(newarea->area);
            free(newarea);
            return 0;
        }
        memset(
            newarea->occupiedslots, 0,
            sizeof(*newarea->occupiedslots) *
            hheap->pools[poolindex].poolareaitemcount
        );
        hheap->pools[poolindex].poolareas_ptrs[i] = newarea;
        i++;
    }
    return 1;
}

int horseheap_EnsureHeadroom(horseheap *hheap) {
    if (!hheap->pools) {
        int sizecount = 0;
        while (horseheap_chunksizes[sizecount] > 0) {
            sizecount++;
        }
        if (sizecount <= 0)
            return 0;
        hheap->pools = malloc(
            sizecount * sizeof(*(hheap->pools))
        );
        if (!hheap->pools)
            return 0;
        memset(hheap->pools, 0, sizeof(*(hheap->pools)) * sizecount);
        hheap->poolscount = sizecount;
        int i = 0;
        while (i < (int)hheap->poolscount) {
            hheap->pools[i].singleitemsize = horseheap_chunksizes[i];
            hheap->pools[i].poolareaitemcount = (
                HORSEMEMCONF_POOLAREA_ITEMS
            );
            i++;
        }
    }
    int i = 0;
    while (i < (int)hheap->poolscount) {
        if (!_horseheap_RequireFreePoolAreas(
                hheap, i, HORSEMEMCONF_POOLAREAS_WANT_FREE
                ))
            return 0;
        i++;
    }
    if (!_horseheap_RequireHeapMinsize(
            hheap, HORSEMEMCONF_LARGEITEMSHEAP_WANT_FREEBYTES,
            HORSEMEMCONF_LARGEITEMSHEAP_WANT_FREESLOTS))
        return 0;
    return 1;
}

int horseheap_EnsureCanAllocSize(horseheap *heap, size_t size) {
    if (!horseheap_EnsureHeadroom(heap))
        return 0;

    // If this fits any pool then we are done:
    int sizecount = 0;
    while (horseheap_chunksizes[sizecount] > 0) {
        if ((size_t)horseheap_chunksizes[sizecount] >= size)
            return 1;
        sizecount++;
    }

    // Ok, see if we can fit this onto the large heap:
    if (size > HORSEMEMCONF_LARGEITEMSHEAP_SIZE / 3)
        return 0;  // cannot guarantee this one.
    return _horseheap_RequireHeapMinsize(
        heap, size, 1
    );
}

static int horseheap_PrepareHeap(horseheap *hheap) {
    if (!hheap)
        return 0;

    if (!horseheap_EnsureHeadroom(hheap))
        return 0;

    return 1;
}

int horsheap_GetPtrInfo(
        horseheap *hheap, void *ptr, horseheapptrinfo *ptrinfo
        ) {
    if (!hheap)
        return 0;

    memset(ptrinfo, 0, sizeof(*ptrinfo));
    int i = 0;
    while (i < hheap->poolscount) {
        int itemsize = hheap->pools[i].singleitemsize;
        int areasize = (
            itemsize * hheap->pools[i].poolareaitemcount
        );
        int k = 0;
        while (k < hheap->pools[i].poolareas_count) {
            char *poolarea = hheap->pools[i].poolareas_ptrs[k]->area;
            if ((char*)ptr >= poolarea &&
                    (char*)ptr < poolarea + areasize) {
                uint64_t offset = ((char*)ptr - poolarea);
                assert(offset % itemsize == 0);
                memset(ptrinfo, 0, sizeof(*ptrinfo));
                ptrinfo->largeheapindex = -1;
                ptrinfo->poolindex = i;
                ptrinfo->poolareaindex = k;
                ptrinfo->poolareaslotindex = (offset / itemsize);
                ptrinfo->alloc_size = itemsize;
                return 1;
            }
            k++;
        }
        i++;
    }
    i = 0;
    while (i < hheap->poolscount) {
        if ((char*)ptr >= hheap->largeitemsheap[i]->heap &&
                (char*)ptr < hheap->largeitemsheap[i]->heap +
                hheap->largeitemsheap[i]->heap_length) {
            uint64_t offset = (
                (char*)ptr - (char*)hheap->largeitemsheap[i]->heap
            );
            int k = 0;
            while (k < hheap->largeitemsheap[i]->slotcount) {
                if (hheap->largeitemsheap[i]->slotoffset[k] == offset) {
                    memset(ptrinfo, 0, sizeof(*ptrinfo));
                    ptrinfo->poolindex = -1;
                    ptrinfo->largeheapindex = i;
                    ptrinfo->largeheapslotindex = k;
                    ptrinfo->alloc_size = (
                        hheap->largeitemsheap[i]->slotsize[k]
                    );
                    #ifndef NDEBUG
                    {
                        int largestpooledsize = 0;
                        int i2 = 0;
                        while (horseheap_chunksizes[i2] != 0) {
                            if (horseheap_chunksizes[i2] > largestpooledsize)
                                largestpooledsize = horseheap_chunksizes[i2];
                            i2++;
                        }
                        assert(ptrinfo->alloc_size > largestpooledsize);
                    }
                    #endif
                    return 1;
                } else if (hheap->largeitemsheap[i]->
                           slotoffset[k] > offset) {
                    break;
                }
                k++;
            }
            break;
        }
        i++;
    }
    return 0;
}

static void horseheap_PrintStats(horseheap *hheap) {
    if (!hheap)
        return;

    printf("horse3d/horseheap.c: debug: "
           "state %p: total use: %" PRId64 "\n",
           hheap, (int64_t)hheap->memory_use);
    int i = 0;
    while (i < hheap->poolscount) {
        printf(
            "horse3d/horseheap.c: debug: state %p: "
            " #%d pool%d item slots: %d\n",
            hheap, i, hheap->pools[i].singleitemsize,
            hheap->pools[i].poolareaitemcount *
            hheap->pools[i].poolareas_count
        ); 
        i++;
    }
}

static void horseheap_FreePtr(
        horseheap *hheap,
        void *ptr,
        horseheapptrinfo *ptrinfo
        ) {
    if (ptrinfo->largeheapindex >= 0) {
        assert(ptrinfo->poolindex < 0);
        assert(ptrinfo->largeheapslotindex >= 0);
        #ifndef NDEBUG
        {
            int largestpooledsize = 0;
            int i = 0;
            while (horseheap_chunksizes[i] != 0) {
                if (horseheap_chunksizes[i] > largestpooledsize)
                    largestpooledsize = horseheap_chunksizes[i];
                i++;
            }
            assert(ptrinfo->alloc_size > largestpooledsize);
        }
        #endif
        assert(ptrinfo->largeheapindex < hheap->largeitemsheap_count);
        assert(ptrinfo->largeheapslotindex <
               hheap->largeitemsheap[ptrinfo->largeheapindex]
                   ->itemsinusecount);
        assert(
            (uintptr_t)hheap->largeitemsheap[ptrinfo->largeheapindex]->heap +
            (ptrdiff_t)hheap->largeitemsheap[
                ptrinfo->largeheapindex
            ]->slotoffset[ptrinfo->largeheapslotindex] == (uintptr_t)ptr
        );
        assert(
            hheap->largeitemsheap[
                ptrinfo->largeheapindex
            ]->slotsize[ptrinfo->largeheapslotindex] > 0
        );
        int k = ptrinfo->largeheapslotindex;
        if (k < hheap->largeitemsheap[ptrinfo->largeheapindex]
                ->itemsinusecount - 1) {
            memmove(
                &hheap->largeitemsheap[
                    ptrinfo->largeheapindex
                ]->slotoffset[k],
                &hheap->largeitemsheap[
                    ptrinfo->largeheapindex
                ]->slotoffset[k + 1],
                sizeof(*hheap->largeitemsheap[
                    ptrinfo->largeheapindex
                ]->slotoffset) * (
                hheap->largeitemsheap[ptrinfo->largeheapindex]
                    ->itemsinusecount - k - 1
                )
            );
            memmove(
                &hheap->largeitemsheap[
                    ptrinfo->largeheapindex
                ]->slotsize[k],
                &hheap->largeitemsheap[
                    ptrinfo->largeheapindex
                ]->slotsize[k + 1],
                sizeof(*hheap->largeitemsheap[
                    ptrinfo->largeheapindex
                ]->slotsize) * (
                hheap->largeitemsheap[ptrinfo->largeheapindex]
                    ->itemsinusecount - k - 1
                )
            );
        }
        hheap->largeitemsheap[
            ptrinfo->largeheapindex
            ]->slotsize[
                hheap->largeitemsheap[ptrinfo->largeheapindex]
                    ->itemsinusecount - 1
            ] = 0;
        hheap->largeitemsheap[
            ptrinfo->largeheapindex
            ]->slotoffset[
                hheap->largeitemsheap[ptrinfo->largeheapindex]
                    ->itemsinusecount - 1
            ] = 0;
        hheap->largeitemsheap[ptrinfo->largeheapindex]
            ->itemsinusecount--;
        return;
    } else if (ptrinfo->poolindex >= 0) {
        assert(ptrinfo->poolareaindex >= 0);
        assert(ptrinfo->poolindex < hheap->poolscount);
        assert(hheap->pools[ptrinfo->poolindex].poolareas_count >=
               ptrinfo->poolareaindex);
        assert(
            ptrinfo->poolareaslotindex >= 0 &&
            ptrinfo->poolareaslotindex < HORSEMEMCONF_POOLAREA_ITEMS
        );
        assert(hheap->pools[ptrinfo->poolindex].poolareas_ptrs[
                   ptrinfo->poolareaindex
               ]->occupiedslots[ptrinfo->poolareaslotindex]);
        hheap->pools[ptrinfo->poolindex].poolareas_ptrs[
           ptrinfo->poolareaindex
        ]->occupiedslots[ptrinfo->poolareaslotindex] = 0;
        hheap->pools[ptrinfo->poolindex].poolareas_ptrs[
           ptrinfo->poolareaindex
        ]->occupiedslotcount--;
        assert(
            hheap->pools[ptrinfo->poolindex].poolareas_ptrs[
                ptrinfo->poolareaindex
            ]->occupiedslotcount >= 0
        );
        return;
    } else {
        fprintf(
            stderr, "horse3d/horseheap.c: corruption: "
            "failed to free pointer %p "
            "on heap %p (unknown pointer type?)\n",
            ptr, hheap
        );
        return;
    }
}

void *horseheap_MallocToPool(
        horseheap *hheap, size_t new_size, int poolindex
        ) {
    int itemsize = hheap->pools[poolindex].singleitemsize;
    assert(itemsize >= new_size);
    int attempts = 0;
    while (attempts < 2) {
        // Find free pool slot and take it:
        int k = 0;
        while (k < hheap->pools[poolindex].poolareas_count) {
            if (hheap->pools[poolindex].poolareas_ptrs[k]
                        ->occupiedslotcount
                    < hheap->pools[poolindex].poolareaitemcount) {
                int j = 0;
                while (j < hheap->pools[poolindex].poolareaitemcount) {
                    assert(
                        hheap->pools[poolindex].poolareas_ptrs[k]
                    );
                    assert(
                        hheap->pools[poolindex].poolareas_ptrs[k]->
                            occupiedslots
                    );
                    if (!hheap->pools[poolindex].poolareas_ptrs[k]->
                            occupiedslots[j]) {
                        hheap->pools[poolindex].poolareas_ptrs[k]->
                            occupiedslots[j] = 1;
                        hheap->pools[poolindex].poolareas_ptrs[k]->
                            occupiedslotcount++;
                        char *ptr = (char*)(
                            hheap->pools[poolindex].poolareas_ptrs[k]->area
                        ) + (horseheap_chunksizes[poolindex] * j);
                        return ptr;
                    }
                    j++;
                }
                fprintf(
                    stderr, "horse3d/horseheap.c: error: "
                    "malformed heap, pool area occupiedslotcount is not "
                    "maxed out but no slot free?\n"
                );
                assert(
                    hheap->pools[poolindex].poolareas_ptrs[k]
                        ->occupiedslotcount
                    >= hheap->pools[poolindex].poolareaitemcount
                );
                return NULL;
            }
            k++;
        }
        // If we arrived here we failed to find a free slot.
        if (!_horseheap_RequireFreePoolAreas(
                hheap, poolindex, 16
                )) {  // Require more free space.
            return NULL;
        }
        attempts++;  // Retry.
    }
    return NULL;
}

void *_horseheap_MallocDo(horseheap *hheap, size_t new_size) {
    if (new_size <= 0)
        return NULL;
    // Try allocating via pool if small enough:
    int i = 0;
    while (horseheap_chunksizes[i] > 0) {
        assert(horseheap_chunksizes[i] % alignof(max_align_t) == 0);
        if (horseheap_chunksizes[i] >= new_size) {
            assert(hheap->pools[i].singleitemsize == horseheap_chunksizes[i]);
            void *ptr = horseheap_MallocToPool(
                hheap, new_size, i
            );
            #if defined(DEBUGHORSEMEMRESIZE) && !defined(NDEBUG)
            if (!ptr)
                printf(
                    "horse3d/horseheap.c: verbose: "
                    "malloc(%d) on #%d pool%d returning "
                    "NULL\n", (int)new_size, i,
                    horseheap_chunksizes[i]
                );
            #endif
            return ptr;
        }
        i++;
    }
    // If we arrived here, it didn't fit any pool.

    // To put this on the large items heap, the size must be aligned
    // (so further items on the large items heap stay aligned)
    int align_diff = alignof(max_align_t) - (
        (new_size % alignof(max_align_t))
    );
    assert(align_diff >= 0);
    new_size += align_diff;
    assert(new_size % alignof(max_align_t) == 0);

    // Now put on large items heap:
    int attempts = 0;
    while (attempts < 2) {
        i = 0;
        while (i < hheap->largeitemsheap_count) {
            if (hheap->largeitemsheap[i]->itemsinusecount >=
                    hheap->largeitemsheap[i]->slotcount) {
                i++;
                continue;
            }
            uint64_t offset = 0;
            int k = 0;
            while (k < hheap->largeitemsheap[i]->slotcount) {
                if (hheap->largeitemsheap[i]->slotsize[k] == 0) {
                    uint64_t space_after = (
                        hheap->largeitemsheap[i]->heap_length - offset
                    );
                    int j = k + 1;
                    while (j < hheap->largeitemsheap[i]->slotcount) {
                        if (hheap->largeitemsheap[i]->slotsize[j] == 0) {
                            j++;
                            continue;
                        }
                        assert(hheap->largeitemsheap[i]->slotsize[j] >=
                               offset);
                        space_after = (
                            (uint64_t)hheap->largeitemsheap[i]
                                ->slotoffset[j] -
                            offset
                        );
                        break;
                    }
                    if (space_after >= new_size) {
                        hheap->largeitemsheap[i]->slotsize[k] = new_size;
                        hheap->largeitemsheap[i]->slotoffset[k] = offset;
                        hheap->largeitemsheap[i]->itemsinusecount++;
                        return (void*)(
                            hheap->largeitemsheap[i]->heap + offset
                        );
                    } 
                }
                if (offset <= hheap->largeitemsheap[i]->slotoffset[k]) {
                    offset = (
                        (uint64_t)hheap->largeitemsheap[i]
                            ->slotoffset[k] +
                        (uint64_t)hheap->largeitemsheap[i]
                            ->slotsize[k]
                    );
                }
                k++;
            }
            i++;
        }
        attempts++;
    }
    return NULL;
}

void *horseheap_Malloc_Verified(horseheap *hheap, size_t new_size) {
    void *p = _horseheap_MallocDo(hheap, new_size);
    if (p && new_size > 0)
        memset(p, 1, new_size);  // write to make sure that won't crash
    else if (!p)
        #if defined(DEBUGHORSEMEMRESIZE) && !defined(NDEBUG)
        printf("horse3d/horseheap.c: verbose: "
               "malloc(%d) returning NULL\n", (int)new_size);
        #endif
    #if defined(DEBUGHORSEMEMRESIZE) && !defined(NDEBUG)
    printf("hors3d/horseheap.c: verbose: "
           "alloced: %p (size %d/end %p)\n",
           p, (int)new_size, p + new_size);
    horseheapptrinfo pinfo;
    assert(horsheap_GetPtrInfo(hheap, p, &pinfo));
    printf(
        "hors3d/horseheap.c: verbose: "
        "ptrinfo->largeheapindex: %d "
        "ptrinfo->largeheapslotindex: %d\n",
        pinfo.largeheapindex,
        pinfo.largeheapslotindex
    );
    printf(
        "hors3d/horseheap.c: verbose: "
        "ptrinfo->poolindex: %d "
        "ptrinfo->poolareaindex: %d "
        "ptrinfo->poolareaslotindex: %d\n",
        pinfo.poolindex,
        pinfo.poolareaindex,
        pinfo.poolareaslotindex
    );
    #endif
    return p;
}

void *horseheap_Realloc(
        horseheap *hheap, void *ptr, size_t new_size
        ) {
    horseheapptrinfo _pinfostore;
    horseheapptrinfo *pinfo = NULL;
    if (ptr) {
        if (!horsheap_GetPtrInfo(hheap, ptr, &_pinfostore)) {
            fprintf(stderr, "horseheap.c: error: "
                    "invalid pointer: %p - "
                    "not known to heap %p\n", ptr, hheap);
            return NULL;
        }
        pinfo = &_pinfostore;
        if (pinfo->alloc_size <= 0) {
            fprintf(stderr, "horseheap.c: error: "
                    "invalid pointer: %p - "
                    "has invalid dimensions on heap %p\n",
                    ptr, hheap);
            return NULL;
        }
    }

    // Quick paths:
    if (ptr && new_size > 0  // Quick non-move resize path:
            && pinfo->alloc_size >= new_size)
        return ptr;
    if (ptr && new_size == 0) {  // Quick free() path:
        horseheap_FreePtr(hheap, ptr, pinfo);
        return NULL;
    }
    if (!ptr && new_size > 0)  // Quick new-alloc path:
        #if defined(DEBUGHORSEMEMRESIZE) && !defined(NDEBUG)
        return horseheap_Malloc_Verified(hheap, new_size);
        #else
        return _horseheap_MallocDo(hheap, new_size);
        #endif

    // Allocate new slot and move memory over:
    int copy_size = new_size;
    if (copy_size > pinfo->alloc_size)
        copy_size = pinfo->alloc_size;
    char *ptr2 = NULL;
    #if defined(DEBUGHORSEMEMRESIZE) && !defined(NDEBUG)
    ptr2 = horseheap_Malloc_Verified(hheap, new_size);
    #else
    ptr2 = _horseheap_MallocDo(hheap, new_size);
    #endif
    if (!ptr2)
        return NULL;
    memmove(ptr2, ptr, copy_size);

    // Free old pointer and return result:
    horseheap_FreePtr(hheap, ptr, pinfo);
    return ptr2;
}

void horseheap_Free(horseheap *hheap, void *ptr) {
    horseheap_Realloc(hheap, ptr, 0);
}

void *horseheap_Malloc(horseheap *hheap, size_t size) {
    return horseheap_Realloc(hheap, NULL, size);
}

void horseheap_Destroy(horseheap *hheap) {
    
}

horseheap *horseheap_New() {
    horseheap *hp = malloc(sizeof(*hp));
    if (!hp)
        return NULL;
    memset(hp, 0, sizeof(*hp));
    if (!horseheap_PrepareHeap(hp))
        horseheap_Destroy(hp);

    return hp;
}
