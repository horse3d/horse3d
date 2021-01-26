// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_FILESYS32_H_
#define HORSE64_FILESYS32_H_

#include <stdint.h>

#include "widechar.h"

void filesys32_FreeFolderList(
    h64wchar **list, int64_t *listlen
);

enum {
    FS32_LISTFOLDERERR_SUCCESS = 0,
    FS32_LISTFOLDERERR_OUTOFMEMORY = -1,
    FS32_LISTFOLDERERR_NOPERMISSION = -2,
    FS32_LISTFOLDERERR_TARGETNOTDIRECTORY = -3,
    FS32_LISTFOLDERERR_OUTOFFDS = -4,
    FS32_LISTFOLDERERR_OTHERERROR = -5
};

int filesys32_ListFolder(
    const h64wchar *path, int64_t pathlen,
    h64wchar ***contents, int64_t **contentslen,
    int returnFullPath, int *error
);

h64wchar *filesys32_RemoveDoubleSlashes(
    const h64wchar *path, int64_t pathlen,
    int couldbewinpath, int64_t *out_len
);

h64wchar *filesys32_NormalizeEx(
    const h64wchar *path, int64_t pathlen,
    int couldbewinpath,
    int64_t *out_len
);

h64wchar *filesys32_Normalize(
    const h64wchar *path, int64_t pathlen,
    int64_t *out_len
);

h64wchar *filesys32_ToAbsolutePath(
    const h64wchar *path, int64_t pathlen,
    int64_t *out_len    
);

int filesys32_WinApiInsensitiveCompare(
    const h64wchar *path1_o, int64_t path1len_o,
    const h64wchar *path2_o, int64_t path2len_o,
    int *wasoom
);

int filesys32_PathCompare(
    const h64wchar *p1, int64_t p1len,
    const h64wchar *p2, int64_t p2len
);

h64wchar *filesys32_Join(
    const h64wchar *path1, int64_t path1len,
    const h64wchar *path2, int64_t path2len,
    int64_t *out_len
);

int filesys32_IsAbsolutePath(const h64wchar *path, int64_t pathlen);

h64wchar *filesys32_GetCurrentDirectory(int64_t *out_len);

#endif  // HORSE64_FILESYS32_H_