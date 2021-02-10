// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
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

int _doprintfwindows(
        ATTR_UNUSED FILE *printfile, ATTR_UNUSED const char *format,
        ATTR_UNUSED va_list vl
        ) {
    #if defined(_WIN32) || defined(_WIN64)
    if (!h64stdout)
        h64stdout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!h64stderr)
        h64stderr = GetStdHandle(STD_ERROR_HANDLE);
    char _stackbuf[256] = "";
    char *buf = _stackbuf;
    int bufheap = 0;
    size_t buflen = 256;
    while (1) {
        va_list vcopy;
        va_copy(vcopy, vl);
        int result = _vsnprintf_l(
            buf, buflen - 1, format, h64locale, vcopy
        );
        va_end(vcopy);
        buf[buflen - 1] = '\0';
        if (result >= 0 && strlen(buf) >= buflen - 1) {
            buflen *= 2;
            char *bufnew = malloc(buflen);
            if (!bufnew) {
                errorquit:
                if (bufheap)
                    free(buf);
                return -1;
            }
            if (bufheap)
                free(buf);
            buf = bufnew;
            bufheap = 1;
        } else if (result < 0) {
            goto errorquit;
        } else {
            break;
        }
    }
    uint32_t written = 0;
    if (printfile == stdout || printfile == stderr) {
        if (!WriteConsole(
                (printfile == stdout ?
                h64stdout : h64stderr),
                buf, strlen(buf), (LPDWORD)&written, NULL
                )) {
            if (bufheap)
                free(buf);
            return -1;
        }
    } else {
        int result = -1;
        if ((result = fprintf(printfile, "%s", buf)) < 0) {
            if (bufheap)
                free(buf);
        }
        written = result;
    }
    if (bufheap)
        free(buf);
    if (written < strlen(buf))
        return written;
    return strlen(buf);
    #else
    return -1;
    #endif
}

int64_t h64cmp_u32u8(
        const h64wchar *s1, int64_t s1len,
        const char *s2
        ) {
    const int64_t s2len = strlen(s2);
    int64_t i1 = 0;
    int64_t i2 = 0;
    while (i1 < s1len && i2 < s2len) {
        h64wchar s1char = s1[i1];
        int s2charlen = utf8_char_len((const uint8_t *)(s2 + i2));
        if (s2charlen < 1)
            s2charlen = 1;
        h64wchar s2char = 0;
        int _cpbyteslen = 0;
        if (!get_utf8_codepoint(
                (const uint8_t *)(s2 + i2), s2charlen, &s2char,
                &_cpbyteslen
                ))
            s2char = s2[i2];
        if (s1char != s2char)
            return (s1char - s2char);
        i1++;
        i2 += s2charlen;
    }
    if (i1 >= s1len && i2 < s2len) {
        return -1;
    } else if (i1 < s1len && i2 >= s2len) {
        return 1;
    }
    return 0;
}

int64_t h64casecmp_u32u8(
        const h64wchar *s1, int64_t s1len,
        const char *s2
        ) {
    const int64_t s2len = strlen(s2);
    int64_t i1 = 0;
    int64_t i2 = 0;
    while (i1 < s1len && i2 < s2len) {
        h64wchar s1char = s1[i1];
        int s2charlen = utf8_char_len((const uint8_t *)(s2 + i2));
        if (s2charlen < 1)
            s2charlen = 1;
        h64wchar s2char = 0;
        int _cpbyteslen = 0;
        if (!get_utf8_codepoint(
                (const uint8_t *)(s2 + i2), s2charlen, &s2char,
                &_cpbyteslen
                ))
            s2char = s2[i2];
        utf32_tolower(&s1char, 1);
        utf32_tolower(&s2char, 1);
        if (s1char != s2char)
            return (s1char - s2char);
        i1++;
        i2 += s2charlen;
    }
    if (i1 >= s1len && i2 < s2len) {
        return -1;
    } else if (i1 < s1len && i2 >= s2len) {
        return 1;
    }
    return 0;
}