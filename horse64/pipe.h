// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_PIPE_H_
#define HORSE64_PIPE_H_

#include <stdint.h>

typedef struct h64gcvalue h64gcvalue;
typedef struct h64pipe h64pipe;
typedef struct h64vmthread h64vmthread;


int _pipe_DoPipeObject(
    h64vmthread *source_thread,
    h64vmthread *target_thread,
    int64_t slot_from, int64_t slot_to,
    h64gcvalue **object_instances_transferlist,
    int *object_instances_transferlist_count,
    int *object_instances_transferlist_alloc,
    int *object_instances_transferlist_onheap
);

#endif  // HORSE64_PIPE_H_