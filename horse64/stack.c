
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bytecode.h"
#include "stack.h"


#define likely(x) __builtin_expect(!!(x), 1) 
#define unlikely(x) __builtin_expect(!!(x), 0) 

h64stack *stack_New() {
    h64stack *st = malloc(sizeof(*st));
    if (!st)
        return NULL;

    return st;
}

void stack_FreeEntry(h64stackblock *block, int slot) {
    return;
}

void stack_Free(h64stack *st) {
    if (!st)
        return;
    int64_t i = 0;
    while (i < st->block_count) {
        int64_t k = 0;
        while (k < st->block[i].entry_count) {
            stack_FreeEntry(&st->block[i], k);
            k++;
        }
        free(st->block[i].entry);
        i++;
    }
    free(st->block);
    free(st);
}

int stack_Shrink(h64stack *st, int64_t total_entries) {
    int i = st->block_count - 1;
    while (likely(i >= 0 && st->entry_total_count > total_entries)) {
        if (unlikely(i == st->block_count - 1 &&
                st->alloc_total_count - st->block[i].alloc_count >=
                total_entries + ALLOC_OVERSHOOT)) {
            // Dump entire last stack block:
            const int blocklen = st->block[i].entry_count;
            int k = 0;
            while (k < blocklen) {
                stack_FreeEntry(&st->block[i], k);
                k++;
            }
            free(st->block[i].entry);
            st->entry_total_count -= st->block[i].entry_count;
            st->alloc_total_count -= st->block[i].alloc_count;
            st->block_count--;
            if (st->block_count == 0) {
                free(st->block);
                st->block = NULL;
            } else {
                h64stackblock *new_blocks = realloc(
                    st->block, sizeof(*st->block) * (
                    st->block_count
                    )
                );
                if (new_blocks)
                    st->block = new_blocks;
            }
            i--;
            continue;
        }
        // Reduce entries in stack block:
        int reduce_amount = (st->entry_total_count - total_entries);
        if (reduce_amount <= 0)
            break;
        int k = st->block[i].entry_count - 1;
        while (k >= 0 && reduce_amount > 0) {
            stack_FreeEntry(&st->block[i], k);
            k--;
            reduce_amount--;
        }
        st->block[i].entry_count -= reduce_amount;
        st->entry_total_count -= reduce_amount;
        assert(reduce_amount == 0 && st->block[i].entry_count >= 0);
        i--;
    }
    return 1;
}

int stack_BlockGrowTo(
        h64stack *st, h64stackblock *block, int64_t size
        ) {
    if (unlikely(size + ALLOC_OVERSHOOT <=
            block->alloc_count ||
            block->alloc_count >= BLOCK_MAX_ENTRIES))
        return 1;
    int64_t new_alloc = size + ALLOC_OVERSHOOT;
    if (new_alloc > BLOCK_MAX_ENTRIES)
        new_alloc = BLOCK_MAX_ENTRIES;
    assert(new_alloc > block->alloc_count);
    assert(new_alloc > 0);
    valuecontent *new_entries = realloc(
        block->entry,
        sizeof(*block->entry) * new_alloc
    );
    if (!new_entries)
        return 0;
    int old_alloc = block->alloc_count;
    block->entry = new_entries;
    block->alloc_count = new_alloc;
    st->alloc_total_count += (int64_t)(new_alloc - old_alloc);
    return 1;
}

void stack_GrowEntriesTo(
        h64stack *st, h64stackblock *block, int64_t size
        ) {
    if (unlikely(size > BLOCK_MAX_ENTRIES))
        size = BLOCK_MAX_ENTRIES;
    if (unlikely(size <= block->entry_count))
        return;
    int oldcount = block->entry_count;
    block->entry_count = size;
    if (size > oldcount)
        memset(
            &block->entry[oldcount], 0,
            sizeof(*block->entry) * (size - oldcount)
        );
    st->entry_total_count += (size - oldcount);
}

