// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <string.h>

#include "process.h"
#include "stack.h"
#include "vmexec.h"

/// @module process Run or interact with other processes on the same machine.

int timelib_sleep(
        h64vmthread *vmthread
        ) {
    /**
     * Sleep for the given amount of seconds. Fractional values are supported,
     * e.g. it is possible to sleep 0.01 seconds.
     *
     * @func sleep
     * @param amount the amount of seconds to sleep
     */
    assert(STACK_TOP(vmthread->stack) >= 1);

    valuecontent *vresult = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vresult);
    valuecontent_Free(vresult);
    memset(vresult, 0, sizeof(*vresult));
    vresult->type = H64VALTYPE_NONE;
    return 1;
}

int timelib_ticks(
        h64vmthread *vmthread
        ) {
    /**
     * Returns a monotonic time value, which starts at some arbitrary value
     * at program start and then increases linearly with passing time in
     * seconds. Most notably, it will also increase linearly and not jump
     * around when e.g. the system time is changed. However, it will reflect
     * actual real time gaps, e.g. when the system was suspended/hibernated
     * in between two calls.
     *
     * @func sleep
     * @returns seconds passed as a @see{number} starting from some
     *   arbitrary value at program start.
     */
    assert(STACK_TOP(vmthread->stack) >= 1);

    valuecontent *vresult = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vresult);
    valuecontent_Free(vresult);
    memset(vresult, 0, sizeof(*vresult));
    vresult->type = H64VALTYPE_NONE;
    return 1;
}

int timelib_RegisterFuncsAndModules(h64program *p) {
    // time.sleep:
    const char *time_sleep_kw_arg_name[] = {
        NULL
    };
    int64_t idx;
    idx = h64program_RegisterCFunction(
        p, "sleep", &timelib_sleep,
        NULL, 1, time_sleep_kw_arg_name, 0,  // fileuri, args
        "time", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    // time.ticks:
    const char *time_ticks_kw_arg_name[] = {
        NULL
    };
    idx = h64program_RegisterCFunction(
        p, "ticks", &timelib_ticks,
        NULL, 0, time_ticks_kw_arg_name, 0,  // fileuri, args
        "time", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    return 1;
}