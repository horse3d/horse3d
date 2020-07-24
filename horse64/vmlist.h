// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_VMLIST_H_
#define HORSE64_VMLIST_H_

#include "bytecode.h"


#define LISTBLOCK_SIZE 64

typedef struct listblock listblock;

typedef struct vectorentry {
    int64_t int_value;
    double float_value;
    uint8_t is_float;
} vectorentry;

typedef struct listblock {
    int entry_count;
    valuecontent entry_values[LISTBLOCK_SIZE];
    listblock *next_block;
} listblock;

typedef struct genericlist {
    int64_t last_accessed_block_offset;
    listblock *last_accessed_block;

    uint64_t contentrevisionid;

    int64_t list_total_entry_count;
    int64_t list_block_count;
    listblock *first_block, *last_block;
} genericlist;

typedef struct hashmap hashmap;

typedef struct genericset {
    hashmap *values;
} genericset;

typedef struct genericmap {
    hashmap *values;
} genericmap;

typedef struct genericvector {
    int entries_count;
    vectorentry *values;
} genericvector;


genericlist *vmlist_New();

static inline int64_t vmlist_Count(genericlist *l) {
    return l->list_total_entry_count;
}

static inline void vmlist_GetEntryBlock(
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
            l->last_accessed_block_offset > entry_no) {
        *out_block = l->last_accessed_block;
        *out_block_offset = *out_block_offset;
        return;
    }

    int64_t offset = 0;
    listblock *block = l->first_block;
    while (block) {
        if (offset <= entry_no &&
                block->entry_count + offset > entry_no) {
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
    *out_block_offset = 0;
}

int vmlist_Append(
    genericlist *l, valuecontent *vc
);

#endif  // HORSE64_VMLIST_H_
