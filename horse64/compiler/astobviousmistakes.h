// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

/// This module contains functionality to find patterns in the code
/// that will compile and run but suspiciously look like programmer
/// errors, and ask programmer to fix them or mark them as intentional.

#ifndef HORSE64_COMPILER_ASTOBVIOUSMISTAKES_H_
#define HORSE64_COMPILER_ASTOBVIOUSMISTAKES_H_

typedef struct h64compileproject h64compileproject;
typedef struct h64ast h64ast;

int astobviousmistakes_CheckAST(
    h64compileproject *pr, h64ast *ast
);


#endif  // HORSE64_COMPILER_ASTOBVIOUSMISTAKES_H_
