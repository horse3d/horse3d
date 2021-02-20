// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_VALUECONTENTSTRUCT_H_
#define HORSE64_VALUECONTENTSTRUCT_H_

#include "compileconfig.h"

#include <stdint.h>

#include "compiler/globallimits.h"
#include "vmschedule.h"
#include "widechar.h"


typedef enum valuetype {
    H64VALTYPE_NONE = 0,
    H64VALTYPE_INT64 = 1,
    H64VALTYPE_FLOAT64,
    H64VALTYPE_BOOL,
    H64VALTYPE_FUNCREF,
    H64VALTYPE_CLASSREF,
    H64VALTYPE_ERROR,
    H64VALTYPE_GCVAL,
    H64VALTYPE_SHORTSTR,
    H64VALTYPE_CONSTPREALLOCSTR,
    H64VALTYPE_SHORTBYTES,
    H64VALTYPE_CONSTPREALLOCBYTES,
    H64VALTYPE_VECTOR,
    H64VALTYPE_UNSPECIFIED_KWARG,
    H64VALTYPE_SUSPENDINFO,
    H64VALTYPE_ITERATOR,
    H64VALTYPE_TOTAL
} valuetype;

#define VALUECONTENT_SHORTSTRLEN 2
#define VALUECONTENT_SHORTBYTESLEN 7

typedef struct h64errorinfo h64errorinfo;
typedef struct valuecontent valuecontent;
typedef struct vectorentry vectorentry;
typedef struct h64iteratorstruct h64iteratorstruct;

typedef struct valuecontent {
    uint8_t type;
    union {
        int64_t int_value;  // 8 bytes
        double float_value;   // 8 bytes
        void *ptr_value;   // 4 or 8 bytes
        struct {   // 9 bytes
            uint8_t shortstr_len;
            h64wchar shortstr_value[
                VALUECONTENT_SHORTSTRLEN
            ];  // should be 2byte/16bit aligned
        };
        struct {   // 8 bytes
            uint8_t shortbytes_len;
            char shortbytes_value[
                VALUECONTENT_SHORTBYTESLEN
            ];  // should be 2byte/16bit aligned
        };
        struct {   // 12 bytes
            h64wchar *constpreallocstr_value;
            int32_t constpreallocstr_len;
        };
        struct {   // 12 bytes
            char *constpreallocbytes_value;
            int32_t constpreallocbytes_len;
        };
        struct {   // 12 bytes
            classid_t error_class_id;
            h64errorinfo *einfo;
        };
        struct {  // 12 bytes
            int32_t vector_len;
            vectorentry *vector_values;
        };
        struct {  // 12 bytes
            int suspend_type;
            int64_t suspend_intarg;
        };
        struct {  // 16 bytes
            h64iteratorstruct *iterator;
        };
    };
} valuecontent;

typedef struct h64vmthread h64vmthread;

int valuecontent_SetStringU8(
    h64vmthread *vmthread, valuecontent *v, const char *u8
);

int valuecontent_SetPreallocStringU8(
    h64program *p, valuecontent *v, const char *u8
);

int valuecontent_SetStringU32(
    h64vmthread *vmthread, valuecontent *v,
    const h64wchar *u32, int64_t u32len
);

int valuecontent_SetBytesU8(
    h64vmthread *vmthread, valuecontent *v,
    uint8_t *bytes, int64_t byteslen
);


uint32_t valuecontent_Hash(
    valuecontent *v
);

int valuecontent_IsMutable(valuecontent *v);

HOTSPOT int valuecontent_CheckEquality(
    h64vmthread *vmthread,
    valuecontent *v1, valuecontent *v2, int *oom
);

HOTSPOT int valuecontent_CompareValues(
    valuecontent *v1, valuecontent *v2,
    int *result, int *typesnotcomparable
);

#endif  // HORSE64_VALUECONTENTSTRUCT_H_