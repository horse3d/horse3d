#include <assert.h>
#include <check.h>

#include "stack.h"

#include "testmain.h"

START_TEST (test_stack)
{
    h64stack *stack = stack_New();

    ck_assert(STACK_TOTALSIZE(stack) == 0);
    ck_assert(STACK_ALLOC_SIZE(stack) == 0);

    ck_assert(stack_ToSize(stack, 10, 0));
    ck_assert(STACK_TOTALSIZE(stack) == 10);
    ck_assert(STACK_ALLOC_SIZE(stack) >= 10 + ALLOC_OVERSHOOT);
    ck_assert(stack->block_count > 0);
    ck_assert(STACK_ENTRY(stack, 0) == &stack->block[0].entry[0]);

    stack_Free(stack);
}
END_TEST

TESTS_MAIN(test_stack)
