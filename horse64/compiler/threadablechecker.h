// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_COMPILER_THREADABLECHECKER_H_
#define HORSE64_COMPILER_THREADABLECHECKER_H_

#include "bytecode.h"

typedef struct h64ast h64ast;
typedef struct h64program h64program;
typedef struct hashmap hashmap;

int threadablechecker_RegisterASTForCheck(
    h64compileproject *project, h64ast *ast
);

typedef struct h64threadablecheck_nodeinfo {
    funcid_t associated_func_id;
    int called_func_count;
    funcid_t called_func_id;
} h64threadablecheck_nodeinfo;

typedef struct h64threadablecheck_graph {
    hashmap *func_id_to_nodeinfo;
} h64threadablecheck_graph;

#endif  // HORSE64_COMPILER_THREADABLECHECKER_H_