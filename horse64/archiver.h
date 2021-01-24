// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_ARCHIVER_H_
#define HORSE64_ARCHIVER_H_

#include <stdint.h>
#include <stdio.h>

typedef struct VFSFILE VFSFILE;
typedef struct h64archive h64archive;
typedef enum h64archivetype {
    H64ARCHIVE_TYPE_AUTODETECT = 0,
    H64ARCHIVE_TYPE_ZIP = 1
} h64archivetype;

h64archive *archive_FromVFSHandle(
    VFSFILE *f, h64archivetype type
);

int64_t h64archive_GetEntryCount(h64archive *a);

const char *h64archive_GetEntryName(h64archive *a, uint64_t entry);

void h64archive_Close(h64archive *a);

h64archive *archive_FromFilePath(
    const char *pathoruri, int createifmissing, int vfsflags,
    h64archivetype type
);

#endif  // HORSE64_ARCHIVER_H_