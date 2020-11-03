// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "corelib/errors.h"
#include "osinfo.h"
#include "stack.h"
#include "system.h"
#include "valuecontentstruct.h"
#include "vmexec.h"


int systemlib_cores(
        h64vmthread *vmthread
        ) {
    /**
     * Get the amount of processor cores on the current system.
     *
     * @func cores
     * @param physical=false
     *    Whether to return the logical cores of the CPU
     *    (physical=false, the default), or the physical ones
     *    (physical=true).
     * @returns the processor cores as a @see{number}.
     */
    assert(STACK_TOP(vmthread->stack) >= 1);

    valuecontent *vcphysical = STACK_ENTRY(vmthread->stack, 0);
    if (vcphysical->type != H64VALTYPE_BOOL &&
            vcphysical->type != H64VALTYPE_UNSPECIFIED_KWARG) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "physical must be a boolean"
        );
    }
    int physical = 0;
    if (vcphysical->type == H64VALTYPE_BOOL)
        physical = (vcphysical->int_value != 0);

    valuecontent *retval = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(retval);
    valuecontent_Free(retval);
    memset(retval, 0, sizeof(*retval));
    retval->type = H64VALTYPE_INT64;
    retval->int_value = (
        physical == 0 ? osinfo_CpuThreads() :
        osinfo_CpuCores()
    );
    return 1;
}

int systemlib_RegisterFuncsAndModules(h64program *p) {
    // system.cores:
    const char *system_cores_kw_arg_name[] = {
        "physical"
    };
    int64_t idx;
    idx = h64program_RegisterCFunction(
        p, "cores", &systemlib_cores,
        NULL, 1, system_cores_kw_arg_name, 0,  // fileuri, args
        "system", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;
    return 1;
}