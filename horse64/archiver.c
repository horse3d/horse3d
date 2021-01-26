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
#include "filesys.h"
#include "nonlocale.h"
#include "uri.h"
#include "vfs.h"
#include "vfsstruct.h"

#define UNCACHED_FILE_SIZE (1024 * 5)

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
    struct {
        int extract_cache_count;
        char **extract_cache_temp_path;
        char **extract_cache_temp_folder;
        char **extract_cache_orig_name;
        char *_read_buf;
    };
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

int64_t h64archive_GetEntrySize(h64archive *a, uint64_t entry) {
    if (a->archive_type == H64ARCHIVE_TYPE_ZIP) {
        mz_zip_archive_file_stat stat = {0};
        mz_bool result = mz_zip_reader_file_stat(
            &a->zip_archive, entry, &stat
        );
        if (!result)
            return -1;
        if (stat.m_uncomp_size > (uint64_t)INT64_MAX)
            return -1;
        return stat.m_uncomp_size;
    }
    return -1;
}

char *h64archive_NormalizeName(const char *name) {
    char *nname = strdup(name);
    if (!nname)
        return NULL;
    uint64_t i = 0;
    while (i < strlen(nname)) {
        if (nname[i] == '\\')
            nname[i] = '/';
        if (nname[i] == '/' &&
                i + 1 < strlen(nname) &&
                nname[i + 1] == '/') {
            memcpy(
                &nname[i], &nname[i + 1],
                strlen(nname) - i
            );
            continue;  // no i++ here!
        }
        i++;
    }
    if (strlen(nname) > 0 && name[strlen(nname) - 1] == '/')
        nname[strlen(nname) - 1] = '\0';
    return nname;
}

