// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include <stdlib.h>
#include <string.h>

#include "poolalloc.h"
#include "threading.h"
#include "vmexec.h"
#include "vmstrings.h"

#define POOLEDSTRSIZE 64

int vmstrings_AllocBuffer(
        h64vmthread *vthread,
        h64stringval *v, uint64_t len) {
    if (!vthread || !v)
        return 0;
    if (!vthread->str_pile) {
        vthread->str_pile = poolalloc_New(POOLEDSTRSIZE);
        if (!vthread->str_pile)
            return 0;
    }
    if (len * sizeof(unicodechar) <= POOLEDSTRSIZE) {
        v->s = poolalloc_malloc(
            vthread->str_pile, 0
        );
    } else {
        v->s = malloc(sizeof(unicodechar) * len);
    }
    v->len = len;
    return (v->s != NULL);
}

void vmstrings_Free(h64vmthread *vthread, h64stringval *v) {
    if (!vthread || !v)
        return;
    if (v->len <= POOLEDSTRSIZE) {
        poolalloc_free(vthread->str_pile, v->s);
    } else {
        free(v->s);
    }
    v->len = 0;
}
