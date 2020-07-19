// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_STACK_H_
#define HORSE64_STACK_H_

#include <stdint.h>

#include "bytecode.h"
#include "compileconfig.h"

typedef struct valuecontent valuecontent;

#define ALLOC_OVERSHOOT 32
#define ALLOC_MAXOVERSHOOT 4096
#define ALLOC_EMERGENCY_MARGIN 6


typedef struct h64stack {
    int64_t entry_count, alloc_count;
    int64_t current_func_floor;
    valuecontent *entry;
} h64stack;

h64stack *stack_New();

int stack_ToSize(
    h64stack *st, int64_t total_entries,
    int can_use_emergency_margin
);

void stack_Free(h64stack *st);


static inline valuecontent *stack_GetEntrySlow(
        h64stack *st, int64_t index
        ) {
    if (unlikely(index < 0))
        index = st->entry_count + index;
    return &st->entry[index];
}

#define STACK_TOTALSIZE(stack) ((int64_t)stack->entry_count)
#define STACK_TOP(stack) (\
    (int64_t)stack->entry_count - stack->current_func_floor\
    )
#define STACK_ALLOC_SIZE(stack) ((int64_t)stack->alloc_count)
#define STACK_ENTRY(stack, no) (&stack->entry[no])

#endif  // HORSE64_STACK_H_
