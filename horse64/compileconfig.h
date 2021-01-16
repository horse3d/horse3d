// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_COMPILECONFIG_H_
#define HORSE64_COMPILECONFIG_H_

#include <math.h>
#include <stdint.h>

#define ATTR_UNUSED __attribute__((unused))

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define DBL_MAX_CONSECUTIVE_INT (9007199254740992LL)
#define DBL_MIN_CONSECUTIVE_INT (-9007199254740992LL)

static int is_double_to_int64_nonoverflowing(double x) {
    return (
        x < 9223372036854775808.0 &&  // INT64_MAX + 1
        x >= -9223372036854775808.0  // INT64_MIN
    );
    // Note:
    // doubles cannot hold INT64_MAX and will round it to
    // INT64_MAX + 1. As a result (x <= INT64_MAX) does not work,
    // but (x < INT64_MAX + 1) does!
}

ATTR_UNUSED static int64_t clamped_round(double x) {
    x = round(x);
    if (!is_double_to_int64_nonoverflowing(x)) {
        if (x > 0)
            return INT64_MAX;
        return INT64_MIN;
    }
    return x;
}

#define DEBUG_SOCKETPAIR

#if defined(_WIN32) || defined(_WIN64)
// Require Windows 7 or newer:
#define _WIN32_WINNT 0x0601
#if defined __MINGW_H
#define _WIN32_IE 0x0400
#endif
#endif

// FD set size on Windows:
#if defined(_WIN32) || defined(_WIN64)
#define FD_SETSIZE 1024
#endif

#define USE_POLL_ON_UNIX 1

#endif  // HORSE64_COMPILECONFIG_H_
