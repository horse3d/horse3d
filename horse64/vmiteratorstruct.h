// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_VMITERATORSTRUCT_H_
#define HORSE64_VMITERATORSTRUCT_H_

#include "compileconfig.h"

#include <stdint.h>

#include "valuecontentstruct.h"


typedef struct h64gcvalue h64gcvalue;

typedef struct h64iteratorstruct {
    int iterated_isgcvalue;
    union {
        struct {
            h64gcvalue *iterated_gcvalue;
            uint64_t iterated_revision;
        };
        valuecontent iterated_vector;
    };
    uint64_t idx, len;
} h64iteratorstruct;


#endif  // HORSE64_VMITERATORSTRUCT_H_