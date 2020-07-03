// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_COMPILER_SCOPERESOLVER_H_
#define HORSE64_COMPILER_SCOPERESOLVER_H_

typedef struct h64ast h64ast;
typedef struct h64compileproject h64compileproject;

int scoperesolver_ResolveAST(
    h64compileproject *pr, h64ast *unresolved_ast,
    int extract_program_main
);

#endif  // HORSE64_COMPILER_SCOPERESOLVER_H_
