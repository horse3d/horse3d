// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_GCVALUE_H_
#define HORSE64_GCVALUE_H_

#include <stdint.h>

#include "vmstrings.h"

typedef struct valuecontent valuecontent;

typedef enum gcvaluetype {
    H64GCVALUETYPE_INVALID = 0,
    H64GCVALUETYPE_CLASSINSTANCE = 1,
    H64GCVALUETYPE_ERRORCLASSINSTANCE = 2,
    H64GCVALUETYPE_CFUNCREF = 3,
    H64GCVALUETYPE_EMPTYARG = 4,
    H64GCVALUETYPE_ERROR = 5,
    H64GCVALUETYPE_STRING = 6
} gcvaluetype;

typedef struct h64gcvalue {
    uint8_t type;
    int heapreferencecount, externalreferencecount;
    union {
        struct {
            int classid;
            valuecontent *membervars;
        };
        struct {
            h64stringval str_val;
        };
    };
} h64gcvalue;

#endif  // HORSE64_GCVALUE_H_
