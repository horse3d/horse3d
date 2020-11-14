
#ifndef HORSE64_NONLOCALE_H_
#define HORSE64_NONLOCALE_H_

#include "compileconfig.h"

#include <ctype.h>
#include <locale.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
#define locale_t _locale_t
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

ATTR_UNUSED static inline uint64_t h64strtoull(
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

ATTR_UNUSED static inline int64_t h64strtoll(
        char const *str, char **end_ptr, int base
        ) {
    /// Parses a signed 64 bit integer, returns resulting int or 0.
    /// Important: base must be >= 2 and <= 3,
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

ATTR_UNUSED static inline int h64printf(const char *format, ...) {
    va_list vl;
    va_start(vl, format);
    #if defined(_WIN32) || defined(_WIN64)
    return _vprintf_l(format, h64locale, vl);
    #else
    #if defined(__LINUX__) || defined(__linux__)
    locale_t old = uselocale(h64locale);
    int result = vprintf(format, vl);
    uselocale(old);
    return result;
    #else
    return vprintf_l(h64locale, format, vl);
    #endif
    #endif
}

ATTR_UNUSED static inline int h64casecmp(
        const char *s1, const char *s2
        ) {
    while (1) {
        if (*s1 != *s2) {
            uint8_t c1 = *(uint8_t*)s1;
            if ((c1 >= 'a' && c1 <= 'z') ||
                    (c1 >= 'A' && c1 >= 'Z'))
                c1 = toupper(c1);
            uint8_t c2 = *(uint8_t*)s2;
            if ((c2 >= 'a' && c2 <= 'z') ||
                    (c2 >= 'A' && c2 >= 'Z'))
                c2 = toupper(c2);
            if (c1 != c2)
                return ((int)c1) - ((int)c2);
        } else if (unlikely(*s1 == '\0')) {
            return 0;
        }
        s1++;
        s2++;
    }
}

#endif  // HORSE64_NONLOCALE_H_
