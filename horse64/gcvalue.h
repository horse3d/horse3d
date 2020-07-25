// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_GCVALUE_H_
#define HORSE64_GCVALUE_H_

#include <stdint.h>

#include "vmlist.h"
#include "vmstrings.h"

typedef struct valuecontent valuecontent;

typedef struct hashmap hashmap;

typedef enum gcvaluetype {
    H64GCVALUETYPE_INVALID = 0,
    H64GCVALUETYPE_CLASSINSTANCE = 1,
    H64GCVALUETYPE_ERRORCLASSINSTANCE,
    H64GCVALUETYPE_FUNCREF_CLOSURE,
    H64GCVALUETYPE_EMPTYARG,
    H64GCVALUETYPE_ERROR,
    H64GCVALUETYPE_STRING,
    H64GCVALUETYPE_LIST,
    H64GCVALUETYPE_SET,
    H64GCVALUETYPE_VECTOR,
    H64GCVALUETYPE_MAP,
    H64GCVALUETYPE_TOTAL_COUNT
} gcvaluetype;

typedef struct h64closureinfo {
    int64_t closure_func_id;
    h64gcvalue *closure_self;
    int closure_vbox_count;
    h64gcvalue *closure_vbox;
} h64closureinfo;


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
        struct {
            genericset set_values;
        };
        struct {
            genericmap map_values;
        };
        struct {
            genericlist *list_values;
        };
        struct {
            int vector_len;
            vectorentry *vector_values;
        };
        struct {
            h64closureinfo *closure_info;
        };
    };
} h64gcvalue;

#endif  // HORSE64_GCVALUE_H_
