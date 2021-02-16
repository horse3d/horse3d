// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_ITEMSORT_H_
#define HORSE64_ITEMSORT_H_

#include "compileconfig.h"

#include <stdint.h>

int itemsort_Do(
    void *sortdata, int64_t sortdatabytes, int64_t itemsize,
    int (*compareFunc)(void *item1, void *item2)
);


#endif  // HORSE64_ITEMSORT_H_
