// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_VFSPAK_H_
#define HORSE64_VFSPAK_H_

#include "compileconfig.h"

#include <stdint.h>
#include <stdio.h>

#include "widechar.h"

typedef struct VFSFILE VFSFILE;
typedef struct embeddedvfspakinfo embeddedvfspakinfo;

typedef struct embeddedvfspakinfo {
    uint64_t data_start_offset, data_end_offset;
    uint64_t full_with_header_start_offset, full_with_header_end_offset;
    embeddedvfspakinfo *next;
} embeddedvfspakinfo;


int vfs_GetEmbbeddedPakInfo(
    const h64wchar *path, int64_t pathlen,
    embeddedvfspakinfo **einfo
);

int vfs_GetEmbbeddedPakInfoByVFSFile(
    VFSFILE *f, embeddedvfspakinfo **einfo
);

int vfs_GetEmbbeddedPakInfoByStdioFile(
    FILE *f, embeddedvfspakinfo **einfo
);

int vfs_HasEmbbededPakGivenFilePath(
    embeddedvfspakinfo *einfo, const char *binary_path,
    const char *file_path, int *out_result
);

int vfs_HasEmbbededPakThatContainsFilePath_Stdio(
    embeddedvfspakinfo *einfo, FILE *binary_file,
    const char *file_path, int *out_result
);

int vfs_AddPak(const h64wchar *path, int64_t pathlen);

int vfs_AddPaksEmbeddedInBinary(FILE *binhandle);

int vfs_AddPakStdioEx(
    FILE *origf, uint64_t start_offset, uint64_t max_len
);

void vfs_FreeEmbeddedPakInfo(embeddedvfspakinfo *einfo);

#endif  // HORSE64_VFSPAK_H_