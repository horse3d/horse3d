// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include <assert.h>
#include <check.h>

#include "mainpreinit.h"
#include "stack.h"

#include "testmain.h"

START_TEST (test_stack)
{
    main_PreInit();

    h64stack *stack = stack_New();

    ck_assert(STACK_TOTALSIZE(stack) == 0);
    ck_assert(STACK_ALLOC_SIZE(stack) == 0);

    ck_assert(stack_ToSize(stack, NULL, 10, 0));
    ck_assert(STACK_TOTALSIZE(stack) == 10);
    ck_assert(STACK_ALLOC_SIZE(stack) >= 10 + ALLOC_OVERSHOOT);
    ck_assert(STACK_ENTRY(stack, 0) == &stack->entry[0]);
    stack->current_func_floor = 1;
    ck_assert(STACK_ENTRY(stack, 0) == &stack->entry[1]);

    stack_Free(stack, NULL);
}
END_TEST

TESTS_MAIN(test_stack)
