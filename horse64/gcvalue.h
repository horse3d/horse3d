// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_GCVALUE_H_
#define HORSE64_GCVALUE_H_

#include <stdint.h>

#include "compiler/globallimits.h"
#include "vmliststruct.h"
#include "vmstringsstruct.h"

typedef struct valuecontent valuecontent;
typedef struct h64gcvalue h64gcvalue;

typedef struct hashmap hashmap;

typedef enum gcvaluetype {
    H64GCVALUETYPE_INVALID = 0,
    H64GCVALUETYPE_FUNCREF_CLOSURE = 1,
    H64GCVALUETYPE_STRING,
    H64GCVALUETYPE_BYTES,
    H64GCVALUETYPE_LIST,
    H64GCVALUETYPE_SET,
    H64GCVALUETYPE_MAP,
    H64GCVALUETYPE_OBJINSTANCE,
    H64GCVALUETYPE_TOTAL_COUNT
} gcvaluetype;

typedef struct h64closureinfo {
    int64_t closure_func_id;
    h64gcvalue *closure_self;
    int closure_bound_values_count;
    valuecontent *closure_bound_values;
} h64closureinfo;


typedef struct h64gcvalue {
    uint8_t type;
    int32_t heapreferencecount, externalreferencecount;
    uint32_t hash;
    union {
        struct {
            h64stringval str_val;
        };
        struct {
            h64bytesval bytes_val;
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
            h64closureinfo *closure_info;
        };
        struct {
            classid_t class_id;
            struct {
                valuecontent *varattr;
            };
            void *cdata;
        };
    };
} h64gcvalue;

#endif  // HORSE64_GCVALUE_H_
