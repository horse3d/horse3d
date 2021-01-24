// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#define _FILE_OFFSET_BITS 64
#ifndef __USE_LARGEFILE64
#define __USE_LARGEFILE64 1
#endif
#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif
#define _LARGEFILE_SOURCE

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif

#include "filesys.h"
#include "filesys32.h"
#include "widechar.h"


h64wchar *filesys32_RemoveDoubleSlashes(
        const h64wchar *path, int64_t pathlen,
        int couldbewinpath, int64_t *out_len
        ) {
    // MAINTENANCE NOTE: KEEP IN SYNC WITH filesys_RemoveDoubleSlashes()!!!

    if (!path)
        return NULL;
    if (pathlen <= 0) {
        if (out_len) *out_len = 0;
        return malloc(1);
    }
    h64wchar *p = malloc(sizeof(*path) * pathlen);
    if (!p)
        return NULL;
    memcpy(p, path, sizeof(*p) * pathlen);
    int64_t plen = pathlen;

    // Remove double slashes:
    int lastwassep = 0;
    int64_t i = 0;
    while (i < plen) {
        if (p[i] == '/'
                || (couldbewinpath && p[i] == '\\')
                ) {
            #if defined(_WIN32) || defined(_WIN64)
            p[i] = '\\';
            #else
            p[i] = '/';
            #endif
            if (!lastwassep) {
                lastwassep = 1;
            } else {
                memmove(
                    p + i, p + i + 1,
                    (plen - i - 1) * sizeof(*path)
                );
                plen--;
                continue;
            }
        } else {
            lastwassep = 0;
        }
        i++;
    }
    if (plen > 1 && (
            p[plen - 1] == '/'
            || (couldbewinpath && p[plen - 1] == '\\')
            )) {
        p[plen - 1] = '\0';
    }

    if (out_len) *out_len = plen;
    return p;
}

h64wchar *filesys32_NormalizeEx(
        const h64wchar *path, int64_t pathlen, int couldbewinpath,
        int64_t *out_len
        ) {
    // MAINTENANCE NOTE: KEEP THIS IN SINC WITH filesys_Normalize()!!!

    if (couldbewinpath == -1) {
        #if defined(_WIN32) || defined(_WIN64)
        couldbewinpath = 1;
        #else
        couldbewinpath = 0;
        #endif
    }

    int64_t resultlen = 0;
    h64wchar *result = filesys32_RemoveDoubleSlashes(
        path, pathlen, couldbewinpath, &resultlen
    );
    if (!result)
        return NULL;

    // Remove all unnecessary ../ and ./ inside the path:
    int last_component_start = -1;
    int64_t i = 0;
    while (i < resultlen) {
        if ((result[i] == '/'
                || (couldbewinpath && result[i] == '\\')
                ) && i + 2 < resultlen &&
                result[i + 1] == '.' &&
                result[i + 2] == '.' && (
                i + 3 >= resultlen ||
                result[i + 3] == '/' ||
                (couldbewinpath && result[i + 3] == '\\')
                ) && i > last_component_start && i > 0 &&
                (result[last_component_start + 1] != '.' ||
                 result[last_component_start + 2] != '.' ||
                 (result[last_component_start + 3] != '/' &&
                  (!couldbewinpath ||
                   result[last_component_start + 3] != '\\')
                 )
                )) {
            // Collapse ../ into previous component:
            int movelen = 4;
            if (i + 3 >= resultlen)
                movelen = 3;
            memmove(
                result + last_component_start + 1,
                result + (i + movelen),
                sizeof(*result) * (resultlen - (i + movelen))
            );
            resultlen -= movelen;
            // Start over from beginning:
            i = 0;
            last_component_start = 0;
            continue;
        } else if ((result[i] == '/' ||
                (couldbewinpath && result[i] == '\\')
                ) && result[i + 1] == '.' && (
                result[i + 2] == '/' ||
                (couldbewinpath && result[i + 2] == '\\')
                )) {
            // Collapse unncessary ./ away:
            last_component_start = i;
            memmove(
                result + i, result + (i + 2),
                sizeof(*result) * (resultlen - (i - 2))
            );
            resultlen -= 2;
            continue;
        } else if (result[i] == '/'
                || (couldbewinpath && result[i] == '\\')
                ) {
            last_component_start = i;
            // Collapse all double slashes away:
            while (result[i + 1] == '/' ||
                    (couldbewinpath && result[i + 1] == '\\')
                    ) {
                memmove(
                    result + i, result + (i + 1),
                    sizeof(*result) * (resultlen - (i - 1))
                );
                resultlen--;
            }
        }
        i++;
    }

    // Remove leading ./ instances:
    while (resultlen >= 2 && result[0] == '.' && (
            result[1] == '/' ||
            (couldbewinpath && result[1] == '\\')
            )) {
        memmove(
            result, result + 2,
            sizeof(*result) * (resultlen - 2)
        );
        resultlen -= 2;
    }

    // Unify path separators:
    i = 0;
    while (i < resultlen) {
        if (result[i] == '/' ||
                (couldbewinpath && result[i] == '\\')
                ) {
            #if defined(_WIN32) || defined(_WIN64)
            result[i] = '\\';
            #else
            result[i] = '/';
            #endif
        }
        i++;
    }

    // Remove trailing path separators:
    while (resultlen > 0) {
        if (result[resultlen - 1] == '/' ||
                result[resultlen - 1] == '\\'
                ) {
            resultlen--;
        } else {
            break;
        }
    }
    if (out_len) *out_len = resultlen;
    return result;
}

