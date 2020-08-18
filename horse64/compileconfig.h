// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_COMPILECONFIG_H_
#define HORSE64_COMPILECONFIG_H_


#define ATTR_UNUSED __attribute__((unused))

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)


#endif  // HORSE64_COMPILECONFIG_H_
