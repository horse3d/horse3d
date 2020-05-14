#include <assert.h>
#include <check.h>

#include "stack.h"

#include "testmain.h"

START_TEST (test_stack)
{
    h64stack *stack = stack_New();

    ck_assert(STACK_SIZE(stack) == 0);
    ck_assert(STACK_ALLOC_SIZE(stack) == 0);

    ck_assert(stack_ToSize(stack, 10, 0));
    ck_assert(STACK_SIZE(stack) == 10);
    ck_assert(STACK_ALLOC_SIZE(stack) >= 10 + ALLOC_OVERSHOOT);

    stack_Free(stack);
}
END_TEST

TESTS_MAIN(test_stack)