h64wchar *filesys32_Normalize(
        const h64wchar *path, int64_t pathlen,
        int64_t *out_len
        ) {
    return filesys32_NormalizeEx(
        path, pathlen, -1, out_len
    );
}

h64wchar *filesys32_ToAbsolutePath(
        const h64wchar *path, int64_t pathlen,
        int64_t *out_len    
        ) {
    if (filesys32_IsAbsolutePath(path, pathlen)) {
        h64wchar *result = malloc(
            (pathlen > 0 ? pathlen : 1) * sizeof(*path)
        );
        if (result) {
            memcpy(result, path, sizeof(*path) * pathlen);
        }
        return result;
    }
    int64_t cwdlen = 0;
    h64wchar *cwd = filesys32_GetCurrentDirectory(&cwdlen);
    if (!cwd)
        return NULL;
    int64_t resultlen = 0;
    h64wchar *result = filesys32_Join(
        cwd, cwdlen, path, pathlen, &resultlen
    );
    free(cwd);
    if (out_len) *out_len = resultlen;
    return result;
}

int filesys32_AssumeCaseSensitiveHostFS() {
    #if defined(_WIN32) || defined(_WIN64)
    return 1;
    #else
    #if defined(__APPLE__)
    return 1;
    #endif
    #endif
    return 0;
}

