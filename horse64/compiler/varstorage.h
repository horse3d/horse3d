// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_COMPILER_VARSTORAGE_H_
#define HORSE64_COMPILER_VARSTORAGE_H_

typedef struct h64compileproject h64compileproject;
typedef struct h64ast h64ast;
typedef struct jsonvalue jsonvalue;

int varstorage_AssignLocalStorage(
    h64compileproject *pr, h64ast *ast
);

typedef struct h64localstorageassign {
    int valuetemporaryid;  // temporary to store actual value
    int valueboxtemporaryid;  // temporary to store value box, if any
    h64scopedef *vardef;  // the definition for this variable

    int ever_used_nonclosure, ever_used_closure;
    int use_start_token_index, use_end_token_index;
} h64localstorageassign;

struct h64codegenstorageinfo {
    int oneline_temps_used_now;
    int max_oneline_slots;

    int perm_temps_count;
    int *perm_temps_used;
};

typedef struct h64funcstorageextrainfo {
    int lowest_guaranteed_free_temp;
    int temp_calculation_slots;

    struct h64codegenstorageinfo codegen;

    int32_t jump_targets_used;

    int closureboundvars_count;
    h64scopedef **closureboundvars;

    int lstoreassign_count;
    h64localstorageassign *lstoreassign;
} h64funcstorageextrainfo;

void varstorage_FreeExtraInfo(
    h64funcstorageextrainfo *einfo
);

jsonvalue *varstorage_ExtraInfoToJSON(
    h64funcstorageextrainfo *einfo
);

#endif  // HORSE64_COMPILER_VARSTORAGE_H_
