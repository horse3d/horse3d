// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "bytecode.h"
#include "gcvalue.h"
#include "valuecontentstruct.h"
#include "vmlist.h"

genericlist *vmlist_New() {
    genericlist *l = malloc(sizeof(*l));
    if (!l)
        return NULL;
    memset(l, 0, sizeof(*l));

    l->first_block = malloc(sizeof(_listblock_minisize));
    if (!l->first_block) {
        free(l);
        return NULL;
    }
    l->first_block->next_block = NULL;
    l->first_block->entry_count = 0;
    l->last_block = l->first_block;
    l->list_block_count = 1;
    l->first_block_shrunk_size = LISTBLOCK_MINSIZE;
    return l;
}

int vmmap_IterateValues(
        genericlist *l, void *userdata,
        int (*cb)(void *udata, valuecontent *value)
        ) {
    listblock *block = l->first_block;
    while (block) {
        int i = 0;
        while (i < block->entry_count) {
            if (!cb(userdata, &block->entry_values[i]))
                return 0;
            i++;
        }
        block = block->next_block;
    }
    return 1;
}

int vmlist_Contains(genericlist *l, valuecontent *v, int *oom) {
    *oom = 0;
    listblock *block = l->first_block;
    while (block) {
        int i = 0;
        while (i < block->entry_count) {
            int inneroom = 0;
            if (valuecontent_CheckEquality(
                    v, &block->entry_values[i], &inneroom
                    ))
                return 1;
            if (inneroom) {
                *oom = 1;
                return 0;
            }
            i++;
        }
        block = block->next_block;
    }
    return 0;
}

static int _grow_shrunk_first_block(
        genericlist *l, int64_t required_space
        ) {
    if (unlikely(l->list_block_count == 1 &&
            l->first_block_shrunk_size < LISTBLOCK_SIZE &&
            required_space > l->first_block_shrunk_size)) {
        listblock *oldblock = l->first_block;
        int64_t new_size = l->first_block_shrunk_size * 2;
        if (unlikely(new_size < oldblock->entry_count + 1))
            new_size = oldblock->entry_count + 1;
        if (unlikely(new_size > LISTBLOCK_SIZE))
            new_size = LISTBLOCK_SIZE;
        listblock *resized_block = malloc(
            sizeof(_listblock_minisize) + (
                sizeof(valuecontent) * (new_size - LISTBLOCK_MINSIZE)
            )
        );
        if (!resized_block) {
            return 0;
        }
        assert(l->first_block_shrunk_size >= LISTBLOCK_MINSIZE);
        memcpy(
            resized_block, oldblock,
            sizeof(_listblock_minisize) + (
                sizeof(valuecontent) * (
                    l->first_block_shrunk_size - LISTBLOCK_MINSIZE
                )
            )
        );
        l->first_block = resized_block;
        l->last_block = resized_block;
        free(oldblock);
        l->first_block_shrunk_size = new_size;
        return 1;
    }
    return 0;
}

int vmlist_Add(
        genericlist *l, valuecontent *vc
        ) {
    assert(l != NULL);
    assert(vc != NULL);

    // If this is the first block, it might be shrunk (to save space for
    // mini lists):
    int first_grown = _grow_shrunk_first_block(
        l, l->list_total_entry_count + 1
    );
    if (first_grown < 0)
        return -1;  // oom

    // Append entry:
    assert(l->last_block != NULL);
    if (l->last_block->entry_count >= LISTBLOCK_SIZE) {
        assert(
            l->first_block_shrunk_size >= LISTBLOCK_SIZE
        );
        listblock *newblock = malloc(sizeof(*newblock));
        if (!newblock)
            return 0;
        newblock->entry_count = 0;
        newblock->next_block = NULL;
        l->last_block->next_block = newblock;
        l->last_block = newblock;
    }
    assert(l->last_block->next_block == NULL &&
           l->last_block->entry_count < LISTBLOCK_SIZE);
    assert(
        l->first_block_shrunk_size >= LISTBLOCK_SIZE ||
        (l->list_block_count == 1 &&
         l->first_block_shrunk_size >= l->last_block->entry_count + 1)
    );
    memcpy(
        &l->last_block->entry_values[l->last_block->entry_count],
        vc, sizeof(*vc)
    );
    ADDREF_HEAP(vc);
    l->last_block->entry_count++;
    l->list_total_entry_count++;
    assert(l->list_total_entry_count >= l->last_block->entry_count);
    l->contentrevisionid++;
    return 1;
}

