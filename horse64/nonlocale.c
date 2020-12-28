// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <locale.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif

#include "horse64/nonlocale.h"

static int h64localeset = 0;
locale_t h64locale = (locale_t) 0;

#if defined(_WIN32) || defined(_WIN64)
HANDLE *h64stdout = NULL;
HANDLE *h64stderr = NULL;
#endif

__attribute__((constructor)) static inline void _genlocale() {
    if (!h64localeset) {
        #if defined(_WIN64) || defined(_WIN32)
        h64locale = _create_locale(LC_ALL, "C");
        #else
        h64locale = newlocale(
            LC_ALL, "C", (locale_t) 0
        );
        #endif
        if (h64locale == (locale_t) 0) {
            h64fprintf(stderr, "failed to generate locale\n");
            _exit(1);
        }
        h64localeset = 1;
    }
}

int64_t h64casecmp_u32(
        const h64wchar *s1, int64_t s1len,
        const h64wchar *s2, int64_t s2len
        ) {
    int i = 0;
    while (i < s1len && i < s2len) {
        h64wchar s1c = s1[i];
        utf32_tolower(&s1c, 1);
        h64wchar s2c = s2[i];
        utf32_tolower(&s2c, 1);
        if (s1c != s2c)
            return ((int64_t)s1c) - ((int64_t)s2c);
        i++;
    }
    if (s1len != s2len)
        return ((int64_t)s1len) - ((int64_t)s2len);
    return 0;
}


uint64_t h64strtoull(
        char const *str, char **end_ptr, int base
        ) {
    /// Parses a unsigned 64 bit integer, return resulting int or 0.
    /// Important: base must be >= 2 and <= 36,
    /// and *end_ptr is always set to NULL.
    if (end_ptr) *end_ptr = NULL;

    if (base < 2 || base > 36)
        return 0;

    uint64_t result = 0;
    while (*str) {
        uint64_t prev_result = result;
        result *= base;
        if (result < prev_result)  // overflow
            return UINT64_MAX;

        uint8_t digit = _parse_digit(*str);
        if (digit >= base)
            return result;

        result += digit;

        str += 1;
    }

    return result;
}

int64_t h64strtoll(
        char const *str, char **end_ptr, int base
        ) {
    /// Parses a signed 64 bit integer, returns resulting int or 0.
    /// Important: base must be >= 2 and <= 36,
    /// and *end_ptr is always set to NULL.
    if (end_ptr) *end_ptr = NULL;

    int64_t sresult;
    if (str[0] == '-') {
        uint64_t result = h64strtoull(
            str + 1, NULL, base
        );
        if (result > 0x8000000000000000ULL)
            return INT64_MAX;
        else if (result == 0x8000000000000000ULL)
            sresult = INT64_MIN;
        else
            sresult = -(int64_t)(result);
    } else {
        uint64_t result = h64strtoull(str, NULL, base);
        if(result >= 0x8000000000000000ULL)
            return INT64_MAX;
        sresult = (int64_t)(result);
    }
    return sresult;
}
