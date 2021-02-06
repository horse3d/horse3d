// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_VFS_H_
#define HORSE64_VFS_H_

#include "compileconfig.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "widechar.h"


#define VFSFLAG_NO_REALDISK_ACCESS 1
#define VFSFLAG_NO_VIRTUALPAK_ACCESS 2


int vfs_Exists(const char *path, int *result, int flags);

int vfs_ExistsU32(
    const h64wchar *path, int64_t pathlen,
    int *result, int flags
);

int vfs_ExistsEx(
    const char *abspath, const char *relpath, int *result, int flags
);

int vfs_ExistsExU32(
    const h64wchar *abspath, int64_t abspathlen,
    const h64wchar *relpath, int64_t relpathlen,
    int *result, int flags
);

int vfs_IsDirectory(const char *path, int *result, int flags);

int vfs_IsDirectoryEx(
    const char *abspath, const char *relpath, int *result, int flags
);


int vfs_IsDirectoryU32(
    const h64wchar *path, int64_t pathlen,
    int *result, int flags
);

int vfs_IsDirectoryExU32(
    const h64wchar *abspath, int64_t abspathlen,
    const h64wchar *relpath, int64_t relpathlen,
    int *result, int flags
);

int vfs_Size(const char *path, uint64_t *result, int flags);

int vfs_SizeEx(
    const char *abspath, const char *relpath, uint64_t *result, int flags
);

int vfs_SizeU32(
    const h64wchar *path, int64_t pathlen,
    uint64_t *result, int flags
);

int vfs_SizeExU32(
    const h64wchar *abspath, int64_t abspathlen,
    const h64wchar *relpath, int64_t relpathlen,
    uint64_t *result, int flags
);

char *vfs_NormalizePath(const char *path);

int vfs_GetBytes(
    const char *path, uint64_t offset,
    uint64_t bytesamount, char *buffer,
    int flags
);

int vfs_GetBytesEx(
    const char *abspath, const char *relpath,
    uint64_t offset,
    uint64_t bytesamount, char *buffer,
    int flags
);

int vfs_GetBytesU32(
    const h64wchar *path, int64_t pathlen, uint64_t offset,
    uint64_t bytesamount, char *buffer,
    int flags
);

int vfs_GetBytesExU32(
    const h64wchar *abspath, int64_t abspathlen,
    const h64wchar *relpath, int64_t relpathlen,
    uint64_t offset,
    uint64_t bytesamount, char *buffer,
    int flags
);

void vfs_Init();

typedef struct VFSFILE VFSFILE;

VFSFILE *vfs_fopen_u32(
    const h64wchar *path, int64_t pathlen,
    const char *mode, int flags
);

VFSFILE *vfs_fopen(const char *path, const char *mode, int flags);

VFSFILE *vfs_ownThisFD(FILE *f, const char *reopenmode);

void vfs_DetachFD(VFSFILE *f);

int vfs_feof(VFSFILE *f);

size_t vfs_fread(
    char *buffer, int bytes, int numn, VFSFILE *f
);

size_t vfs_freadline(VFSFILE *f, char *buf, size_t bufsize);

int vfs_fwritable(VFSFILE *f);

int64_t vfs_ftell(VFSFILE *f);

void vfs_fclose(VFSFILE *f);

int vfs_fseek(VFSFILE *f, uint64_t offset);

int vfs_fseektoend(VFSFILE *f);

int vfs_fgetc(VFSFILE *f);

int vfs_peakc(VFSFILE *f);

size_t vfs_fwrite(const char *buffer, int bytes, int numn, VFSFILE *f);

VFSFILE *vfs_fdup(VFSFILE *f);

int vfs_flimitslice(VFSFILE *f, uint64_t fileoffset, uint64_t maxlen);

void vfs_FreeFolderList(char **list);

int vfs_ListFolder(
    const char *path,
    char ***contents,
    int returnFullPath,
    int vfsflags
);

int vfs_ListFolderEx(
    const char *abspath,
    const char *relpath,
    char ***contents,
    int returnFullPath,
    int flags
);

#endif  // HORSE64_VFS_H_
