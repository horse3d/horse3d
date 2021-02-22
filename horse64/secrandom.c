// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#if defined(_WIN64) || defined(_WIN32)
#include <windows.h>
#include <wincrypt.h>
#else
#include <errno.h>
#if !defined(ANDROID) && !defined(__ANDROID__)
#include <sys/random.h>
#endif
#endif

#include "nonlocale.h"
#include "secrandom.h"


// Global state for when direct file handle to /dev/urandom is used:
#if defined(ANDROID) || defined(__ANDROID__)
static FILE *urandom = NULL;
#endif


#if !defined(_WIN32) && !defined(_WIN64)
// Warning: this function uses /dev/urandom non-blocking.
// This is probably good enough for most uses, but for generation of
// strong GPG key and such it is a bit undesirable.
int _secrandom_GetBytesUpTo256(char *ptr, size_t amount) {
    if (amount == 0)
        return 1;

    int attempts = 0;
    while (attempts < 100) {
        attempts += 1;

        #if defined(ANDROID) || defined(__ANDROID__)
        // Android doesn't have the wrappers until in late versions,
        // so we have to read /dev/urandom manually:
        if (!urandom)
            urandom = fopen("/dev/urandom", "rb");
        size_t bytesread = 0;
        char *p = ptr;
        do {
            int result = (int)fread(
                p, 1,
                amount - bytesread, urandom
            );
            if (result <= 0)
                return 0;
            p += result;
            bytesread += result;
        } while (bytesread < amount);
        return 1;
        #else
        // We'll use getentropy if not on android:
        int result = getentropy(ptr, amount);
        if (result != 0) {
            if (errno == EIO && attempts < 1000) {
                usleep(20);  // wait a short amount of time, retry
                continue;
            }
            return 0;
        }
        return 1;
        #endif
    }
    return 0;
}
#else
static HCRYPTPROV hProvider = 0;
static void _secrandom_initProvider() {
    if (hProvider)
        return;
    int result = CryptAcquireContext(
        &hProvider, NULL, NULL, PROV_RSA_FULL, 0
    );
    if (result == FALSE) {
        if ((uint32_t)GetLastError() == (uint32_t)NTE_BAD_KEYSET) {
            result = CryptAcquireContext(
                &hProvider, NULL, NULL, PROV_RSA_FULL, CRYPT_NEWKEYSET
            );
        }
    }
    if (result == FALSE) {
        LPSTR s = NULL;
        FormatMessage(
            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS |
            FORMAT_MESSAGE_ARGUMENT_ARRAY | FORMAT_MESSAGE_ALLOCATE_BUFFER,
            NULL, GetLastError(), 0, (LPSTR)&s, 0, NULL
        );
        char buf[1024];
        h64snprintf(
            buf, sizeof(buf) - 1,
            "Couldn't acquire HCRYPTPROV: %s", s
        );
        if (s)
            LocalFree(s);
        h64fprintf(
            stderr, "horse64/secrandom.c: error: %s\n", buf
        );
        int msgboxID = MessageBox(
            NULL, buf, "horse64/secrandom.c ERROR",
            MB_OK|MB_ICONSTOP
        );
        exit(1);
    }
}
__attribute__((constructor)) static void _ensureInit() {
    _secrandom_initProvider();
}
#endif


int secrandom_GetBytes(char *ptr, size_t amount) {
    #if !defined(_WIN32) && !defined(_WIN64)
    char *p = ptr;
    size_t fetched = 0;
    while (fetched < amount) {
        size_t next_bytes = amount - fetched;
        if (next_bytes > 256)
            next_bytes = 256;
        if (!_secrandom_GetBytesUpTo256(p, next_bytes))
            return 0;
        fetched += next_bytes;
        p += next_bytes;
    }
    return 1;
    #else
    if (!hProvider)
        _secrandom_initProvider();
    memset(ptr, 0, amount);
    return (CryptGenRandom(hProvider, amount, (uint8_t *)ptr) != FALSE);
    #endif
}

int64_t secrandom_RandIntRange(int64_t min, int64_t max) {
    assert(max >= min);
    uint64_t range = max - min;
    if (range == 0)
        return min;
    if (range == UINT64_MAX)
        return secrandom_RandInt();
    range++;  // Inclusive maximum.
    uint64_t safe_modulo_range = (
        ((int64_t)(UINT64_MAX / range)) * range
    );
    if (safe_modulo_range < range)
        safe_modulo_range = range;
    uint64_t i = 0;
    while (1) {
        while (!secrandom_GetBytes((char *)&i, sizeof(i))) {
            // repeat until we get a value.
        }
        if (i < safe_modulo_range)
            break;
    }
    return min + (i % range);
}

int64_t secrandom_RandInt() {
    int64_t i = 0;
    while (!secrandom_GetBytes((char *)&i, sizeof(i))) {
        // repeat until we get a value.
    }
    return i;
}

#include "vendor/erand48_impl.c"

double secrandom_Rand0ToExclusive1() {
    while (1) {
        uint16_t r[3];
        while (!secrandom_GetBytes((char *)r, sizeof(r))) {
            // repeat until we get a value.
        }
        double result = netbsd_erand48(r);
        if (likely(result >= 0 && result < 1.0))
            return result;
    }
}