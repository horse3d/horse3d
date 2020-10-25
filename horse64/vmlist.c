// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "bytecode.h"
#include "gcvalue.h"
#include "vmlist.h"

genericlist *vmlist_New() {
    genericlist *l = malloc(sizeof(*l));
    if (!l)
        return NULL;
    memset(l, 0, sizeof(*l));

    l->first_block = malloc(sizeof(*l->first_block));
    if (!l->first_block) {
        free(l);
        return NULL;
    }
    l->first_block->next_block = NULL;
    l->first_block->entry_count = 0;
    l->last_block = l->first_block;
    return l;
}

int vmlist_Add(
        genericlist *l, valuecontent *vc
        ) {
    assert(l != NULL);
    assert(vc != NULL);
    assert(l->last_block != NULL);
    if (l->last_block->entry_count >= LISTBLOCK_SIZE) {
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
    memcpy(
        &l->last_block->entry_values[l->last_block->entry_count],
        vc, sizeof(*vc)
    );
    ADDREF_HEAP(vc);
    l->last_block->entry_count++;
    l->list_total_entry_count++;
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
    return 1;
}

int vmlist_Insert(
        genericlist *l, int64_t index, valuecontent *vc
        ) {
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
    return 1;
}