// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#if defined(_WIN32) || defined(_WIN64)
#define WINVER 0x0600
#define _WIN32_WINNT 0x0600
#endif

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <time.h>
#include <unistd.h>
#endif


uint64_t datetime_Ticks() {
    #if defined(_WIN32) || defined(_WIN64)
    return GetTickCount64();
    #else
    struct timespec spec;

    clock_gettime(CLOCK_MONOTONIC, &spec);

    uint64_t ms1 = ((uint64_t)spec.tv_sec) * 1000ULL;
    uint64_t ms2 = ((uint64_t)spec.tv_nsec) / 1000000ULL;
    return ms1 + ms2;
    #endif
}


void datetime_Sleep(uint64_t ms) {
    #if defined(_WIN32) || defined(_WIN64)
    Sleep(ms);
    #else
    usleep(ms * 1000UL);
    #endif
}
