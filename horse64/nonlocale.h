
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

static inline long long int h64strtoll(const char *s, char **endptr, int base) {
    #if defined(_WIN32) || defined(_WIN64)
    return _strtoll_l(s, endptr, base, h64locale);
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
    return _strtoll_l(s, NULL, 10, h64locale);
    #else
    return strtoll_l(s, NULL, 10, h64locale);
    #endif
}

#endif  // HORSE64_NONLOCALE_H_
