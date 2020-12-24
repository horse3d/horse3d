// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_COMPILECONFIG_H_
#define HORSE64_COMPILECONFIG_H_


#define ATTR_UNUSED __attribute__((unused))

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

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
