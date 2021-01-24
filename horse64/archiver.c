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

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz/miniz.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "archiver.h"
#include "nonlocale.h"
#include "uri.h"
#include "vfs.h"
#include "vfsstruct.h"

typedef struct h64archive {
    h64archivetype archive_type;
    union {
        uint8_t in_writing_mode, finalize_before_read;
        int64_t cached_entry_count;
        char **cached_entry;
        mz_zip_archive zip_archive;
        char *_last_returned_name;
    };
    VFSFILE *f;
} h64archive;

int64_t h64archive_GetEntryCount(h64archive *a) {
    if (a->archive_type == H64ARCHIVE_TYPE_ZIP) {
        if (a->in_writing_mode)
            return a->cached_entry_count;
        return mz_zip_reader_get_num_files(&a->zip_archive);
    } else {
        return -1;
    }
}

const char *h64archive_GetEntryName(h64archive *a, uint64_t entry) {
    if (a->archive_type == H64ARCHIVE_TYPE_ZIP) {
        if (a->in_writing_mode) {
            if (entry >= (uint64_t)a->cached_entry_count)
                return NULL;
            return a->cached_entry[entry];
        }
        if (a->_last_returned_name) {
            free(a->_last_returned_name);
            a->_last_returned_name = NULL;
        }
        if (entry >= mz_zip_reader_get_num_files(&a->zip_archive))
            return NULL;
        uint64_t neededlen = mz_zip_reader_get_filename(
            &a->zip_archive, entry, NULL, 0
        );
        a->_last_returned_name = malloc(neededlen + 1);
        if (!a->_last_returned_name) {
            return NULL;
        }
        uint64_t written = mz_zip_reader_get_filename(
            &a->zip_archive, entry, a->_last_returned_name, neededlen + 1
        );
        if (written > neededlen)
            written = 0;
        a->_last_returned_name[written] = '\0';
        return a->_last_returned_name;
    } else {
        return NULL;
    }
}

void h64archive_Close(h64archive *a) {
    if (a->archive_type == H64ARCHIVE_TYPE_ZIP) {
        if (a->in_writing_mode) {
            mz_zip_writer_finalize_archive(&a->zip_archive);
            mz_zip_writer_end(&a->zip_archive);
        }
        mz_zip_reader_end(&a->zip_archive);
        if (a->cached_entry) {
            int64_t i = 0;
            while (i < a->cached_entry_count) {
                free(a->cached_entry[i]);
                i++;
            }
            free(a->cached_entry);
        }
        free(a->_last_returned_name);
    }
    vfs_fclose(a->f);
    free(a);
}

static size_t miniz_read_h64archive(
        void *pOpaque, mz_uint64 file_ofs, void *pBuf, size_t n
        ) {
    mz_zip_archive *mza = (mz_zip_archive *)pOpaque;
    h64archive *a = (h64archive *)mza->m_pIO_opaque;
    if (vfs_fseek(a->f, file_ofs) != 0)
        return 0;
    size_t result = vfs_fread(pBuf, 1, n, a->f);
    return result;
}

static size_t miniz_write_h64archive(
        void *pOpaque, mz_uint64 file_ofs, const void *pBuf, size_t n
        ) {
    mz_zip_archive *mza = (mz_zip_archive *)pOpaque;
    h64archive *a = (h64archive *)mza->m_pIO_opaque;
    if (vfs_fseek(a->f, file_ofs) != 0)
        return 0;
    size_t result = vfs_fwrite(pBuf, 1, n, a->f);
    return result;
}

