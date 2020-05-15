#ifndef HORSE64_STACK_H_
#define HORSE64_STACK_H_

#include <stdint.h>

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
    int block_count;
    h64stackblock *block;
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
    if (index < 0)
        index = st->entry_total_count + index;
    int k = 0;
    while (k < st->block_count &&
            st->block[k].offset > index)
        k++;
    return &st->block[k].entry[index - st->block[k].offset];
}

#define STACK_SIZE(stack) ((int64_t)stack->entry_total_count)
#define STACK_ALLOC_SIZE(stack) ((int64_t)stack->alloc_total_count)
#define STACK_ENTRY(stack, no) (\
    ((int64_t)no >= 0) ?\
    ((stack->block_count > 0 &&\
      no < st->block[0].entry_count) ?\
      &st->block[0].entry[no] :\
      stack_GetEntrySlow(stack, no)) :\
     (stack->block_count > 0 &&\
      -no <= st->block[st->block_count - 1].entry_count) ?\
      &st->block[st->block_count - 1].entry[\
          st->block[st->block_count - 1].entry_count + no\
      ] : stack_GetEntrySlow(stack, no)\
)

#endif  // HORSE64_STACK_H_
