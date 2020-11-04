// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <math.h>
#include <string.h>
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <unistd.h>
#endif
#include <time.h>

#include "corelib/errors.h"
#include "datetime.h"
#include "process.h"
#include "stack.h"
#include "vmexec.h"

/// @module process Run or interact with other processes on the same machine.


int timelib_sleep(
        h64vmthread *vmthread
        ) {
    /**
     * Sleep for the given amount of seconds. Fractional values are supported,
     * e.g. it is possible to sleep 0.01 seconds. If the system goes into
     * suspend or hibernate while your function is sleeping, then the
     * remaining sleep interval will be resumed post suspend (so the suspend
     * does not count into the sleep interval).
     *
     * @func sleep
     * @param amount the amount of seconds to sleep
     */
    assert(STACK_TOP(vmthread->stack) >= 1);

    valuecontent *vcamount = STACK_ENTRY(vmthread->stack, 0);
    if (vcamount->type != H64VALTYPE_INT64 &&
            vcamount->type != H64VALTYPE_FLOAT64) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "amount must be a number"
        );
    }
    int64_t sleepms = 0;
    if (vcamount->type == H64VALTYPE_INT64) {
        if (unlikely(vcamount->int_value < 0)) {
            sleepms = 0;
        } else if (vcamount->int_value < INT64_MAX / 1000000LL) {
            sleepms = vcamount->int_value * 1000LL;
        } else {
            sleepms = INT64_MAX / 1000LL;
        }
    } else {
        assert(vcamount->type == H64VALTYPE_FLOAT64);
        long double val = vcamount->float_value * 1000.0;
        if (val <= 0) {
            sleepms = 0;
        } else if (val < (long double)(INT64_MAX / 1000LL) - 5.0f) {
            // (-5.0f for some rounding margin)
            sleepms = roundl(val);
        } else {
            sleepms = INT64_MAX / 1000LL;
        }
    }

    return vmschedule_SuspendFunc(
        vmthread, SUSPENDTYPE_FIXEDTIME,
        datetime_Ticks() + sleepms
    );
}

int timelib_ticks(
        h64vmthread *vmthread
        ) {
    /**
     * Returns a monotonic time value, which starts at some arbitrary value
     * at program start and then increases linearly with passing time in
     * seconds. Most notably, it will be independent of changes to system
     * time (e.g. when system time is adjusted forward or backward by the
     * user), but it will reflect time spent in suspend or hibernate after
     * wake-up.
     *
     * @func ticks
     * @returns seconds passed as a @see{number}, starting from some
     *   arbitrary value at program start.
     */
    assert(STACK_TOP(vmthread->stack) >= 0);

    if (STACK_TOP(vmthread->stack) == 0) {
        int result = stack_ToSize(
            vmthread->stack, vmthread->stack->entry_count + 1, 0
        );
        if (!result)
            return vmexec_ReturnFuncError(vmthread,
                H64STDERROR_OUTOFMEMORYERROR,
                "out of memory when returning value"
            );
    }

    int64_t tickms = datetime_Ticks();
    double seconds = (double)((int64_t)tickms / 1000LL);
    double fraction = ((double)((int64_t)tickms % 1000LL)) / 1000.0;

    valuecontent *vresult = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vresult);
    valuecontent_Free(vresult);
    memset(vresult, 0, sizeof(*vresult));
    vresult->type = H64VALTYPE_FLOAT64;
    vresult->float_value = seconds + fraction;
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