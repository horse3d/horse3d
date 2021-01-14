// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_VMLIST_H_
#define HORSE64_VMLIST_H_

#include "compileconfig.h"

#include <assert.h>
#include <stdio.h>

#include "bytecode.h"
#include "vmcontainerstruct.h"

genericlist *vmlist_New();

ATTR_UNUSED static inline int64_t vmlist_Count(genericlist *l) {
    return l->list_total_entry_count;
}

ATTR_UNUSED static inline void vmlist_GetEntryBlock(
        genericlist *l, int64_t entry_no,
        listblock **out_block, int64_t *out_block_offset
        ) {
    if (!l) {
        *out_block = NULL;
        *out_block_offset = 0;
        return;
    }

    if (l->last_accessed_block_offset < entry_no &&
            l->last_accessed_block != NULL &&
            l->last_accessed_block->entry_count +
            l->last_accessed_block_offset >= entry_no) {
        *out_block = l->last_accessed_block;
        *out_block_offset = l->last_accessed_block_offset;
        return;
    }

    int64_t offset = 0;
    listblock *block = l->first_block;
    while (block) {
        if (offset < entry_no &&
                block->entry_count + offset >= entry_no) {
            l->last_accessed_block = block;
            l->last_accessed_block_offset = offset;
            *out_block = block;
            *out_block_offset = offset;
            return;
        }
        offset += block->entry_count;
        block = block->next_block;
    }
    *out_block = NULL;
    *out_block_offset = -1;
}

int vmlist_Add(
    genericlist *l, valuecontent *vc
);

int vmlist_Insert(
    genericlist *l, int64_t index, valuecontent *vc
);  // return value: 1 = ok, 0 = invalid index, -1 = oom

typedef struct h64gcvalue2 {
    uint8_t type;
} h64gcvalue2;

ATTR_UNUSED static valuecontent *vmlist_Get(genericlist *l, int64_t i) {
    if (i < 1 || i > l->list_total_entry_count)
        return NULL;
    int64_t blockoffset = -1;
    listblock *block = NULL;
    vmlist_GetEntryBlock(
        l, i, &block, &blockoffset
    );
    assert(block != NULL && blockoffset >= 0);
    i -= blockoffset;
    assert(i >= 1 && i <= block->entry_count);
    valuecontent *result = &block->entry_values[i - 1];
    return result;
}

int vmlist_Set(
    genericlist *l, int64_t index, valuecontent *vc
);  // return value: 1 = ok, 0 = invalid index, -1 = oom

int vmlist_Remove(genericlist *l, int64_t index);

#endif  // HORSE64_VMLIST_H_
