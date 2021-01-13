// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_CORELIB_MODULELESS_CONTAINERS_H_
#define HORSE64_CORELIB_MODULELESS_CONTAINERS_H_

#include "widechar.h"
#include "valuecontentstruct.h"

typedef struct h64program h64program;
typedef struct h64vmthread h64vmthread;

int corelib_RegisterContainerFuncs(h64program *p);

funcid_t corelib_GetContainerFuncIdx(
    h64program *p, int64_t nameidx, int container_type
);

typedef struct h64moduleless_containers_indexes {
    int func_count;
    char **func_name;
    funcid_t *func_idx;
    int64_t *func_name_idx;
} h64moduleless_containers_indexes;

#endif  // HORSE64_CORELIB_MODULELESS_CONTAINERS_H_
