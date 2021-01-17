// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_CORELIB_ERRORS_H_
#define HORSE64_CORELIB_ERRORS_H_

#include "compileconfig.h"

#include <stdlib.h>  // for 'NULL'

typedef struct h64program h64program;
typedef struct h64vmthread h64vmthread;


typedef enum stderrorclassnum {
    H64STDERROR_ERROR = 0,
    H64STDERROR_RUNTIMEERROR = 1,
    H64STDERROR_OUTOFMEMORYERROR,
    H64STDERROR_OSERROR,
    H64STDERROR_IOERROR,
    H64STDERROR_PERMISSIONERROR,
    H64STDERROR_ARGUMENTERROR,
    H64STDERROR_TYPEERROR,
    H64STDERROR_VALUEERROR,
    H64STDERROR_ATTRIBUTEERROR,
    H64STDERROR_INDEXERROR,
    H64STDERROR_MATHERROR,
    H64STDERROR_OVERFLOWERROR,
    H64STDERROR_INVALIDDESTRUCTORERROR,
    H64STDERROR_INVALIDNOASYNCRESOURCEERROR,
    H64STDERROR_ENCODINGERROR,
    H64STDERROR_ASSERTIONERROR,
    H64STDERROR_TOTAL_COUNT
} stderrorclassnum;

ATTR_UNUSED static const char *stderrorclassnames[] = {
    "Error",
    "RuntimeError",
    "OutOfMemoryError",
    "OSError",
    "IOError",
    "PermissionError",
    "ArgumentError",
    "TypeError",
    "ValueError",
    "AttributeError",
    "IndexError",
    "MathError",
    "OverflowError",
    "InvalidDestructorError",
    "InvalidNoasyncResourceError",
    "EncodingError",
    "AssertionError",
    NULL
};

int corelib_RegisterErrorClasses(
    h64program *p
);

#endif  // HORSE64_CORELIB_ERRORS_H_
