// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_NONLOCALE_H_
#define HORSE64_NONLOCALE_H_

#include "compileconfig.h"

#include <ctype.h>
#include <locale.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "widechar.h"

#if defined(_WIN32) || defined(_WIN64)
#define locale_t _locale_t
#include <windows.h>
extern HANDLE *h64stdout, *h64stderr;
#endif
extern locale_t h64locale;

#if !defined(_WIN32) || !defined(_WIN64)
// Work around glibc wanting _GNU_SOURCE to give us these:
extern double strtod_l(const char * __restrict, char **__restrict, locale_t);
#endif

ATTR_UNUSED static inline uint8_t _parse_digit(char c) {
    /// Parses a single digit,
    /// returns either digit value or 0xFF if invalid.

    if (c >= 'A' && c <= 'Z')
        return 10 + (uint8_t)(c - 'A');
    else if (c >= 'a' && c <= 'z')
        return 10 + (uint8_t)(c - 'a');
    else if (c >= '0' && c <= '9')
        return (uint8_t)(c - '0');
    else
        return 0xFF;
}

uint64_t h64strtoull(
    char const *str, char **end_ptr, int base
);

int64_t h64strtoll(
    char const *str, char **end_ptr, int base
);

ATTR_UNUSED static inline double h64atof(const char *s) {
    #if defined(_WIN32) || defined(_WIN64)
    return _strtod_l(s, NULL, h64locale);
    #else
    return strtod_l(s, NULL, h64locale);
    #endif
}

ATTR_UNUSED static inline long long int h64atoll(const char *s) {
    return h64strtoll(s, NULL, 10);
}

ATTR_UNUSED static inline int h64snprintf(
        char *buf,
        size_t size,
        const char *format, ...
        ) {
    va_list vl;
    va_start(vl, format);
    #if defined(_WIN32) || defined(_WIN64)
    return _vsnprintf_l(buf, size, format, h64locale, vl);
    #else
    #if (defined(__LINUX__) || defined(__linux__))
    locale_t old = uselocale(h64locale);
    int result = vsnprintf(buf, size, format, vl);
    uselocale(old);
    return result;
    #else
    return vsnprintf_l(buf, size, format, vl, h64locale);
    #endif
    #endif
}

ATTR_UNUSED static inline void _windows_ForceTerminalMode() {
    #if defined(_WIN32) || defined(_WIN64)
    // Attach to a terminal:
    if (!AttachConsole(-1)) {
        if (!AllocConsole()) {
            // nothing useful to do if this also fails
        }
    }

    // Update stderr/stdin so they use the new window:
    h64stdout = GetStdHandle(STD_OUTPUT_HANDLE);
    h64stderr = GetStdHandle(STD_ERROR_HANDLE);
    #endif
}

int _doprintfwindows(
    FILE *printfile, const char *format, va_list vl
);

ATTR_UNUSED static inline int _doprintfunix(
        ATTR_UNUSED FILE *printfile, ATTR_UNUSED const char *format,
        ATTR_UNUSED va_list vl
        ) {
    #if defined(_WIN32) || defined(_WIN64)
    return -1;
    #else
    #if defined(__LINUX__) || defined(__linux__)
    locale_t old = uselocale(h64locale);
    int result = vfprintf(printfile, format, vl);
    uselocale(old);
    return result;
    #else
    return vprintf_l(printfile, h64locale, format, vl);
    #endif
    #endif
}

ATTR_UNUSED static inline int h64printf(const char *format, ...) {
    va_list vl;
    va_start(vl, format);
    #if defined(_WIN32) || defined(_WIN64)
    int result = _doprintfwindows(stdout, format, vl);
    #else
    int result = _doprintfunix(stdout, format, vl);
    #endif
    va_end(vl);
    return result;
}

ATTR_UNUSED static inline int h64fprintf(
        FILE *tgfd, const char *format, ...
        ) {
    va_list vl;
    va_start(vl, format);
    #if defined(_WIN32) || defined(_WIN64)
    int result = _doprintfwindows(tgfd, format, vl);
    #else
    int result = _doprintfunix(tgfd, format, vl);
    #endif
    va_end(vl);
    return result;
}

ATTR_UNUSED static inline int h64casecmp(
        const char *s1, const char *s2
        ) {
    while (1) {
        if (*s1 != *s2) {
            uint8_t c1 = *(uint8_t*)s1;
            if (c1 >= 'a' && c1 <= 'z')
                c1 = (c1 - 'a') + 'A';
            uint8_t c2 = *(uint8_t*)s2;
            if (c2 >= 'a' && c2 <= 'z')
                c2 = (c2 - 'a') + 'A';
            if (c1 != c2)
                return ((int)c1) - ((int)c2);
        } else if (unlikely(*s1 == '\0')) {
            return 0;
        }
        s1++;
        s2++;
    }
}

int64_t h64casecmp_u32(
    const h64wchar *s1, int64_t s1len,
    const h64wchar *s2, int64_t s2len
);

#endif  // HORSE64_NONLOCALE_H_
