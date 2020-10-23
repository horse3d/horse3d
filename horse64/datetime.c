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

#include "threading.h"


uint64_t datetime_Ticks() {
    #if defined(_WIN32) || defined(_WIN64)
    return GetTickCount64();
    #else
    struct timespec spec;
    clock_gettime(CLOCK_BOOTTIME, &spec);
    uint64_t ms1 = ((uint64_t)spec.tv_sec) * 1000ULL;
    uint64_t ms2 = ((uint64_t)spec.tv_nsec) / 1000000ULL;
    return ms1 + ms2;
    #endif
}

mutex *_nosuspendticksmutex = NULL;
int64_t _nosuspendlastticks = 0;
int64_t _nosuspendoffset = 0;

uint64_t datetime_TicksNoSuspendJump() {
    if (!_nosuspendticksmutex) {
        _nosuspendticksmutex = mutex_Create();
    if (!_nosuspendticksmutex)
        return 0;
    }
    int64_t ticks = datetime_Ticks();
    mutex_Lock(_nosuspendticksmutex);
    ticks += _nosuspendoffset;
    if (ticks > _nosuspendlastticks + 500L) {
        _nosuspendoffset -= (ticks - _nosuspendlastticks - 10L);
        ticks = _nosuspendlastticks + 10L;
    }
    mutex_Release(_nosuspendticksmutex);
    return ticks;
}

__attribute__((constructor)) void datetime_NoSuspendJumpMutex() {
    if (!_nosuspendticksmutex)
        _nosuspendticksmutex = mutex_Create();
}


void datetime_Sleep(uint64_t sleepms) {
    #if defined(_WIN32) || defined(_WIN64)
    Sleep(sleepms);
    #else
    #if _POSIX_C_SOURCE >= 199309L
    struct timespec ts;
    ts.tv_sec = sleepms / 1000LL;
    ts.tv_nsec = (sleepms % 1000LL) * 1000LL;
    nanosleep(&ts, NULL);
    #else
    if (sleepms >= 1000LL) {
        sleep(sleepms / 1000LL);
        sleepms = sleepms % 1000LL;
    }
    usleep(1000LL * sleepms);
    #endif
    #endif
}
