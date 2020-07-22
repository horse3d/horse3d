#ifndef HORSE64_CORELIB_ERRORS_H_
#define HORSE64_CORELIB_ERRORS_H_

#include <stdlib.h>  // NULL

typedef struct h64vmthread h64vmthread;

int stderror(
    h64vmthread *vmthread,
    int error_class_id,
    const char *errmsg,
    ...
);

typedef enum stderrorclassnum {
    H64STDERROR_EXCEPTION = 0,
    H64STDERROR_RUNTIMEEXCEPTION = 1,
    H64STDERROR_OUTOFMEMORYERROR,
    H64STDERROR_OSERROR,
    H64STDERROR_IOERROR,
    H64STDERROR_ARGUMENTERROR,
    H64STDERROR_TYPEERROR,
    H64STDERROR_MATHERROR,
    H64STDERROR_TOTAL_COUNT
} stderrorclassnum;

static const char *stderrorclassnames[] = {
    "Exception",
    "RuntimeException",
    "OutOfMemoryError",
    "OSError",
    "IOError",
    "ArgumentError",
    "TypeError",
    "MathError",
    NULL
};

int corelib_RegisterErrorClasses(
    h64program *p
);

#endif  // HORSE64_CORELIB_ERRORS_H_
