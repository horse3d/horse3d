// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_POOLALLOC_H_
#define HORSE64_POOLALLOC_H_

typedef struct poolalloc poolalloc;

poolalloc *poolalloc_New(int itemsize);

void poolalloc_Destroy(poolalloc *poolac);

void *poolalloc_malloc(poolalloc *poolac,
    int can_use_emergency_margin);

void poolalloc_free(poolalloc *poolac, void *ptr);

#endif  // HORSE64_POOLALLOC_H_
