
#ifndef HORSE64_NONLOCALE_H_
#define HORSE64_NONLOCALE_H_

#define WINVER 0x0600

#include <locale.h>

#if defined(_WIN32) || defined(_WIN64)
#define locale_t _locale_t
#endif
extern locale_t h64locale;

#if !defined(_WIN32) || !defined(_WIN64)
// Work around glibc wanting _GNU_SOURCE to give us these:
extern float strtof_l(const char * __restrict, char **__restrict, locale_t);
extern long long int strtoll_l(const char * __restrict, char **__restrict, int, locale_t);
extern double strtod_l(const char * __restrict, char **__restrict, locale_t);
#endif

//! Parses a single digit, returns either digit value or 0xFF if invalid
static uint8_t parse_digit(char c)
{
    if(c >= 'A' && c <= 'Z')
        return 10 + (uint8_t)(c - 'A');
    else if(c >= 'a' && c <= 'z')
        return 10 + (uint8_t)(c - 'a');
    else if(c >= '0' && c <= '9')
        return      (uint8_t)(c - '0');
    else
        return 0xFF;
}

//! Parses a unsigned 64 bit integer, returns `true` on success, else `false`.
//! `out_result` must be non-NULL, `base` >= 2 and <= 36
bool parse_uint64(char const * str, size_t len, uint8_t base, uint64_t * out_result)
{
    if(len == 0)
        return false;
    if(base < 2 || base > 36)
        return false;
    
    uint64_t result = 0;
    while(len > 0)
    {
        result *= base; // warning: can silently overflow!

        uint8_t digit = parse_digit(*str);
        if(digit >= base)
            return false;

        result += digit;

        len -= 1;
        str += 1;
    }

    (*out_result) = result;

    return true;
}

//! Parses a signed 64 bit integer, returns `true` on success, else `false`.
//! `out_result` must be non-NULL, `base` >= 2 and <= 36
bool parse_int64(char const * str, size_t len, uint8_t base, int64_t * out_result)
{
    int64_t sresult;
    if(len > 0 && str[0] == '-') {
        uint64_t result;
        bool success = parse_uint64(str + 1, len - 1, base, &result);
        if(!success)
            return false;
        if(result > 0x8000000000000000ULL)
            return false;
        else if(result == 0x8000000000000000ULL)
            sresult = -0x7FFFFFFFFFFFFFFFLL - 1;
        else
            sresult = -(int64_t)(result);
    }
    else {
        uint64_t result;
        bool success = parse_uint64(str, len, base, &result);
        if(!success)
            return false;
        if(result >= 0x8000000000000000ULL)
            return false;
        sresult = (int64_t)(result);
    }
    *out_result = sresult;
    return true;
}

static inline long long int h64strtoll(const char *s, char **endptr, int base) {
    #if defined(_WIN32) || defined(_WIN64)
    // FIXME: MinGW currently lacks _strtoll_l
    setlocale(LC_ALL, "C");
    return strtoll(s, endptr, base);
    #else
    return strtoll_l(s, endptr, base, h64locale);
    #endif
}

static inline double h64atof(const char *s) {
    #if defined(_WIN32) || defined(_WIN64)
    return _strtod_l(s, NULL, h64locale);
    #else
    return strtod_l(s, NULL, h64locale);
    #endif
}

static inline long long int h64atoll(const char *s) {
    #if defined(_WIN32) || defined(_WIN64)
    // FIXME: MinGW currently lacks _strtoll_l
    setlocale(LC_ALL, "C");
    return strtoll(s, NULL, 10);
    #else
    return strtoll_l(s, NULL, 10, h64locale);
    #endif
}

#endif  // HORSE64_NONLOCALE_H_
