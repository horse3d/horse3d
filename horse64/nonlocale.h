
#ifndef HORSE64_NONLOCALE_H_
#define HORSE64_NONLOCALE_H_

#include <stdarg.h>
#include <stdint.h>

double h64atof(const char *s);

int64_t h64strtoll(const char *s, char **endptr, int base);

int64_t h64atoll(const char *s);

#endif  // HORSE64_NONLOCALE_H_