int filesys32_WinApiInsensitiveCompare(
        const h64wchar *path1_o, int64_t path1len_o,
        const h64wchar *path2_o, int64_t path2len_o,
        int *wasoom
        ) {
    #if defined(_WIN32) || defined(_WIN64)
    uint16_t *path1 = NULL;
    uint16_t *path2 = NULL;
    path1 = malloc(
        sizeof(*path1) * (path1len_o * 2 + 1)
    );
    if (!path1) {
        oom:
        free(path1);
        free(path2);
        if (wasoom) *wasoom = 1;
        return 0;
    }
    path2 = malloc(
        sizeof(*path2) * (path2len_o * 2 + 1)
    );
    assert(
        sizeof(*path1) == sizeof(wchar_t)
        // should be true for windows
    );
    if (!path2)
        goto oom;
    int64_t path2len = 0;
    int64_t path1len = 0;
    int result1 = utf32_to_utf16(
        path1_o, path1len_o, (char *)path1,
        path1len_o * 2 * sizeof(*path1),
        &path1len, 1
    );
    int result2 = utf32_to_utf16(
        path2_o, path2len_o, (char *)path2,
        path2len_o * 2 * sizeof(*path2),
        &path2len, 1
    );
    if (!result1 || !result2) {
        // This shouldn't happen. But we'd rather not crash here.
        goto oom;
    }
    if (path1len != path2len) {
        free(path1);
        free(path2);
        if (wasoom) *wasoom = 0;
        return 0;
    }
    path1[path1len] = '\0';
    path2[path2len] = '\0';
    CharUpperW((wchar_t *)path1);  // winapi case folding.
    CharUpperW((wchar_t *)path2);
    if (memcmp(path1, path2, path1len * sizeof(*path1)) == 0) {
        free(path1);
        free(path2);
        if (wasoom) *wasoom = 0;
        return 1;
    }
    free(path1);
    free(path2);
    if (wasoom) *wasoom = 0;
    return 0;
    #else
    if (wasoom) *wasoom = 1;
    return 0;
    #endif
}

int filesys32_PathCompare(
        const h64wchar *p1, int64_t p1len,
        const h64wchar *p2, int64_t p2len
        ) {
    int64_t p1normalizedlen = 0;
    h64wchar *p1normalized = filesys32_Normalize(
        p1, p1len, &p1normalizedlen
    );
    int64_t p2normalizedlen = 0;
    h64wchar *p2normalized = filesys32_Normalize(
        p2, p2len, &p2normalizedlen
    );
    if (!p1normalized || !p2normalized) {
        free(p1normalized);
        free(p2normalized);
        return -1;
    }
    int result = 0;
    #if defined(_WIN32) || defined(_WIN64)
    if (filesys32_AssumeCaseSensitiveHostFS()) {
        int wasoom = 0;
        result = (filesys32_WinApiInsensitiveCompare(
            p1normalized, p1normalizedlen,
            p2normalized, p2normalizedlen, &wasoom
        ));
        free(p1normalized);
        free(p2normalized);
        if (!result && wasoom)
            return -1;
        return result;
    }
    #else
    result = (memcmp(
        p1normalized, p2normalized,
        p1normalizedlen * sizeof(*p1normalized)
    ) == 0);
    free(p1normalized);
    free(p2normalized);
    #endif
    return result;
}

h64wchar *filesys32_GetCurrentDirectory(int64_t *out_len) {
    #if defined(_WIN32) || defined(_WIN64)
    assert(sizeof(wchar_t) == sizeof(uint16_t));  // winapi specific
    DWORD size = GetCurrentDirectoryW(0, NULL);
    uint16_t *s = malloc(size * sizeof(*s) + 1);
    if (!s)
        return NULL;
    if (GetCurrentDirectoryW(size, (wchar_t *)s) != 0) {
        s[size - 1] = '\0';
    } else {
        free(s);
        return NULL;
    }
    int hadoom = 0;
    int64_t resultlen = 0;
    h64wchar *result = utf16_to_utf32(
        s, size, &resultlen, 1, &hadoom
    );
    free(s);
    if (!result)
        return NULL;
    if (out_len) *out_len = resultlen;
    return result;
    #else
    char *cwd = filesys_GetCurrentDirectory();
    if (!cwd)
        return NULL;
    int wasinvalid = 0;
    int wasoom = 0;
    int64_t resultlen = 0;
    h64wchar *result = utf8_to_utf32_ex(
        cwd, strlen(cwd), NULL, 0, NULL, NULL, &resultlen,
        1, 0, &wasinvalid, &wasoom
    );
    free(cwd);
    if (result)
        if (out_len) *out_len = resultlen;
    return result;
    #endif
}

