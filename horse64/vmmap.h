// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_VMMAP_H_
#define HORSE64_VMMAP_H_

#include "compileconfig.h"

#include <assert.h>
#include <stdio.h>

#include "bytecode.h"
#include "vmcontainerstruct.h"

genericmap *vmmap_New();

ATTR_UNUSED static inline int64_t vmmap_Count(genericmap *m) {
    if ((m->flags & GENERICMAP_FLAG_LINEAR) != 0)
        return m->linear.entry_count;
    return m->hashed.entry_count;
}

int vmmap_Set(
    genericmap *m, valuecontent *key, valuecontent *value
);

int vmmap_Remove(genericmap *m, valuecontent *key);

int vmmap_Contains(genericmap *m, valuecontent *key);

int vmmap_Get(
    genericmap *m, valuecontent *key, valuecontent *value
);

#endif  // HORSE64_VMMAP_H_
