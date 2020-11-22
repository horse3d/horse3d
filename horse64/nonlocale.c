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
            fprintf(stderr, "failed to generate locale\n");
            _exit(1);
        }
        h64localeset = 1;
    }
}
