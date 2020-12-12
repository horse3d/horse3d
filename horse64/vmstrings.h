// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_VMSTRINGS_H_
#define HORSE64_VMSTRINGS_H_

#include "compileconfig.h"

#include <assert.h>
#include <stdint.h>

#include "widechar.h"

typedef uint32_t h64wchar;
typedef struct h64vmthread h64vmthread;
typedef struct valuecontent valuecontent;

#include "vmstringsstruct.h"


ATTR_UNUSED static inline void vmstrings_RequireLetterLen(
        h64stringval *v
        ) {
    if (v->len != 0 && v->letterlen == 0) {
        v->letterlen = utf32_letters_count(
            v->s, v->len
        );
        assert(v->letterlen > 0);
    }
}

int vmstrings_Equality(
    valuecontent *v1, valuecontent *v2
);

int vmbytes_Equality(
    valuecontent *v1, valuecontent *v2
);

int vmstrings_AllocBuffer(
    h64vmthread *vthread, h64stringval *v, uint64_t len
);

void vmstrings_Free(h64vmthread *vthread, h64stringval *v);

int vmbytes_AllocBuffer(
    h64vmthread *vthread, h64bytesval *v, uint64_t len
);

void vmbytes_Free(h64vmthread *vthread, h64bytesval *v);

#endif  // HORSE64_VMSTRINGS_H_