int h64archive_GetEntryIndex(
        h64archive *a, const char *filename, int64_t *index
        ) {
    char *cleanname = h64archive_NormalizeName(filename);
    int64_t entry_count = h64archive_GetEntryCount(a);
    int64_t i2 = 0;
    while (i2 < entry_count) {
        const char *e = h64archive_GetEntryName(a, i2);
        if (!e) {
            free(cleanname);
            return 0;
        }
        char *eclean = h64archive_NormalizeName(e);
        if (!eclean) {
            free(cleanname);
            return 0;
        }
        if (strcmp(e, cleanname) == 0) {
            free(eclean);
            free(cleanname);
            *index = i2;
            return 1;
        }
        free(eclean);
        i2++;
    }
    free(cleanname);
    *index = -1;
    return 1;
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

int _h64archive_ReadFileToMemDirectly(
        h64archive *a, int64_t entry, char *buf, size_t buflen
        ) {
    if (a->archive_type == H64ARCHIVE_TYPE_ZIP) {
        mz_bool result = mz_zip_reader_extract_to_mem(
            &a->zip_archive, entry, buf, buflen, 0
        );
        if (!result)
            return 0;
        return 1;
    }
    return 0;
}

int _h64archive_ReadFileToFPtr(
        h64archive *a, int64_t entry, FILE *f
        ) {
    const char *e = h64archive_GetEntryName(a, entry);
    if (!e)
        return 0;
    if (a->archive_type == H64ARCHIVE_TYPE_ZIP) {
        mz_bool result = mz_zip_reader_extract_file_to_cfile(
            &a->zip_archive, e, f, 0
        );
        if (!result)
            return 0;
        return 1;
    }
    return 0;
}

const char *_h64archive_GetFileCachePath(
        h64archive *a, int64_t entry
        ) {
    const char *e = h64archive_GetEntryName(a, entry);
    if (!e)
        return NULL;
    int64_t i = 0;
    while (i < a->extract_cache_count) {
        if (strcmp(a->extract_cache_orig_name[i], e) == 0)
            return a->extract_cache_temp_path[i];
        i++;
    }
    char **orig_name_new = realloc(
        a->extract_cache_orig_name,
        sizeof(*a->extract_cache_orig_name) *
            (a->extract_cache_count + 1)
    );
    if (!orig_name_new)
        return NULL;
    a->extract_cache_orig_name = orig_name_new;
    char **temp_name_new = realloc(
        a->extract_cache_temp_path,
        sizeof(*a->extract_cache_temp_path) *
            (a->extract_cache_count + 1)
    );
    if (!temp_name_new)
        return NULL;
    a->extract_cache_temp_path = temp_name_new;
    char **temp_path_new = realloc(
        a->extract_cache_temp_folder,
        sizeof(*a->extract_cache_temp_folder) *
            (a->extract_cache_count + 1)
    );
    if (!temp_path_new)
        return NULL;
    a->extract_cache_temp_folder = temp_path_new;

    char *folder_path = NULL;
    char *full_path = NULL;
    FILE *f = filesys_TempFile(
        1, "h64archive-", "", &folder_path, &full_path
    );
    if (!f) {
        return NULL;
    }
    if (!_h64archive_ReadFileToFPtr(a, entry, f)) {
        free(full_path);
        free(folder_path);
        fclose(f);
        return NULL;
    }
    fclose(f);
    a->extract_cache_orig_name[
        a->extract_cache_count
    ] = strdup(e);
    if (!a->extract_cache_orig_name[
            a->extract_cache_count
            ]) {
        filesys_RemoveFileOrEmptyDir(full_path);
        filesys_RemoveFolderRecursively(folder_path);
        free(full_path);
        free(folder_path);
        return NULL;
    }
    a->extract_cache_temp_path[
        a->extract_cache_count
    ] = full_path;
    a->extract_cache_temp_folder[
        a->extract_cache_count
    ] = folder_path;
    a->extract_cache_count++;
    return full_path;
}

int h64archive_ReadFileByteSlice(
        h64archive *a, int64_t entry,
        uint64_t offset, char *buf, size_t readlen
        ) {
    int64_t fsize = h64archive_GetEntrySize(a, entry);
    if (fsize < 0)
        return 0;
    if (offset + readlen > (uint64_t)fsize)
        return 0;
    if (fsize < UNCACHED_FILE_SIZE) {
        if (!a->_read_buf) {
            a->_read_buf = malloc(UNCACHED_FILE_SIZE);
            if (!a->_read_buf)
                return 0;
        }
        int result = _h64archive_ReadFileToMemDirectly(
            a, entry, a->_read_buf, fsize
        );
        if (!result)
            return 0;
        memcpy(
            buf, a->_read_buf + offset, readlen
        );
        return 1;
    }
    const char *file_path = _h64archive_GetFileCachePath(
        a, entry
    );
    if (!file_path)
        return 0;
    FILE *f = filesys_OpenFromPath(file_path, "rb");
    if (!f)
        return 0;
    if (fseek64(f, offset, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    size_t result = fread(buf, 1, readlen, f);
    fclose(f);
    if (result == readlen)
        return 1;
    return 0;
}

int _h64archive_EnableWriting(h64archive *a) {
    if (a->archive_type == H64ARCHIVE_TYPE_ZIP) {
        if (a->in_writing_mode)
            return 1;
        if (a->cached_entry) {
            int64_t i = 0;
            while (i < a->cached_entry_count) {
                free(a->cached_entry[i]);
                i++;
            }
            free(a->cached_entry);
        }
        a->cached_entry_count = h64archive_GetEntryCount(a);
        a->cached_entry = malloc(
            sizeof(a->cached_entry) * (
                a->cached_entry_count > 0 ?
                a->cached_entry_count : 1
            )
        );
        if (!a->cached_entry) {
            a->cached_entry_count = 0;
            return 0;
        }
        int64_t i = 0;
        while (i < a->cached_entry_count) {
            const char *e = h64archive_GetEntryName(
                a, i
            );
            if (e)
                a->cached_entry[i] = strdup(e);
            if (!a->cached_entry[i]) {
                int64_t k = 0;
                while (k < i) {
                    free(a->cached_entry[k]);
                    k++;
                }
                free(a->cached_entry);
                a->cached_entry = NULL;
                a->cached_entry_count = 0;
                return 0;
            }
            i++;
        }
        mz_bool result = mz_zip_writer_init_from_reader_v2(
            &a->zip_archive, NULL,
            MZ_ZIP_FLAG_WRITE_ZIP64 |
            MZ_ZIP_FLAG_CASE_SENSITIVE |
            MZ_ZIP_FLAG_WRITE_ALLOW_READING
        );
        if (!result) {
            return 0;
        }
        return 1;
    }
    return 1;
}

int _h64archive_AddFile_CheckName(
        h64archive *a, const char *filename,
        char **cleaned_name
        ) {
    if (strlen(filename) == 0 ||
            filename[strlen(filename) - 1] == '/' ||
            filename[strlen(filename) - 1] == '\\' ||
            filename[0] == '/' || filename[0] == '\\'
            )
        return H64ARCHIVE_ADDERROR_INVALIDNAME;
    unsigned int i = 0;
    while (i < strlen(filename)) {
        if (!is_valid_utf8_char(
                (uint8_t *)(filename + i), strlen(filename + i)
                )) {
            return H64ARCHIVE_ADDERROR_INVALIDNAME;
        }
        i += utf8_char_len((uint8_t *)(filename + i));
    }
    char *clean_name = h64archive_NormalizeName(filename);
    if (!clean_name)
        return H64ARCHIVE_ADDERROR_OUTOFMEMORY;
    int64_t entry_count = h64archive_GetEntryCount(a);
    int64_t i2 = 0;
    while (i2 < entry_count) {
        const char *e = h64archive_GetEntryName(a, i2);
        if (!e) {
            free(clean_name);
            return H64ARCHIVE_ADDERROR_OUTOFMEMORY;
        }
        char *eclean = h64archive_NormalizeName(e);
        if (!eclean) {
            free(clean_name);
            return H64ARCHIVE_ADDERROR_OUTOFMEMORY;
        }
        if (strcmp(eclean, clean_name) == 0) {
            free(eclean);
            free(clean_name);
            return H64ARCHIVE_ADDERROR_DUPLICATENAME;
        }
        free(eclean);
        i2++;
    }
    *cleaned_name = clean_name;
    return H64ARCHIVE_ADDERROR_SUCCESS;
}

int h64archive_AddFileFromMem(
        h64archive *a, const char *filename,
        const char *bytes, uint64_t byteslen
        ) {
    char *cleaned_name = NULL;
    int result = _h64archive_AddFile_CheckName(
        a, filename, &cleaned_name
    );
    if (result != H64ARCHIVE_ADDERROR_SUCCESS)
        return result;
    if (!_h64archive_EnableWriting(a)) {
        free(cleaned_name);
        return H64ARCHIVE_ADDERROR_IOERROR;
    }
    if (a->archive_type == H64ARCHIVE_TYPE_ZIP) {
        char **_expanded_names = realloc(
            a->cached_entry,
            sizeof(*a->cached_entry) * (a->cached_entry_count + 1)
        );
        if (!_expanded_names) {
            free(cleaned_name);
            return H64ARCHIVE_ADDERROR_OUTOFMEMORY;
        }
        a->cached_entry = _expanded_names;
        mz_bool result2 = mz_zip_writer_add_mem(
            &a->zip_archive, cleaned_name, bytes, byteslen,
            MZ_BEST_COMPRESSION
        );
        if (!result2) {
            free(cleaned_name);
            return H64ARCHIVE_ADDERROR_IOERROR;
        }
        a->cached_entry[a->cached_entry_count] = cleaned_name;
        a->cached_entry_count++;
        return 1;
    } else {
        free(cleaned_name);
        return H64ARCHIVE_ADDERROR_OUTOFMEMORY;
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
    {
        int64_t i = 0;
        while (i < a->extract_cache_count) {
            filesys_RemoveFileOrEmptyDir(
                a->extract_cache_temp_path[i]
            );
            filesys_RemoveFolderRecursively(
                a->extract_cache_temp_folder[i]
            );
            free(a->extract_cache_temp_path[i]);
            free(a->extract_cache_temp_folder[i]);
            free(a->extract_cache_orig_name[i]);
            i++;
        }
        free(a->extract_cache_orig_name);
        free(a->extract_cache_temp_path);
        free(a->extract_cache_temp_folder);
    }
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

h64archive *archive_FromFilePathSlice(
        const char *pathoruri, uint64_t fileoffset, uint64_t maxlen,
        int createifmissing, int vfsflags, h64archivetype type
        ) {
    int was_originally_fileuri = 0;
    {
        char header[32];
        if (strlen(pathoruri) > strlen("file://")) {
            memcpy(header, pathoruri, strlen("file://"));
            header[strlen("file://")] = '\0';
            was_originally_fileuri = (
                h64casecmp(header, "file://") == 0 ||
                h64casecmp(header, "file:\\\\") == 0
            );
        }
    }

    // Parse path as URI to see if it's VFS or not:
    uriinfo *uri = uri_ParseEx(
        pathoruri, "file", 0
    );
    if (!uri)
        return NULL;
    if (h64casecmp(uri->protocol, "file") == 0 && (
             (was_originally_fileuri &&
              (vfsflags & VFSFLAG_NO_REALDISK_ACCESS) == 0))) {
        vfsflags |= VFSFLAG_NO_VIRTUALPAK_ACCESS;
    } else if (h64casecmp(uri->protocol, "vfs") == 0) {
        if ((vfsflags & VFSFLAG_NO_VIRTUALPAK_ACCESS) != 0) {
            uri_Free(uri);
            return NULL;
        }
        vfsflags |= VFSFLAG_NO_REALDISK_ACCESS;
    }
    if (h64casecmp(uri->protocol, "file") != 0 &&
            h64casecmp(uri->protocol, "vfs") != 0) {
        uri_Free(uri);
        return NULL;
    }

    // Get the VFS file:
    errno = 0;
    VFSFILE *f = vfs_fopen(
        uri->path, "r+b", vfsflags
    );
    uri_Free(uri);
    if (!f && errno == ENOENT && createifmissing) {
        f = vfs_fopen(
            uri->path, "w+b", VFSFLAG_NO_VIRTUALPAK_ACCESS
        );
    }
    if (f) {
        // Make sure the offset parameters are applied & valid:
        if (fileoffset > 0 || maxlen > 0) {
            if (vfs_fseektoend(f) != 0) {
                vfs_fclose(f);
                return NULL;
            }
            int64_t end = vfs_ftell(f);
            if (end < 0 || fileoffset + maxlen > (uint64_t)end ||
                    fileoffset >= (uint64_t)end) {
                vfs_fclose(f);
                return NULL;
            }
            if (maxlen == 0) {
                maxlen = (uint64_t)end - fileoffset;
            }
            if (!vfs_flimitslice(f, fileoffset, maxlen)) {
                vfs_fclose(f);
                return NULL;
            }
        }
        // Now load archive:
        h64archive *a = archive_FromVFSHandleEx(f, createifmissing, type);
        vfs_fclose(f);
        return a;
    }
    vfs_fclose(f);
    return NULL;
}

h64archive *archive_FromFilePath(
        const char *pathoruri, int createifmissing, int vfsflags,
        h64archivetype type
        ) {
    return archive_FromFilePathSlice(
        pathoruri, 0, 0, createifmissing, vfsflags, type
    );
}