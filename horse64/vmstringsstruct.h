// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_VMSTRINGSSTRUCT_H_
#define HORSE64_VMSTRINGSSTRUCT_H_

#include <stdint.h>

#include "widechar.h"

typedef struct h64stringval {
    h64wchar *s;
    uint64_t len, letterlen;
    int refcount;
} h64stringval;

typedef struct h64bytesval {
    char *s;
    uint64_t len;
    int refcount;
} h64bytesval;

#endif  // HORSE64_VMSTRINGSSTRUCT_H_