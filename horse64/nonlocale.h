
#ifndef HORSE64_NONLOCALE_H_
#define HORSE64_NONLOCALE_H_

#include <locale.h>
#include <stdint.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
#define locale_t _locale_t
#endif
extern locale_t h64locale;

#if !defined(_WIN32) || !defined(_WIN64)
// Work around glibc wanting _GNU_SOURCE to give us these:
extern double strtod_l(const char * __restrict, char **__restrict, locale_t);
#endif

static inline uint8_t _parse_digit(char c) {
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

static uint64_t h64strtoull(
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

static int64_t h64strtoll(
        char const *str, char **end_ptr, int base
        ) {
    /// Parses a signed 64 bit integer, returns resulting int or 0.
    /// Important: base must be >= 2 and <= 3,
    /// and *end_ptr is always set to NULL.
    if (end_ptr) *end_ptr = NULL;

    size_t len = strlen(str);
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

static inline double h64atof(const char *s) {
    #if defined(_WIN32) || defined(_WIN64)
    return _strtod_l(s, NULL, h64locale);
    #else
    return strtod_l(s, NULL, h64locale);
    #endif
}

static inline long long int h64atoll(const char *s) {
    return h64strtoll(s, NULL, 10); 
}

#endif  // HORSE64_NONLOCALE_H_
