// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_VFSSTRUCT_H_
#define HORSE64_VFSSTRUCT_H_

#include <physfs.h>
#include <stdint.h>
#include <stdio.h>

typedef struct VFSFILE {
    int via_physfs;
    union {
        PHYSFS_File *physfshandle;
        FILE *diskhandle;
    };
    int64_t size;
    uint64_t offset;
    char *mode, *path;
} VFSFILE;

#endif  // HORSE64_VFSSTRUCT_H_