// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bytecode.h"
#include "corelib/errors.h"
#include "poolalloc.h"
#include "gcvalue.h"
#include "stack.h"
#include "vmexec.h"

int corelib_RegisterErrorClasses(
        h64program *p
        ) {
    if (!p)
        return 0;

    assert(p->classes_count == 0);

    int i = 0;
    while (i < H64STDERROR_TOTAL_COUNT) {
        assert(stderrorclassnames[i] != NULL);
        int idx = h64program_AddClass(
            p, stderrorclassnames[i], NULL, NULL, NULL
        );
        if (idx >= 0) {
            assert(p->classes_count - 1 == idx);
            p->classes[i].is_error = 1;
            if (idx > 0)
                p->classes[i].base_class_global_id = (
                    0 // "Exception" is base classes
                );
        } else {
            return 0;
        }
        i++;
    }

    return 1;
}
