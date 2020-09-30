// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_STRINGHELPERS_H_
#define HORSE64_STRINGHELPERS_H_

// FIXME: abandon this header for our own widechar handling, eventually

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
static wchar_t *widecharstr(const char *s) {
    int size = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *result = malloc(
        sizeof(*result) * (size + 1)
    );
    if (!result)
        return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, result, size);
    result[size] = '\0';
    return result;
}
#endif

#endif  // HORSE64_STRINGHELPERS_H_
