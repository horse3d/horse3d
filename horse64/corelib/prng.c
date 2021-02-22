// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "bytecode.h"
#include "compileconfig.h"

#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "corelib/errors.h"
#include "corelib/random.h"
#include "secrandom.h"
#include "stack.h"
#include "threading.h"
#include "valuecontentstruct.h"
#include "vendor/mtwister/mtwister.c"
#include "vmexec.h"


mutex *_shared_random_mutex = NULL;
MTRand globalrand = {0};
int globalrandseeded = 0;

__attribute__((constructor)) static void _create_mutex() {
    if (_shared_random_mutex)
        return;

    _shared_random_mutex = mutex_Create();
    if (!_shared_random_mutex) {
        fprintf(stderr,
            "prng.c: error: failed to create mutex, out of memory?\n"
        );
        exit(1);
    }
}

static void _requireseeded() {
    if (!globalrandseeded) {
        globalrandseeded = 1;
        uint32_t u;
        while (!secrandom_GetBytes((char *)&u, sizeof(u))) {
            // repeat until we get a value.
        }
        globalrand = seedRand(u);
    }
}

static uint64_t urandint64() {
    mutex_Lock(_shared_random_mutex);
    _requireseeded();
    uint32_t i1 = genRandLong(&globalrand);
    uint32_t i2 = genRandLong(&globalrand);
    mutex_Release(_shared_random_mutex);
    uint64_t i;
    memcpy(&i, &i1, sizeof(i1));
    memcpy(((char*)&i) + sizeof(i1), &i2, sizeof(i2));
    return i;
}

static int64_t randint64() {
    mutex_Lock(_shared_random_mutex);
    _requireseeded();
    uint32_t i1 = genRandLong(&globalrand);
    uint32_t i2 = genRandLong(&globalrand);
    mutex_Release(_shared_random_mutex);
    int64_t i;
    memcpy(&i, &i1, sizeof(i1));
    memcpy(((char*)&i) + sizeof(i1), &i2, sizeof(i2));
    return i;
}

static double randdbl() {
    mutex_Lock(_shared_random_mutex);
    _requireseeded();
    double v = genRand(&globalrand);
    mutex_Release(_shared_random_mutex);
    return v;
}


int prnglib_randint(
        h64vmthread *vmthread
        ) {
    /**
     * This function returns a random integer number optionally
     * limited to a given given range, with no fractions (only
     * integers), with minimum and maximum inclusive.
     *
     * This uses an UNSAFE pseudo random generator that is
     * faster than the random module equivalent, but not
     * cryptographically secure.
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
    int64_t i = 0;
    if (maxval > minval && (maxval != INT64_MAX ||
            minval != INT64_MIN)) {
        uint64_t range = maxval - minval;
        assert(range < UINT64_MAX);
        range += 1;
        uint64_t unbiased_modulo = (
            (UINT64_MAX / range) * range
        );
        if (unbiased_modulo < range)
            unbiased_modulo = range;
        uint64_t u_i;
        while (1) {
            u_i = urandint64();
            if (u_i > unbiased_modulo)
                continue;
            u_i = u_i % range;
            break;
        }
        i = minval + u_i;
    } else if (maxval <= minval) { 
        i = minval;
    } else {
        i = randint64();
    }
    retval->int_value = i;
    ADDREF_NONHEAP(retval);
    return 1;
}

int prnglib_rand(
        h64vmthread *vmthread
        ) {
    /**
     * This function returns a random floating point number
     * which is 0.0 or higher, and always smaller than 1.0
     * (exclusive end).
     *
     * This uses an UNSAFE pseudo random generator that is
     * faster than the random module equivalent, but not
     * cryptographically secure.
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
    retval->type = H64VALTYPE_FLOAT64;
    retval->float_value = randdbl();
    ADDREF_NONHEAP(retval);
    return 1;
}

int prnglib_RegisterFuncsAndModules(h64program *p) {
    int64_t idx;

    // prng.randint
    const char *prandom_randint_kw_arg_name[] = {
        "min", "max"
    };
    idx = h64program_RegisterCFunction(
        p, "randint", &prnglib_randint,
        NULL, 0, 2, prandom_randint_kw_arg_name,  // fileuri, args
        "prng", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    // prng.rand
    const char *prandom_rand_kw_arg_name[] = {
        NULL
    };
    idx = h64program_RegisterCFunction(
        p, "rand", &prnglib_rand,
        NULL, 0, 0, prandom_rand_kw_arg_name,  // fileuri, args
        "prng", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    return 1;
}