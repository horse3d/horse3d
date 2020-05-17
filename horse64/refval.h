#ifndef HORSE64_REFVAL_H_
#define HORSE64_REFVAL_H_

#include <stdint.h>

typedef enum refvaluetype {
    H64REFVALTYPE_INVALID = 0,
    H64REFVALTYPE_CLASSINSTANCE = 1,
    H64REFVALTYPE_ERRORCLASSINSTANCE = 2,
    H64REFVALTYPE_CFUNCREF = 3,
    H64REFVALTYPE_EMPTYARG = 4,
    H64REFVALTYPE_ERROR = 5
} refvaluetype;

typedef struct h64refvalue {
    uint8_t type;
    int heapreferencecount, stackreferencecount;
    union {
        int classid;
    };
} h64refvalue;

#endif  // HORSE64_REFVAL_H_
