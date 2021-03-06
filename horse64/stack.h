// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_STACK_H_
#define HORSE64_STACK_H_

#include "compileconfig.h"

#include <stdint.h>

#include "bytecode.h"

typedef struct valuecontent valuecontent;
typedef struct h64vmthread h64vmthread;

#define ALLOC_OVERSHOOT 32
#define ALLOC_MAXOVERSHOOT 4096
#define ALLOC_EMERGENCY_MARGIN 6


typedef struct h64stack {
    int64_t entry_count, alloc_count;
    int64_t current_func_floor;
    valuecontent *entry;
} h64stack;

h64stack *stack_New();

int stack_IncreaseAlloc(
    h64stack *st, ATTR_UNUSED h64vmthread *vmthread,
    int64_t total_entries, int alloc_needed_margin
);


void stack_Shrink(
    h64stack *st, h64vmthread *vmthread, int64_t total_entries
);

ATTR_UNUSED HOTSPOT static inline int stack_ToSize(
        h64stack *st, h64vmthread *vmthread,
        int64_t total_entries,
        int can_use_emergency_margin
        ) {
    assert(total_entries >= 0);
    int alloc_needed_margin = (
        can_use_emergency_margin ? 0 :
        ALLOC_EMERGENCY_MARGIN
    );
    // Grow if needed:
    if (unlikely(st->alloc_count < total_entries + alloc_needed_margin)) {
        if (!stack_IncreaseAlloc(
                st, vmthread, total_entries, alloc_needed_margin
                ))
            return 0;
    }
    if (total_entries < st->entry_count) {
        // Shrink and done:
        stack_Shrink(st, vmthread, total_entries);
        return 1;
    }
    assert(st->alloc_count >= total_entries);
    if (likely(st->entry_count < total_entries)) {
        memset(&st->entry[st->entry_count], 0,
            sizeof(st->entry[st->entry_count]) * (
                total_entries - st->entry_count
            ));
    }
    st->entry_count = total_entries;
    return 1;
}

void stack_Free(h64stack *st, h64vmthread *vmthread);

void stack_PrintDebug(h64stack *st);


ATTR_UNUSED static inline valuecontent *stack_GetEntrySlow(
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
#define STACK_ENTRY(stack, no) (\
    &stack->entry[(int64_t)(no) + stack->current_func_floor]\
    )

#endif  // HORSE64_STACK_H_
