// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_VMCONTAINERSTRUCT_H_
#define HORSE64_VMCONTAINERSTRUCT_H_

#include "compileconfig.h"

#include <stdint.h>

#include "compiler/globallimits.h"
#include "valuecontentstruct.h"

#define LISTBLOCK_SIZE (4096)
#define LISTBLOCK_MINSIZE 16

typedef struct listblock listblock;

typedef struct vectorentry {
    int64_t int_value;
    double float_value;
    uint8_t is_float;
} vectorentry;

typedef struct listblock {
    int entry_count;
    listblock *next_block;
    valuecontent entry_values[LISTBLOCK_SIZE];
} listblock;

typedef struct _listblock_minisize {
    int entry_count;
    listblock *next_block;
    valuecontent entry_values[LISTBLOCK_MINSIZE];
} _listblock_minisize;

typedef struct genericlist {
    int64_t last_accessed_block_offset;
    listblock *last_accessed_block;

    uint64_t contentrevisionid;

    int64_t list_total_entry_count;
    int64_t list_block_count;
    listblock *first_block, *last_block;
    int first_block_shrunk_size;
} genericlist;

typedef struct hashmap hashmap;

typedef struct genericset {
    hashmap *values;
    int64_t nonhashable_count;
    valuecontent *nonhashable_value;
    uint64_t contentrevisionid;
} genericset;

static const uint8_t GENERICMAP_FLAG_LINEAR = 0x1;

typedef struct genericmapbucket {
    int32_t entry_count;
    valuecontent *key, *entry;
    uint32_t *entry_hash;
} genericmapbucket;

typedef struct genericmap {
    uint8_t flags;
    union {
        struct hashed {
            int64_t entry_count;
            int32_t bucket_count;
            genericmapbucket *bucket;
        } hashed;
        struct linear {
            int16_t entry_count, entry_alloc;
            valuecontent *key, *entry;
            uint32_t *entry_hash;
        } linear;
    };
    uint64_t contentrevisionid;
} genericmap;

typedef struct genericvector {
    int entries_count;
    vectorentry *values;
} genericvector;


#endif  // HORSE64_VMCONTAINERSTRUCT_H_