int stack_ToSize(
        h64stack *st,
        int64_t total_entries,
        int can_use_emergency_margin
        ) {
    int alloc_needed_margin = (
        can_use_emergency_margin ? 0 : ALLOC_EMERGENCY_MARGIN
    );
    if (likely(st->alloc_total_count >= total_entries +
               alloc_needed_margin)) {
        if (unlikely(st->entry_total_count == total_entries))
            return 1;
        if (unlikely(st->entry_total_count > total_entries))
            return stack_Shrink(st, total_entries);

        #ifndef NDEBUG
        {   // Ensure all blocks but the last one are fully filled:
            int i = 0;
            while (i < st->block_count - 1) {
                assert(st->block[i].alloc_count == BLOCK_MAX_ENTRIES);
                assert(st->block[i].entry_count == st->block[i].alloc_count);
                i++;
            }
        }
        #endif
        int last_block = st->block_count - 1;
        assert(last_block >= 0);
        assert(
            st->block[last_block].offset ==
            (int64_t)BLOCK_MAX_ENTRIES * (int64_t)(last_block - 1)
        );
        int64_t need_in_last_block = (
            total_entries - st->block[last_block].offset
        );
        assert(st->block[last_block].alloc_count >= need_in_last_block);
        if (st->block[last_block].entry_count < need_in_last_block) {
            int oldcount = st->block[last_block].entry_count;
            st->entry_total_count += (int64_t)(
                need_in_last_block - st->block[last_block].entry_count
            );
            st->block[last_block].entry_count = need_in_last_block;
            assert(need_in_last_block > oldcount);
            memset(&st->block[last_block].entry[oldcount], 0,
                sizeof(*st->block[last_block].entry) *
                (need_in_last_block - oldcount));
        }
        return 1;
    }

    int block_count = (int64_t)(
        total_entries / ((int64_t)BLOCK_MAX_ENTRIES)
    );
    assert(st->entry_total_count >=
           (int16_t)(st->block_count - 1) * (int64_t)BLOCK_MAX_ENTRIES);
    #ifndef NDEBUG
    {  // Ensure all blocks but the last one are fully filled:
        int i = 0;
        while (i < st->block_count - 1) {
            assert(
                st->block[i].alloc_count == BLOCK_MAX_ENTRIES
            );
            assert(
                st->block[i].entry_count == st->block[i].alloc_count ||
                st->block[i + 1].entry_count == 0
            );
            i++;
        }
    }
    #endif
    int lookat_block = st->block_count - 1;
    while (lookat_block > 0 &&
            st->block[lookat_block - 1].entry_count < BLOCK_MAX_ENTRIES)
        lookat_block--;
    while (lookat_block >= 0 && lookat_block < st->block_count - 1) {
        // Add as much as possible into last non-full block:
        assert(
            st->block[lookat_block].offset ==
            (int64_t)BLOCK_MAX_ENTRIES * (int64_t)(lookat_block - 1)
        );
        int64_t space_in_last_block = (
            (int64_t)BLOCK_MAX_ENTRIES - (int64_t)st->block[lookat_block].
                entry_count);
        assert(space_in_last_block >= 0);
        int64_t add_to_block = (total_entries + alloc_needed_margin -
            st->block[lookat_block].offset) -
            st->block[lookat_block].entry_count;
        if (unlikely(add_to_block > space_in_last_block))
            add_to_block = space_in_last_block;
        if (likely(add_to_block > 0)) {
            if (!stack_BlockGrowTo(
                    st, &st->block[lookat_block],
                    add_to_block + st->block[lookat_block].entry_count
                    ))
                return 0;
            if (likely(add_to_block - alloc_needed_margin >
                    st->block[lookat_block].entry_count))
                stack_GrowEntriesTo(
                    st, &st->block[lookat_block], (add_to_block +
                    st->block[lookat_block].entry_count) - alloc_needed_margin
                );
            if (st->entry_total_count >= total_entries &&
                    st->alloc_total_count >=
                    total_entries + alloc_needed_margin) {
                assert(st->entry_total_count == total_entries);
                return 1;
            }
        }
        lookat_block++;
    }
    assert(
        (st->entry_total_count <= total_entries &&
         st->alloc_total_count < total_entries + alloc_needed_margin));
    // If we arrived here, it didn't all fit into the existing blocks.
    int64_t new_blocks_needed = (int64_t)(
        ((total_entries + ALLOC_OVERSHOOT * 2 + alloc_needed_margin) -
        st->alloc_total_count) +
        (BLOCK_MAX_ENTRIES - 1)
    ) / (int64_t)BLOCK_MAX_ENTRIES;
    assert(new_blocks_needed > 0);
    h64stackblock *new_blocks = realloc(
        st->block,
        sizeof(*new_blocks) * (st->block_count + new_blocks_needed)
    );
    if (!new_blocks)
        return 0;
    int oldcount = st->block_count;
    st->block = new_blocks;
    memset(&st->block[st->block_count], 0,
        sizeof(*st->block) * (new_blocks_needed));
    st->block_count += new_blocks_needed;
    int k = oldcount;
    while (k < st->block_count) {
        // Allocate in current block:
        int64_t still_needed_alloc = (
            (total_entries + alloc_needed_margin + ALLOC_OVERSHOOT) -
            st->alloc_total_count
        );
        if (still_needed_alloc > 0) {
            if (!stack_BlockGrowTo(
                    st, &st->block[lookat_block],
                    still_needed_alloc
                    ))
                return 0;
        }
        if (likely(total_entries < st->entry_total_count)) {
            int block_new_size = total_entries - st->entry_total_count;
            if (block_new_size > BLOCK_MAX_ENTRIES)
                block_new_size = BLOCK_MAX_ENTRIES;
            stack_GrowEntriesTo(
                st, &st->block[k], block_new_size
            );
        } else {
            assert(st->entry_total_count == total_entries);
            return 1;
        }
        k++;
    }
    assert(st->entry_total_count == total_entries);
    return 1;
}
