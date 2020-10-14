// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_COMPILER_THREADABLECHECKER_H_
#define HORSE64_COMPILER_THREADABLECHECKER_H_

#include "bytecode.h"

typedef struct h64ast h64ast;
typedef struct h64compileproject h64compileproject;
typedef struct h64program h64program;
typedef struct hashmap hashmap;

int threadablechecker_RegisterASTForCheck(
    h64compileproject *project, h64ast *ast
);

int threadablechecker_IterateFinalGraph(
    h64compileproject *project
);


void threadablechecker_FreeGraphInfoFromProject(
    h64compileproject *project
);


typedef struct h64threadablecheck_calledfuncinfo {
    funcid_t func_id;
    int64_t line, column;
} h64threadablecheck_calledfuncinfo;

typedef struct h64threadablecheck_nodeinfo {
    funcid_t associated_func_id;
    int called_func_count;
    h64threadablecheck_calledfuncinfo *called_func_info;
} h64threadablecheck_nodeinfo;

typedef struct h64threadablecheck_graph {
    hashmap *func_id_to_nodeinfo;
} h64threadablecheck_graph;

#endif  // HORSE64_COMPILER_THREADABLECHECKER_H_