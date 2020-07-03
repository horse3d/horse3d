#ifndef HORSE64_STACK_H_
#define HORSE64_STACK_H_

#include <stdint.h>

#include "bytecode.h"

typedef struct valuecontent valuecontent;

#define BLOCK_MAX_ENTRIES (1024 * 2)
#define ALLOC_OVERSHOOT 32
#define ALLOC_EMERGENCY_MARGIN 6

typedef struct h64stackblock {
    int entry_count, alloc_count;
    valuecontent *entry;
    int64_t offset;
} h64stackblock;

typedef struct h64stack {
    int64_t entry_total_count, alloc_total_count;
    int64_t current_func_floor;
    int64_t last_block_relative_floor;
    int block_count;
    h64stackblock *block;
    h64stackblock *last_block;
} h64stack;

static inline void stack_RelFloorUpdate(h64stack *s) {
    s->last_block_relative_floor = (
        s->current_func_floor - s->last_block->offset
    );
    if (s->last_block_relative_floor < 0)
        s->last_block_relative_floor = INT_MAX;
}

h64stack *stack_New();

int stack_ToSize(
    h64stack *st, int64_t total_entries,
    int can_use_emergency_margin
);

void stack_Free(h64stack *st);

static inline valuecontent *stack_GetEntrySlowUnsafe(
        h64stack *st, int64_t index
        ) {
    int k = 0;
    while (k < st->block_count &&
            st->block[k].offset > index)
        k++;
    return &st->block[k].entry[index - st->block[k].offset];
}

static inline valuecontent *stack_GetEntrySlow(
        h64stack *st, int64_t index
        ) {
    if (index < 0)
        index = st->entry_total_count + index;
    return stack_GetEntrySlowUnsafe(st, index);
}

#define STACK_TOTALSIZE(stack) ((int64_t)stack->entry_total_count)
#define STACK_TOP(stack) (\
    (int64_t)stack->entry_total_count - stack->current_func_floor\
    )
#define STACK_ALLOC_SIZE(stack) ((int64_t)stack->alloc_total_count)
#define STACK_ENTRY(stack, no) (\
    ((no + stack->last_block_relative_floor <\
          stack->last_block->entry_count) ?\
      &stack->last_block->entry[no + stack->last_block_relative_floor] :\
      stack_GetEntrySlowUnsafe(stack, no + stack->current_func_floor))\
)

#endif  // HORSE64_STACK_H_
