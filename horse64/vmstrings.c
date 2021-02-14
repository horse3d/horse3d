// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <stdlib.h>
#include <string.h>

#include "gcvalue.h"
#include "poolalloc.h"
#include "threading.h"
#include "vmexec.h"
#include "valuecontentstruct.h"
#include "vmstrings.h"

#define POOLEDSTRSIZE 64


int vmstrings_Equality(
        valuecontent *v1, valuecontent *v2
        ) {
    h64wchar *s1v = NULL;
    h64wchar *s2v = NULL;
    int64_t s1l = 0;
    int64_t s2l = 0;
    if (v1->type == H64VALTYPE_SHORTSTR) {
        s1v = v1->shortstr_value;
        s1l = v1->shortstr_len;
    } else if (v1->type == H64VALTYPE_CONSTPREALLOCSTR) {
        s1v = v1->constpreallocstr_value;
        s1l = v1->constpreallocstr_len;
    } else if (v1->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue*)v1->ptr_value)->type == H64GCVALUETYPE_STRING) {
        s1v = ((h64gcvalue*)v1->ptr_value)->str_val.s;
        s1l = ((h64gcvalue*)v1->ptr_value)->str_val.len;
    }
    if (v2->type == H64VALTYPE_SHORTSTR) {
        s2v = v2->shortstr_value;
        s2l = v2->shortstr_len;
    } else if (v2->type == H64VALTYPE_CONSTPREALLOCSTR) {
        s2v = v2->constpreallocstr_value;
        s2l = v2->constpreallocstr_len;
    } else if (v2->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue*)v2->ptr_value)->type == H64GCVALUETYPE_STRING) {
        s2v = ((h64gcvalue*)v2->ptr_value)->str_val.s;
        s2l = ((h64gcvalue*)v2->ptr_value)->str_val.len;
    }
    if (likely(s1l != s2l))
        return 0;
    if (unlikely(s1l == 0 && s2l == 0))
        return 1;
    assert(s1v != NULL && s2v != NULL);
    return (memcmp(s1v, s2v, s1l * sizeof(*s1v)) == 0);
}

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
    if (len * sizeof(h64wchar) <= POOLEDSTRSIZE) {
        v->s = poolalloc_malloc(
            vthread->str_pile, 0
        );
    } else {
        v->s = malloc(sizeof(h64wchar) * len);
    }
    v->len = len;
    return (v->s != NULL);
}

void vmstrings_Free(h64vmthread *vthread, h64stringval *v) {
    if (!vthread || !v)
        return;
    if (v->len * sizeof(h64wchar) <= POOLEDSTRSIZE) {
        poolalloc_free(vthread->str_pile, v->s);
    } else {
        free(v->s);
    }
    v->len = 0;
}

int vmbytes_Equality(
        valuecontent *v1, valuecontent *v2
        ) {
    char *s1v = NULL;
    char *s2v = NULL;
    int64_t s1l = 0;
    int64_t s2l = 0;
    if (v1->type == H64VALTYPE_SHORTBYTES) {
        s1v = (char*) v1->shortbytes_value;
        s1l = v1->shortbytes_len;
    } else if (v1->type == H64VALTYPE_CONSTPREALLOCBYTES) {
        s1v = (char*) v1->constpreallocbytes_value;
        s1l = v1->constpreallocbytes_len;
    } else if (v1->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue*)v1->ptr_value)->type == H64GCVALUETYPE_BYTES) {
        s1v = (char*) ((h64gcvalue*)v1->ptr_value)->bytes_val.s;
        s1l = ((h64gcvalue*)v1->ptr_value)->bytes_val.len;
    }
    if (v2->type == H64VALTYPE_SHORTBYTES) {
        s2v = (char*) v2->shortbytes_value;
        s2l = v2->shortbytes_len;
    } else if (v2->type == H64VALTYPE_CONSTPREALLOCBYTES) {
        s2v = (char*) v2->constpreallocbytes_value;
        s2l = v2->constpreallocbytes_len;
    } else if (v2->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue*)v2->ptr_value)->type == H64GCVALUETYPE_BYTES) {
        s2v = (char*) ((h64gcvalue*)v2->ptr_value)->bytes_val.s;
        s2l = ((h64gcvalue*)v2->ptr_value)->bytes_val.len;
    }
    if (!s1v || !s2v || s1l != s2l)
        return 0;
    return (memcmp(s1v, s2v, s1l) == 0);
}

int vmbytes_AllocBuffer(
        h64vmthread *vthread,
        h64bytesval *v, uint64_t len) {
    if (!vthread || !v)
        return 0;
    if (!vthread->str_pile) {
        vthread->str_pile = poolalloc_New(POOLEDSTRSIZE);
        if (!vthread->str_pile)
            return 0;
    }
    if (len <= POOLEDSTRSIZE) {
        v->s = poolalloc_malloc(
            vthread->str_pile, 0
        );
    } else {
        v->s = malloc(len);
    }
    v->len = len;
    return (v->s != NULL);
}

void vmbytes_Free(h64vmthread *vthread, h64bytesval *v) {
    if (!vthread || !v)
        return;
    if (v->len <= POOLEDSTRSIZE) {
        poolalloc_free(vthread->str_pile, v->s);
    } else {
        free(v->s);
    }
    v->len = 0;
}
