// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#define _FILE_OFFSET_BITS 64
#ifndef __USE_LARGEFILE64
#define __USE_LARGEFILE64 1
#endif
#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif
#define _LARGEFILE_SOURCE
#if defined(_WIN32) || defined(_WIN64)
#define fseek64 _fseeki64
#define ftell64 _ftelli64
#else
#define fseek64 fseeko64
#define ftell64 ftello64
#endif

#include "compileconfig.h"

#include <physfs.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <errno.h>
#endif

#include "filesys32.h"
#include "vfspartialfileio.h"

typedef struct partialfileinfo {
    FILE *f;
    uint64_t offset;
    uint64_t start;
    uint64_t end;
} partialfileinfo;

PHYSFS_sint64 _partialfile_read(
        struct PHYSFS_Io *io, void *buf, PHYSFS_uint64 len
        ) {
    partialfileinfo *pinfo = io->opaque;
    if (pinfo->offset >= pinfo->end) {  // Already at end
        return 0;
    }
    if (pinfo->offset + len > pinfo->end)  // Limit to max len
        len = pinfo->end - pinfo->offset;
    // Do the actual reading:
    size_t result = fread(
        buf, 1, len, pinfo->f
    );
    if (result > 0) {  // It worked! Update offset & done:
        pinfo->offset += result;
        return result;
    }
    // Ooops, failure. Let's try to update the offset:
    int64_t offset = ftell64(pinfo->f);
    if (offset >= 0)
        pinfo->offset = offset;
    return -1;
}

int _partialfile_seek(
        struct PHYSFS_Io *io, PHYSFS_uint64 offset
        ) {
    partialfileinfo *pinfo = io->opaque;
    if (offset > pinfo->end)
        return -1;
    if (fseek(pinfo->f, pinfo->start + offset, SEEK_SET) < 0) {
        // Ooops, failure. Let's try to update the offset:
        int64_t offset = ftell64(pinfo->f);
        if (offset >= 0)
            pinfo->offset = offset;
        return 0;
    }
    pinfo->offset = pinfo->start + offset;
    return 1;
}

PHYSFS_sint64 _partialfile_tell(struct PHYSFS_Io *io) {
    partialfileinfo *pinfo = io->opaque;
    return pinfo->offset - pinfo->start;
}

PHYSFS_sint64 _partialfile_length(struct PHYSFS_Io *io) {
    partialfileinfo *pinfo = io->opaque;
    return pinfo->end - pinfo->start;
}

struct PHYSFS_Io *_partialfile_duplicate(struct PHYSFS_Io *io) {
    partialfileinfo *pinfo = io->opaque;

    partialfileinfo *pinfonew = malloc(
        sizeof(*pinfonew)
    );
    if (!pinfonew)
        return NULL;
    memcpy(pinfonew, pinfo, sizeof(*pinfo));
    pinfonew->f = NULL;
    #if defined(_WIN32) || defined(_WIN64)
    pinfonew->f = _fdopen(_dup(_fileno(pinfo->f)), "rb");
    #else
    pinfonew->f = fdopen(dup(fileno(pinfo->f)), "rb");
    #endif
    if (!pinfonew->f) {
        free(pinfonew);
        return NULL;
    }

    PHYSFS_Io *ionew = malloc(sizeof(*ionew));
    if (!ionew) {
        fclose(pinfonew->f);
        free(pinfonew);
        return NULL;
    }
    memcpy(ionew, io, sizeof(*io));
    ionew->opaque = pinfonew;
    return ionew;
}

void _partialfile_destroy(struct PHYSFS_Io *io) {
    partialfileinfo *pinfo = io->opaque;
    fclose(pinfo->f);
    free(pinfo);
    free(io);
}

int _partialfile_flush(struct PHYSFS_Io *io) {
    partialfileinfo *pinfo = io->opaque;
    return (fflush(pinfo->f) == 0);
}

void *_PhysFS_Io_partialFileReadOnlyStruct(
        FILE *forig, uint64_t start, uint64_t len
        ) {
    FILE *f = _dupfhandle(forig, "rb");
    if (!f)
        return NULL;
    if (len <= 0) {
        if (fseek64(f, 0, SEEK_END) < 0) {
            fclose(f);
            return NULL;
        }
        int64_t v = ftell64(f);
        if (v < 0) {
            fclose(f);
            return NULL;
        }
        if (start < (uint64_t)v)
            len = v - start;
    }
    if (len <= 0) {
        fclose(f);
        return NULL;
    }
    if (fseek64(f, start, SEEK_SET) < 0) {
        fclose(f);
            return NULL;
    }

    partialfileinfo *pinfo = malloc(sizeof(*pinfo));
    if (!pinfo) {
        fclose(f);
        return NULL;
    }
    memset(pinfo, 0, sizeof(*pinfo));
    pinfo->f = f;
    pinfo->start = start;
    pinfo->end = start + len;
    pinfo->offset = start;

    PHYSFS_Io *io = malloc(sizeof(*io));
    if (!io) {
        fclose(f);
        free(pinfo);
        return NULL;
    }
    memset(io, 0, sizeof(*io));

    io->version = 0;
    io->opaque = pinfo;
    io->read = _partialfile_read;
    io->seek = _partialfile_seek;
    io->tell = _partialfile_tell;
    io->duplicate = _partialfile_duplicate;
    io->flush = _partialfile_flush;
    io->destroy = _partialfile_destroy;
    io->length = _partialfile_length;
    return io;
}