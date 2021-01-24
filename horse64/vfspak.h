// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_VFSPAK_H_
#define HORSE64_VFSPAK_H_

#include <stdint.h>

typedef struct embeddedvfspakinfo embeddedvfspakinfo;

typedef struct embeddedvfspakinfo {
    uint64_t data_start_offset, data_end_offset;
    embeddedvfspakinfo *next;
} embeddedvfspakinfo;

int vfs_AddPak(const char *path);

int vfs_AddPaksEmbeddedInBinary(const char *path);


#endif  // HORSE64_VFSPAK_H_