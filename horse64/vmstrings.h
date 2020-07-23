// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_VMSTRINGS_H_
#define HORSE64_VMSTRINGS_H_

#include <stdint.h>

typedef uint32_t unicodechar;
typedef struct h64vmthread h64vmthread;

typedef struct h64stringval {
    unicodechar *s;
    uint64_t len;
    int refcount;
} h64stringval;

int vmstrings_AllocBuffer(
    h64vmthread *vthread, h64stringval *v, uint64_t len
);

void vmstrings_Free(h64vmthread *vthread, h64stringval *v);

#endif  // HORSE64_VMSTRINGS_H_
