#ifndef HORSE64_REFVAL_H_
#define HORSE64_REFVAL_H_

#include <stdint.h>

#include "vmstrings.h"

typedef struct valuecontent valuecontent;

typedef enum refvaluetype {
    H64REFVALTYPE_INVALID = 0,
    H64REFVALTYPE_CLASSINSTANCE = 1,
    H64REFVALTYPE_ERRORCLASSINSTANCE = 2,
    H64REFVALTYPE_CFUNCREF = 3,
    H64REFVALTYPE_EMPTYARG = 4,
    H64REFVALTYPE_ERROR = 5,
    H64REFVALTYPE_STRING = 6,
    H64REFVALTYPE_SHORTSTR = 7
} refvaluetype;

typedef struct h64refvalue {
    uint8_t type;
    int heapreferencecount, externalreferencecount;
    union {
        struct {
            int classid;
            valuecontent *membervars;
        };
        struct {
            h64stringval *str_val;
        };
        struct {
            unicodechar shortstr_val[3];
            uint8_t shortstr_len;
        };
    };
} h64refvalue;

#endif  // HORSE64_REFVAL_H_
