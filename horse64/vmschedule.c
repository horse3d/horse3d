// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <string.h>

#include "stack.h"
#include "valuecontentstruct.h"
#include "vmexec.h"
#include "vmschedule.h"


int vmschedule_SuspendFunc(
        h64vmthread *vmthread, suspendtype suspend_type,
        int64_t suspend_intarg
        ) {
    if (STACK_TOP(vmthread->stack) == 0) {
        if (!stack_ToSize(
                vmthread->stack,
                STACK_TOTALSIZE(vmthread->stack) + 1, 1
                ))
            return 0;
    }
    valuecontent *vc = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vc);
    valuecontent_Free(vc);
    memset(vc, 0, sizeof(*vc));
    vc->type = H64VALTYPE_THREADSUSPENDINFO;
    vc->suspend_type = suspend_type;
    vc->suspend_intarg = suspend_intarg;
    return 0;
}
