#ifndef HORSE64_STACK_H_
#define HORSE64_STACK_H_

#include <stdint.h>

typedef struct valuecontent valuecontent;

#define BLOCK_MAX_ENTRIES (1024 * 5)
#define ALLOC_OVERSHOOT 32
#define ALLOC_EMERGENCY_MARGIN 6

typedef struct h64stackblock {
    int entry_count, alloc_count;
    valuecontent *entry;
    int64_t offset;
} h64stackblock;

typedef struct h64stack {
    int64_t entry_total_count, alloc_total_count;
    int block_count;
    h64stackblock *block;
} h64stack;

h64stack *stack_New();

int stack_ToSize(
    h64stack *st, int64_t total_entries,
    int can_use_emergency_margin
);

void stack_Free(h64stack *st);

#define STACK_SIZE(stack) ((int64_t)stack->entry_total_count)
#define STACK_ALLOC_SIZE(stack) ((int64_t)stack->alloc_total_count)

#endif  // HORSE64_STACK_H_