h64archive *archive_FromVFSHandleEx(
        VFSFILE *f, int createifmissing, h64archivetype type) {
    if (!f)
        return NULL;
    VFSFILE *fnew = vfs_fdup(f);
    if (!fnew)
        return NULL;
    h64archive *a = malloc(sizeof(*a));
    if (!a) {
        vfs_fclose(fnew);
        return NULL;
    }
    memset(a, 0, sizeof(*a));
    a->f = fnew;
    if (vfs_fseektoend(a->f) != 0) {
        vfs_fclose(fnew);
        free(a);
        return NULL;
    }
    int64_t size = vfs_ftell(a->f);
    if (vfs_fseek(a->f, 0) != 0) {
        vfs_fclose(fnew);
        free(a);
        return NULL;
    }
    
    // If file is empty, don't allow autodetect or opening without create:
    if ((type == H64ARCHIVE_TYPE_AUTODETECT ||
            !createifmissing) && size == 0) {
        vfs_fclose(fnew);
        free(a);
        return NULL;
    }

    // Otherwise, try to open as zip:
    if (type == H64ARCHIVE_TYPE_AUTODETECT)
        type = H64ARCHIVE_TYPE_ZIP;  // FIXME: guess formats once we got more

    // Handle the various archive types:
    a->archive_type = type;
    if (type == H64ARCHIVE_TYPE_ZIP) {
        a->zip_archive.m_pRead = miniz_read_h64archive;
        a->zip_archive.m_pWrite = miniz_write_h64archive;
        a->zip_archive.m_pIO_opaque = a;
        if (size == 0 && createifmissing) {
            // Create new archive from scratch:
            mz_bool result = mz_zip_writer_init_v2(
                &a->zip_archive, size,
                MZ_ZIP_FLAG_WRITE_ZIP64 |
                MZ_ZIP_FLAG_CASE_SENSITIVE |
                MZ_ZIP_FLAG_WRITE_ALLOW_READING
            );
            if (!result) {
                vfs_fclose(fnew);
                free(a);
                return NULL;
            }
            if (!mz_zip_writer_finalize_archive(&a->zip_archive)) {
                mz_zip_writer_end(&a->zip_archive);
                vfs_fclose(fnew);
                free(a);
                return NULL;
            }
            mz_zip_writer_end(&a->zip_archive);
        }
        // Get a reader.
        mz_bool result = mz_zip_reader_init(
            &a->zip_archive, size,
            MZ_ZIP_FLAG_WRITE_ZIP64 |
            MZ_ZIP_FLAG_CASE_SENSITIVE |
            MZ_ZIP_FLAG_WRITE_ALLOW_READING
        );
        if (!result) {
            vfs_fclose(fnew);
            free(a);
            return NULL;
        }
        return a;
    } else {
        // Unsupported archive.
        vfs_fclose(fnew);
        free(a);
        return NULL;
    }
}

h64archive *archive_FromVFSHandle(
        VFSFILE *f,  h64archivetype type
        ) {
    return archive_FromVFSHandleEx(f, 0, type);
}

h64archive *archive_FromFilePath(
        const char *path, int createifmissing,
        int vfsflags, h64archivetype type
        ) {
    int was_originally_fileuri = 0;
    {
        char header[32];
        if (strlen(path) > strlen("file://")) {
            memcpy(header, path, strlen("file://"));
            header[strlen("file://")] = '\0';
            was_originally_fileuri = (
                h64casecmp(header, "file://") == 0 ||
                h64casecmp(header, "file:\\\\") == 0
            );
        }
    }
    uriinfo *uri = uri_ParseEx(
        path, "file", 0
    );
    if (!uri)
        return NULL;
    if (h64casecmp(uri->protocol, "file") == 0 && (
             (was_originally_fileuri &&
              (vfsflags & VFSFLAG_NO_REALDISK_ACCESS) == 0))) {
        vfsflags |= VFSFLAG_NO_VIRTUALPAK_ACCESS;
    } else if (h64casecmp(uri->protocol, "vfs") == 0) {
        if ((vfsflags & VFSFLAG_NO_VIRTUALPAK_ACCESS) != 0)
            return NULL;
        vfsflags |= VFSFLAG_NO_REALDISK_ACCESS;
    }
    // Get the VFS file:
    errno = 0;
    VFSFILE *f = vfs_fopen(
        uri->path, "r+b", vfsflags
    );
    if (!f && errno == ENOENT && createifmissing) {
        f = vfs_fopen(
            uri->path, "w+b", VFSFLAG_NO_VIRTUALPAK_ACCESS
        );
    }
    if (f) {
        h64archive *a = archive_FromVFSHandleEx(f, createifmissing, type);
        vfs_fclose(f);
        return a;
    }
    vfs_fclose(f);
    return NULL;
}