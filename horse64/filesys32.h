// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_FILESYS32_H_
#define HORSE64_FILESYS32_H_

#include "widechar.h"



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