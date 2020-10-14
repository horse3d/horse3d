// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_VMLISTSTRUCT_H_
#define HORSE64_VMLISTSTRUCT_H_

#include <stdint.h>

#include "compiler/globallimits.h"
#include "valuecontentstruct.h"

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


#endif  // HORSE64_VMLISTSTRUCT_H_