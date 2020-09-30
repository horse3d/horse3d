// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_VMSTRINGS_H_
#define HORSE64_VMSTRINGS_H_

#include <stdint.h>

typedef uint32_t h64wchar;
typedef struct h64vmthread h64vmthread;

typedef struct h64stringval {
    h64wchar *s;
    uint64_t len;
    int refcount;
} h64stringval;

typedef struct h64bytesval {
    char *s;
    uint64_t len;
    int refcount;
} h64bytesval;


int vmstrings_AllocBuffer(
    h64vmthread *vthread, h64stringval *v, uint64_t len
);

void vmstrings_Free(h64vmthread *vthread, h64stringval *v);

int vmbytes_AllocBuffer(
    h64vmthread *vthread, h64bytesval *v, uint64_t len
);

void vmbytes_Free(h64vmthread *vthread, h64bytesval *v);

#endif  // HORSE64_VMSTRINGS_H_
