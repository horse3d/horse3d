// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_COMPILER_CODEGEN_H_
#define HORSE64_COMPILER_CODEGEN_H_

typedef struct h64compileproject h64compileproject;
typedef struct h64ast h64ast;

int codegen_GenerateBytecodeForFile(
    h64compileproject *project, h64ast *resolved_ast
);

#endif  // HORSE64_COMPILER_CODEGEN_H_
