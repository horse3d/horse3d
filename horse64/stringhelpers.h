
#ifndef HORSE64_STRINGS_H_
#define HORSE64_STRINGS_H_

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
static wchar_t *unicodestr(const char *s) {
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

#endif