h64wchar *filesys32_Join(
        const h64wchar *path1, int64_t path1len,
        const h64wchar *path2_orig, int64_t path2_origlen,
        int64_t *out_len
        ) {
    // Quick result paths:
    if (!path1 || !path2_orig)
        return NULL;
    if ((path2_origlen == 1 && path2_orig[0] == '.') ||
            path2_origlen == 0) {
        returnfirst: ;
        h64wchar *result = malloc(
            (path1len > 0 ? path1len : 1) * sizeof(*path1)
        );
        if (result) {
            memcpy(result, path1, sizeof(*path1) * path1len);
            if (out_len)
                *out_len = path1len;
        }
        return result;
    }

    // Clean up path2 for merging:
    int64_t path2len = path2_origlen;
    h64wchar *path2 = malloc(
        sizeof(*path2_orig) * (
            path2_origlen > 0 ? path2_origlen : 1
        )
    );
    if (!path2)
        return NULL;
    memcpy(path2, path2_orig, sizeof(*path2_orig) * path2_origlen);
    while (path2len >= 2 && path2[0] == '.' &&
            (path2[1] == '/'
            #if defined(_WIN32) || defined(_WIN64)
            || path2[1] == '\\'
            #endif
            )) {
        memmove(
            path2, path2 + 2,
            (path2len - 2) * sizeof(*path2)
        );
        if (path2len == 0 || (path2len == 1 && path2[0] == '.')) {
            free(path2);
            goto returnfirst;
        }
    }

    // Do actual merging:
    h64wchar *presult = malloc(
        (path1len + 1 + path2len) * sizeof(*path1)
    );
    int64_t presultlen = 0;
    if (!presult) {
        free(path2);
        return NULL;
    }
    if (path1len > 0)
        memcpy(
            presult, path1, path1len * sizeof(*path1)
        );
    presultlen = path1len;
    if (path1len > 0) {
        #if defined(_WIN32) || defined(_WIN64)
        if (path1[path1len - 1] != '\\' &&
                path1[path1len - 1] != '/' &&
                (path2len == 0 || path2[0] != '\\' ||
                 path2[0] != '/')) {
            presult[presultlen] = '\\';
            presultlen++;
        }
        #else
        if ((path1[path1len - 1] != '/') &&
                (path2len == 0 || path2[0] != '/')) {
            presult[presultlen] = '/';
            presultlen++;
        }
        #endif
        memcpy(
            presult + presultlen, path2,
            sizeof(*path2) * path2len
        );
        presultlen += path2len;
    } else {
        #if defined(_WIN32) || defined(_WIN64)
        if (path2len > 0 && (
                path2[0] == '/' ||
                path2[0] == '\\')) {
            memcpy(
                presult + presultlen,
                path2 + 1, sizeof(*path2) * (path2len - 1)
            );
            presultlen += (path2len - 1);
        } else {
            memcpy(
                presult + presultlen,
                path2, sizeof(*path2) * path2len
            );
            presultlen += path2len;
        }
        #else
        if (path2len > 0 && path2[0] == '/') {
            memcpy(
                presult + presultlen,
                path2 + 1, sizeof(*path2) * (path2len - 1)
            );
            presultlen += (path2len - 1);
        } else if (path2len > 0) {
            memcpy(
                presult + presultlen,
                path2, sizeof(*path2) * path2len
            );
            presultlen += path2len;
        }
        #endif
    }
    free(path2);  // this was a mutable copy of ours
    if (out_len) *out_len = presultlen;
    return presult;
}

int filesys32_IsAbsolutePath(
        const h64wchar *path, int64_t pathlen
        ) {
    if (pathlen == 0)
        return 0;
    if (path[0] == '.')
        return 0;
    #if (!defined(_WIN32) && !defined(_WIN64))
    if (path[0] == '/')
        return 1;
    #endif
    #if defined(_WIN32) || defined(_WIN64)
    if (pathlen > 2 && (
            path[1] == ':' || path[1] == '\\'))
        return 1;
    #endif
    return 0;
}