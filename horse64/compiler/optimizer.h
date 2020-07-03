// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_COMPILER_OPTIMIZER_H_
#define HORSE64_COMPILER_OPTIMIZER_H_

typedef struct h64compileproject h64compileproject;
typedef struct h64ast h64ast;

int optimizer_PreevaluateConstants(
    h64compileproject *pr, h64ast *ast
);

#endif  // HORSE64_COMPILER_OPTIMIZER_H_
