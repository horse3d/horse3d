// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_FILESYS_H_
#define HORSE64_FILESYS_H_

#include <stdint.h>
#include <stdio.h>

FILE *filesys_OpenFromPath(
    const char *path, const char *mode
);

int filesys_FileExists(const char *path);

int filesys_IsDirectory(const char *path);

int filesys_RemoveFolderRecursively(const char *path);

int filesys_RemoveFileOrEmptyDir(const char *path);

const char *filesys_AppDataSubFolder(
    const char *appname
);

const char *filesys_DocumentsSubFolder(
    const char *subfolder
);

int filesys_IsSymlink(const char *path, int *result);

void filesys_FreeFolderList(char **list);

int filesys_ListFolder(const char *path,
                       char ***contents,
                       int returnFullPath);

int filesys_GetComponentCount(const char *path);

void filesys_RequestFilesystemAccess();

int filesys_CreateDirectory(
    const char *path, int user_readable_only
);

char *filesys_GetOwnExecutable();

int filesys_LaunchExecutable(const char *path, int argc, ...);

char *filesys_ParentdirOfItem(const char *path);

char *filesys_Join(const char *path1, const char *path2);

int filesys_IsAbsolutePath(const char *path);

char *filesys_ToAbsolutePath(const char *path);

char *filesys_Basename(const char *path);

char *filesys_Dirname(const char *path);

int filesys_GetSize(const char *path, uint64_t *size);

char *filesys_Normalize(const char *path);

char *filesys_NormalizeEx(const char *path, int couldbewinpath);

char *filesys_GetCurrentDirectory();

char *filesys_GetRealPath(const char *s);

char *filesys_TurnIntoPathRelativeTo(
    const char *path, const char *makerelativetopath
);

int filesys_FolderContainsPath(
    const char *folder_path, const char *check_path,
    int *result
);

FILE *filesys_TempFile(
    int subfolder, const char *prefix,
    const char *suffix, char **folder_path,
    char **path
);

#endif  // HORSE64_FILESYS_H_
