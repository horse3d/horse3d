// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "bytecode.h"
#include "compileconfig.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "corelib/errors.h"
#include "corelib/random.h"
#include "secrandom.h"
#include "stack.h"
#include "valuecontentstruct.h"
#include "vmexec.h"


int randomlib_randint(
        h64vmthread *vmthread
        ) {
    /**
     * This function returns a random integer number optionally
     * limited to a given given range, with no fractions (only
     * integers), with minimum and maximum inclusive.
     *
     * The random integer is based on true entropy based on
     * /dev/urandom on Unix, or CryptGenRandom on Windows,
     * and the implementation tries to avoid biases even with
     * ranges specified.
     *
     * @func randint
     * @param min=none the minimum of the possible output
     *     range. Can be down to lowest possible @see{number}
     *     value if unspecified.
     * @param max=none the maximum of the possible output range.
     *     Can be up to highest possible @see{number} value
     *     if unspecified.
     * @return a random integer with the given range as a @see{number}
     */
    assert(STACK_TOP(vmthread->stack) >= 2);

    int64_t minval = INT64_MIN;
    valuecontent *vcmin = STACK_ENTRY(vmthread->stack, 0);
    if (vcmin->type == H64VALTYPE_INT64) {
        minval = vcmin->int_value;
    } else if (vcmin->type == H64VALTYPE_FLOAT64) {
        minval = floor(vcmin->int_value);
    } else if (vcmin->type != H64VALTYPE_UNSPECIFIED_KWARG) {
        return  vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "min argument must be a number"
        );
    }
    int64_t maxval = INT64_MAX;
    valuecontent *vcmax = STACK_ENTRY(vmthread->stack, 1);
    if (vcmin->type == H64VALTYPE_INT64) {
        maxval = vcmax->int_value;
    } else if (vcmax->type == H64VALTYPE_FLOAT64) {
        maxval = floor(vcmax->int_value);
    } else if (vcmax->type != H64VALTYPE_UNSPECIFIED_KWARG) {
        return  vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "max argument must be a number"
        );
    }

    valuecontent *retval = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(retval);
    valuecontent_Free(vmthread, retval);
    memset(retval, 0, sizeof(*retval));
    retval->type = H64VALTYPE_INT64;
    retval->int_value = (
        (minval != INT64_MIN || maxval != INT64_MAX) ?
        secrandom_RandIntRange(minval, maxval) :
        secrandom_RandInt()
    );
    ADDREF_NONHEAP(retval);
    return 1;
}


int randomlib_rand(
        h64vmthread *vmthread
        ) {
    /**
     * This function returns a random floating point number
     * which is 0.0 or higher, and always smaller than 1.0
     * (exclusive end).
     *
     * The random number is based on true entropy based on
     * /dev/urandom on Unix, or CryptGenRandom on Windows,
     * and the implementation tries to avoid biases even with
     * ranges specified.
     *
     * @func rand
     * @return a random number from 0.0...1.0 as a @see{number}
     */
    assert(STACK_TOP(vmthread->stack) >= 0);
    if (STACK_TOP(vmthread->stack) < 1) {
        if (!stack_ToSize(
                vmthread->stack, vmthread,
                vmthread->stack->entry_count + 1, 0)) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_OUTOFMEMORYERROR,
                "out of memory allocating return value"
            );
        }
    }

    valuecontent *retval = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(retval);
    valuecontent_Free(vmthread, retval);
    memset(retval, 0, sizeof(*retval));
    retval->type = H64VALTYPE_INT64;
    retval->float_value = secrandom_Rand0ToExclusive1();
    ADDREF_NONHEAP(retval);
    return 1;
}

int randomlib_RegisterFuncsAndModules(h64program *p) {
    int64_t idx;

    // random.randint
    const char *random_randint_kw_arg_name[] = {
        "min", "max"
    };
    idx = h64program_RegisterCFunction(
        p, "randint", &randomlib_randint,
        NULL, 0, 2, random_randint_kw_arg_name,  // fileuri, args
        "random", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    // random.rand
    const char *random_rand_kw_arg_name[] = {
        NULL
    };
    idx = h64program_RegisterCFunction(
        p, "rand", &randomlib_randint,
        NULL, 0, 2, random_rand_kw_arg_name,  // fileuri, args
        "random", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    return 1;
}