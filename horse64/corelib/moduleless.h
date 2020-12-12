// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_CORELIB_MODULELESS_H_
#define HORSE64_CORELIB_MODULELESS_H_

#include "widechar.h"
#include "valuecontentstruct.h"

typedef struct h64program h64program;
typedef struct h64vmthread h64vmthread;

int corelib_RegisterFuncsAndModules(h64program *p);

h64wchar *corelib_value_to_str(
    h64vmthread *vmthread,
    valuecontent *c, h64wchar *tempbuf, int tempbuflen,
    int64_t *outlen
);

#endif  // HORSE64_CORELIB_MODULELESS_H_
