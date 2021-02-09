// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_FILESYS32_H_
#define HORSE64_FILESYS32_H_

#include "compileconfig.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif

#include "widechar.h"


#if defined(_WIN32) || defined(_WIN64)
#define h64filehandle HANDLE
#define H64_NOFILE (INVALID_HANDLE_VALUE)
#else
typedef int h64filehandle;
#define H64_NOFILE ((int)0)
#endif

h64wchar *filesys32_GetOwnExecutable(int64_t *out_len);

int filesys32_IsObviouslyInvalidPath(const h64wchar *p, int64_t plen);

int filesys32_TargetExists(
    const h64wchar *path, int64_t pathlen, int *result
);

int filesys32_IsDirectory(
    const h64wchar *path, int64_t pathlen, int *result
);

int filesys32_SetExecutable(
    const h64wchar *path, int64_t pathlen, int *err
);

int filesys32_SetOctalPermissions(
    const h64wchar *path, int64_t pathlen, int *err,
    int permission_extra, int permission_user,
    int permission_group, int permission_any
);

int filesys32_GetOctalPermissions(
    const h64wchar *path, int64_t pathlen, int *err,
    int *permission_extra, int *permission_user,
    int *permission_group, int *permission_any
);

void filesys32_FreeFolderList(
    h64wchar **list, int64_t *listlen
);

enum {
    FS32_ERR_SUCCESS = 0,
    FS32_ERR_NOPERMISSION = -1,
    FS32_ERR_TARGETNOTADIRECTORY = -2,
    FS32_ERR_TARGETNOTAFILE = -3,
    FS32_ERR_NOSUCHTARGET = -4,
    FS32_ERR_OUTOFMEMORY = -5,
    FS32_ERR_TARGETALREADYEXISTS = -6,
    FS32_ERR_INVALIDNAME = -7,
    FS32_ERR_OUTOFFDS = -8,
    FS32_ERR_PARENTSDONTEXIST = -9,
    FS32_ERR_DIRISBUSY = -10,
    FS32_ERR_NONEMPTYDIRECTORY = -11,
    FS32_ERR_SYMLINKSWEREEXCLUDED = -12,
    FS32_ERR_IOERROR = -13,
    FS32_ERR_UNSUPPORTEDPLATFORM = -14,
    FS32_ERR_OTHERERROR = -15
};

int filesys32_ChangeDirectory(
    h64wchar *path, int64_t pathlen
);

int filesys32_CreateDirectory(
    h64wchar *path, int64_t pathlen, int user_readable_only
);

int filesys32_CreateDirectoryRecursively(
    h64wchar *path, int64_t pathlen, int user_readable_only
);

int filesys32_RemoveFolderRecursively(
    const h64wchar *path, int64_t pathlen, int *error
);


int filesys32_RemoveFileOrEmptyDir(
    const h64wchar *path, int64_t pathlen, int *error
);

int filesys32_ListFolderEx(
    const h64wchar *path, int64_t pathlen,
    h64wchar ***contents, int64_t **contentslen,
    int returnFullPath, int allowsymlink, int *error
);

char *filesys23_ContentsAsStr(
    const h64wchar *path, int64_t pathlen, int *error
);

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

int filesys32_GetSize(
    const h64wchar *path, int64_t pathlen, uint64_t *size,
    int *err
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

h64wchar *filesys32_Basename(
    const h64wchar *path, int64_t pathlen, int64_t *out_len
);

h64wchar *filesys32_Dirname(
    const h64wchar *path, int64_t pathlen, int64_t *out_len
);

h64wchar *filesys32_GetCurrentDirectory(int64_t *out_len);

h64filehandle filesys32_OpenFromPathAsOSHandle(
    const h64wchar *pathu32, int64_t pathu32len,
    const char *mode, int *err
);

enum {
    _WIN32_OPEN_LINK_ITSELF = 0x1,
    _WIN32_OPEN_DIR = 0x2,
    OPEN_ONLY_IF_NOT_LINK = 0x4
};

h64filehandle filesys32_OpenFromPathAsOSHandleEx(
    const h64wchar *pathu32, int64_t pathu32len,
    const char *mode, int flags, int *err
);

FILE *filesys32_OpenFromPath(
    const h64wchar *path, int64_t pathlen,
    const char *mode, int *err
);

FILE *filesys32_OpenOwnExecutable();

h64wchar *filesys32_GetSysTempdir(int64_t *output_len);

FILE *filesys32_TempFile(
    int subfolder, int folderonly,
    const h64wchar *prefix, int64_t prefixlen,
    const h64wchar *suffix, int64_t suffixlen,
    h64wchar **folder_path, int64_t* folder_path_len,
    h64wchar **path, int64_t *path_len
);

h64wchar *filesys32_TurnIntoPathRelativeTo(
    const h64wchar *path, int64_t pathlen,
    const h64wchar *makerelativetopath, int64_t makerelativetopathlen,
    int64_t *out_len
);

int filesys32_FolderContainsPath(
    const h64wchar *folder_path, int64_t folder_path_len,
    const h64wchar *check_path, int64_t check_path_len,
    int *result
);

int filesys32_GetComponentCount(
    const h64wchar *path, int64_t pathlen
);

h64wchar *filesys32_ParentdirOfItem(
    const h64wchar *path, int64_t pathlen, int64_t *out_len
);

int filesys32_IsSymlink(
    h64wchar *pathu32, int64_t pathu32len, int *err, int *result
);

ATTR_UNUSED static FILE *_dupfhandle(FILE *f, const char* mode) {
    int fd = -1;
    int fd2 = -1;
    #if defined(_WIN32) || defined(_WIN64)
    fd = _fileno(f);
    fd2 = _dup(fd);
    #else
    fd = fileno(f);
    fd2 = dup(fd);
    #endif
    if (fd2 < 0)
        return NULL;
    FILE *f2 = fdopen(fd2, mode);
    if (!f2) {
        #if defined(_WIN32) || defined(_WIN64)
        _close(fd2);
        #else
        close(fd2);
        #endif
        return NULL;
    }
    return f2;
}

#endif  // HORSE64_FILESYS32_H_