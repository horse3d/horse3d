// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_COMPILER_CODEFLOW_H_
#define HORSE64_COMPILER_CODEFLOW_H_

#include "compiler/ast.h"
#include "compiler/astparser.h"

typedef struct h64compileproject h64compileproject;


int codeflow_FollowFlowBackwards(
    h64ast *ast, h64expression *expr,
    int *inout_prevstatement_alloc,
    int *out_prevstatement_count,
    h64expression ***inout_prevstatement
);

h64expression *statement_before(
    h64ast *ast, h64expression *expr
);

h64expression *statement_after(
    h64ast *ast, h64expression *expr
);

void codeflow_SetBeforeAfter(h64compileproject *pr, h64ast *ast);

#endif  // HORSE64_COMPILER_CODEFLOW_H_
