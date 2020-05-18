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
    H64STDERROR_ERROR = 0,
    H64STDERROR_RUNTIMEERROR = 1,
    H64STDERROR_OUTOFMEMORYERROR,
    H64STDERROR_OSERROR,
    H64STDERROR_IOERROR,
    H64STDERROR_TOTAL_COUNT
} stderrorclassnum;

static const char *stderrorclassnames[] = {
    "Error",
    "RuntimeError",
    "OutOfMemoryError",
    "OSError",
    "IOError",
    NULL
};

int corelib_RegisterErrorClasses(
    h64program *p
);

#endif  // HORSE64_CORELIB_ERRORS_H_