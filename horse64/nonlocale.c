
#define WINVER 0x0600

#include <locale.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "horse64/nonlocale.h"

static int h64localeset = 0;
locale_t h64locale = (locale_t) 0;

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
            fprintf(stderr, "failed to generate locale\n");
            _exit(1);
        }
        h64localeset = 1;
    }
}