int vmlist_Remove(genericlist *l, int64_t index) {
    if (index < 1 || index > l->list_total_entry_count)
        return 0;
    int64_t blockoffset = -1;
    listblock *block = NULL;
    vmlist_GetEntryBlock(
        l, index, &block, &blockoffset
    );
    assert(block != NULL && blockoffset >= 0);
    int local_index = (int64_t)(index - blockoffset);
    assert(local_index >= 1 && local_index <= LISTBLOCK_SIZE &&
           local_index <= block->entry_count);
    assert(block->entry_count > 0);
    DELREF_HEAP(&block->entry_values[local_index - 1]);
    if (local_index < LISTBLOCK_SIZE) {
        memmove(
            &block->entry_values[(local_index - 1)],
            &block->entry_values[(local_index - 1) + 1],
            sizeof(*block->entry_values) * (
                LISTBLOCK_SIZE - local_index
            )
        );
    }
    block->entry_count--;
    l->contentrevisionid++;
    return 1;
}

int vmlist_Set(genericlist *l, int64_t index, valuecontent *vc) {
    if (index < 1 || index > l->list_total_entry_count + 1)
        return 0;
    if (index == l->list_total_entry_count + 1)
        return (vmlist_Add(l, vc) ? 1 : -1);
    int64_t blockoffset = -1;
    listblock *block = NULL;
    vmlist_GetEntryBlock(
        l, index, &block, &blockoffset
    );
    assert(block != NULL && blockoffset >= 0);
    int local_index = (int64_t)(index - blockoffset);
    assert(local_index >= 1 && local_index <= LISTBLOCK_SIZE &&
           local_index <= block->entry_count);
    DELREF_HEAP(&block->entry_values[local_index - 1]);
    memcpy(
        &block->entry_values[local_index - 1],
        vc, sizeof(*vc)
    );
    ADDREF_HEAP(&block->entry_values[local_index - 1]);
    // Do NOT increase l->contentrevisionid, since this doesn't change
    // container length. So this is allowed while iterating a list.
    return 1;
}

int vmlist_Insert(
        genericlist *l, int64_t index, valuecontent *vc
        ) {
    if (index < 1 || index > l->list_total_entry_count + 1)
        return 0;
    if (index == l->list_total_entry_count + 1)
        return (vmlist_Add(l, vc) ? 1 : -1);

    // Get the block into which to insert:
    int64_t blockoffset = -1;
    listblock *block = NULL;
    vmlist_GetEntryBlock(
        l, index, &block, &blockoffset
    );
    assert(block != NULL && blockoffset >= 0);
    int local_index = (int64_t)(index - blockoffset);
    assert(local_index >= 1 && local_index <= LISTBLOCK_SIZE &&
           local_index <= block->entry_count);

    // If this is the first block, it might be shrunk (to save space for
    // mini lists):
    int first_grown = _grow_shrunk_first_block(l, block->entry_count + 1);
    if (first_grown < 0)
        return -1;  // oom
    else if (first_grown > 0)
        block = l->first_block;  // need to update pointer.
    
    // Regular block list growth if we fill up this block:
    if (block->entry_count >= LISTBLOCK_SIZE) {
        listblock *newblock = malloc(sizeof(*newblock));
        if (!newblock)
            return -1;
        int pushout_items = (LISTBLOCK_SIZE - local_index) + 1;
        assert(pushout_items > 0 && pushout_items <= LISTBLOCK_SIZE);
        memcpy(
            newblock->entry_values,
            &block->entry_values[LISTBLOCK_SIZE - pushout_items],
            sizeof(*newblock->entry_values) * pushout_items
        );
        block->entry_count -= pushout_items;
        assert(block->entry_count >= 0 &&
               block->entry_count < LISTBLOCK_SIZE);
        newblock->entry_count = pushout_items;
        assert(newblock->entry_count > 0 &&
               newblock->entry_count <= LISTBLOCK_SIZE);
        if (!block->next_block)
            l->last_block = newblock;
        block->next_block = newblock;
    }
    if (local_index < LISTBLOCK_SIZE) {
        memmove(
            &block->entry_values[(local_index - 1) + 1],
            &block->entry_values[(local_index - 1)],
            sizeof(*block->entry_values) * (LISTBLOCK_SIZE - local_index)
        );
    }
    memmove(
        &block->entry_values[(local_index - 1)],
        vc, sizeof(*vc)
    );
    ADDREF_HEAP(vc);
    block->entry_count++;
    l->list_total_entry_count++;
    l->contentrevisionid++;
    l->last_accessed_block = NULL;
    l->last_accessed_block_offset = -1;
    return 1;
}