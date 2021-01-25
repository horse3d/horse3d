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

#include <errno.h>
#include <physfs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#if defined(_WIN_32) || defined(_WIN64)
#include <windows.h>
#endif

#include "filesys.h"
#include "nonlocale.h"
#include "vfs.h"
#include "vfspak.h"
#include "vfspartialfileio.h"
#include "vfsstruct.h"


//#define DEBUG_VFS

void vfs_fclose(VFSFILE *f) {
    if (!f->via_physfs)
        fclose(f->diskhandle);
    else
        PHYSFS_close(f->physfshandle);
    free(f->mode);
    free(f->path);
    free(f);
}

int vfs_fgetc(VFSFILE *f) {
    unsigned char buf[1];
    size_t result = vfs_fread((char *)buf, 1, 1, f);
    if (result == 1)
        return (int)buf[0];
    return -1;
}

int64_t vfs_ftell(VFSFILE *f) {
    if (!f->via_physfs)
        return ftell(f->diskhandle);
    else
        return PHYSFS_tell(f->physfshandle);
}

int vfs_peakc(VFSFILE *f) {
    int64_t byteoffset = 0;
    if (!f->via_physfs)
        byteoffset = ftell(f->diskhandle);
    else
        byteoffset = PHYSFS_tell(f->physfshandle);
    if (byteoffset < 0 || vfs_feof(f))
        return -1;
    unsigned char buf[1];
    size_t result = vfs_fread((char *)buf, 1, 1, f);
    if (!f->via_physfs)
        fseek(f->diskhandle, byteoffset, SEEK_SET);
    else
        PHYSFS_seek(f->physfshandle, byteoffset);
    if (result == 1)
        return (int)buf[0];
    return -1;
}

int vfs_fwritable(VFSFILE *f) {
    return (!f->via_physfs);
}

size_t vfs_fwrite(const char *buffer, int bytes, int numn, VFSFILE *f) {
    if (f->via_physfs)
        return 0;
    return fwrite(buffer, bytes, numn, f->diskhandle);
}

VFSFILE *vfs_fdup(VFSFILE *f) {
    VFSFILE *fnew = malloc(sizeof(*fnew));
    if (!fnew)
        return NULL;
    memcpy(fnew, f, sizeof(*f));
    fnew->mode = strdup(f->mode);
    if (!fnew->mode) {
        free(fnew);
        return NULL;
    }
    fnew->path = NULL;
    if (!fnew->via_physfs) {
        #if defined(_WIN32) || defined(_WIN64)
        fnew->diskhandle = _fdopen(_dup(
            _fileno(fnew->diskhandle
            )), f->mode);
        #else
        fnew->diskhandle = fdopen(dup(
            fileno(fnew->diskhandle)),
            f->mode);
        #endif
        if (!fnew->diskhandle) {
            free(fnew->mode);
            free(fnew);
            return NULL;
        }
    } else {
        fnew->path = strdup(f->path);
        if (!fnew->path) {
            free(fnew->mode);
            free(fnew);
            return NULL;
        }
        fnew->physfshandle = (
            PHYSFS_openRead(f->path)
        );
        if (!fnew->physfshandle) {
            free(fnew->path);
            free(fnew->mode);
            free(fnew);
            return NULL;
        }
        int seekworked = 0;
        int64_t pos = PHYSFS_tell(f->physfshandle);
        if (pos >= 0) {
            if (PHYSFS_seek(f->physfshandle, pos) == 0) {
                seekworked = 1;
            }
        }
        if (!seekworked) {
            PHYSFS_close(f->physfshandle);
            free(fnew->path);
            free(fnew->mode);
            free(fnew);
            return NULL;
        }
    }
    return fnew;
}

size_t vfs_freadline(VFSFILE *f, char *buf, size_t bufsize) {
    if (bufsize <= 0 || !f)
        return 0;
    buf[0] = '\0';
    uint64_t i = 0;
    while (i + 2 < bufsize) {
        int c_result = vfs_fgetc(f);
        if ((c_result < 0 && vfs_feof(f)) ||
                c_result == '\n' || c_result == '\r') {
            if (c_result == '\r' && vfs_peakc(f) == '\n')
                vfs_fgetc(f);  // Skip '\n' character
            buf[i] = '\n';
            buf[i + 1] = '\0';
            return i;
        } else if (c_result < 0) {
            buf[0] = '\0';
            return 0;
        }
        buf[i] = c_result;
        buf[i + 1] = '\0';
        i++;
    }
    return i;
}

int vfs_feof(VFSFILE *f) {
    if (!f->via_physfs)
        return feof(f->diskhandle);
    if (f->size < 0)
        return 0;
    return ((uint64_t)f->offset >= (uint64_t)f->size);
}

int vfs_fseek(VFSFILE *f, uint64_t offset) {
    uint64_t startoffset = 0;
    if (f->is_limited) {
        startoffset = f->limit_start;
        if (offset > f->limit_len)
            return -1;
    }
    if (!f->via_physfs) {
        if (fseek64(f->diskhandle,
                offset + startoffset, SEEK_SET) == 0) {
            f->offset = offset;
            return 0;
        }
        return -1;
    }
    if (PHYSFS_seek(f->physfshandle, offset + startoffset) != 0) {
        f->offset = offset;
        return 0;
    }
    return -1;
}

int vfs_fseektoend(VFSFILE *f) {
    if (f->is_limited)
        return vfs_fseek(f, f->limit_len);
    if (!f->via_physfs) {
        if (fseek64(f->diskhandle, 0, SEEK_END) == 0) {
            int64_t tellpos = ftell64(f->diskhandle);
            if (tellpos >= 0) {
                f->offset = tellpos;
                return 1;
            }
            // Otherwise, try to revert:
            fseek64(f->diskhandle, f->offset, SEEK_SET);
            return 0;
        }
        return 0;
    }
    if (PHYSFS_seek(f->physfshandle,
            PHYSFS_fileLength(f->physfshandle)) != 0) {
        int64_t tellpos = PHYSFS_tell(f->physfshandle);
        if (tellpos >= 0) {
            f->offset = tellpos;
            return 1;
        }
        // Otherwise, try to revert:
        PHYSFS_seek(f->physfshandle, f->offset);
        return 0;
    }
    return 0;
}

size_t vfs_fread(
        char *buffer, int bytes, int numn, VFSFILE *f
        ) {
    if (bytes <= 0 || numn <= 0)
        return 0;

    if (f->is_limited) {
        while (numn > 0 &&
                f->offset + (bytes * numn) > f->limit_start +
                f->limit_len) {
            if (bytes == 1) {
                numn = ((f->limit_len + f->limit_start) - f->offset);
                break;
            }
            numn--;
        }
        if (numn <= 0)
            return 0;
    }

    if (!f->via_physfs) {
        size_t result = fread(buffer, bytes, numn, f->diskhandle);
        if (result > 0)
            f->offset += result;
        return result;
    }

    if (bytes == 1) {
        size_t result = PHYSFS_readBytes(
            f->physfshandle, buffer, numn
        );
        if (result > 0)
            f->offset += (uint64_t)result;
        return result;
    }

    char *p = buffer;
    size_t result = 0;
    while ((int64_t)bytes + (int64_t)f->offset <
            f->size && numn > 0) {
        size_t presult = PHYSFS_readBytes(
            f->physfshandle, p, bytes
        );
        if (presult >= (uint64_t)bytes) {
            result++;
            numn--;
            p += bytes;
            f->offset += (uint64_t)presult;
        } else {
            PHYSFS_seek(f->physfshandle, f->offset);
            return result;
        }
    }
    return result;
}

int vfs_flimitslice(VFSFILE *f, uint64_t fileoffset, uint64_t maxlen) {
    if (!f)
        return 0;

    // Get the old position to revert to if stuff goes wrong:
    int64_t pos = (
        f->via_physfs ? ftell64(f->diskhandle) :
        PHYSFS_tell(f->physfshandle)
    );
    if (pos < 0)
        return 0;

    // Get the file's current true size:
    int64_t size = -1;
    if (!f->via_physfs) {
        if (fseek64(f->diskhandle, 0, SEEK_END) != 0) {
            // at least TRY to seek back:
            fseek64(f->diskhandle, pos, SEEK_SET);
            return 0;
        }
        size = ftell64(f->diskhandle);
        if (fseek64(f->diskhandle, pos, SEEK_SET) != 0) {  // revert back
            // ... nothing we can do?
            return 0;
        }
    } else {
        size = PHYSFS_fileLength(f->physfshandle);
    }
    if (size < 0) {
        return 0;
    }

    // Make sure the window applied is sane:
    if (fileoffset + maxlen > (uint64_t)size)
        return 0;
    int64_t newpos = pos;
    if ((uint64_t)newpos < fileoffset)
        newpos = fileoffset;
    if ((uint64_t)newpos > fileoffset + maxlen)
        newpos = fileoffset + maxlen;

    // Now, try to seek to the new position that is now inside the window:
    if (f->via_physfs ? PHYSFS_seek(f->physfshandle, newpos) == 0 :
            fseek64(f->diskhandle, newpos, SEEK_SET) == 0) {
        if (f->via_physfs) {  // at least TRY to seek back
            PHYSFS_seek(f->physfshandle, pos);
        } else {
            fseek64(f->diskhandle, pos, SEEK_SET);
        }
        return 0;
    }
    // Apply the new window:
    f->limit_start = fileoffset;
    f->limit_len = maxlen;
    f->is_limited = 1;
    return 1;
}

VFSFILE *vfs_fopen(const char *path, const char *mode, int flags) {
    VFSFILE *vfile = malloc(sizeof(*vfile));
    if (!vfile) {
        #if !defined(_WIN32) && !defined(_WIN64)
        errno = ENOMEM;
        #endif
        return 0;
    }
    memset(vfile, 0, sizeof(*vfile));
    vfile->mode = strdup(mode);
    if (!vfile->mode) {
        free(vfile);
        return 0;
    }
    vfile->size = -1;

    if ((flags & VFSFLAG_NO_VIRTUALPAK_ACCESS) == 0) {
        char *p = vfs_NormalizePath(path);
        if (!p) {
            #if !defined(_WIN32) && !defined(_WIN64)
            errno = ENOMEM;
            #endif
            free(vfile->mode);
            free(vfile);
            return 0;
        }
        if (PHYSFS_exists(p)) {
            vfile->via_physfs = 1;
            vfile->physfshandle = PHYSFS_openRead(p);
            if (vfile->physfshandle) {
                {
                    // HACK: we need this, since physfs has no dup call...
                    vfile->path = strdup(path);
                    if (!vfile->path) {
                        PHYSFS_close(vfile->physfshandle);
                        free(vfile->mode);
                        free(vfile);
                        return 0;
                    }
                }
                vfile->size = (
                    (int64_t)PHYSFS_fileLength(vfile->physfshandle)
                );
                free(p);
                return vfile;
            }
        }
        free(p);
    }
    if ((flags & VFSFLAG_NO_REALDISK_ACCESS) == 0) {
        vfile->via_physfs = 0;
        errno = 0;
        vfile->diskhandle = filesys_OpenFromPath(path, mode);
        if (!vfile->diskhandle) {
            free(vfile->mode);
            free(vfile);
            return 0;
        }
        return vfile;
    }

    free(vfile->mode);
    free(vfile);
    #if !defined(_WIN32) && !defined(_WIN64)
    errno = ENOENT;
    #endif
    return 0;
}

char *vfs_NormalizePath(const char *path) {
    char *p = strdup(path);
    if (!p)
        return NULL;
    if (filesys_IsAbsolutePath(p)) {
        char *pnew = filesys_TurnIntoPathRelativeTo(
            p, NULL
        );
        free(p);
        if (!pnew)
            return NULL;
        p = pnew;
    }
    char *pnew = filesys_Normalize(p);
    free(p);
    if (!pnew)
        return NULL;
    p = pnew;
    #if defined(_WIN32) || defined(_WIN64)
    unsigned int k = 0;
    while (k < strlen(p)) {
        if (p[k] == '\\')
            p[k] = '/';
        k++;
    }
    #endif
    return p;
}

char *vfs_AbsolutePath(const char *path) {
    char *p = vfs_NormalizePath(path);
    if (!p || filesys_IsAbsolutePath(p))
        return p;
    char *result = filesys_Join(
        filesys_GetCurrentDirectory(), p
    );
    free(p);
    return result;
}

int vfs_Exists(const char *path, int *result, int flags) {
    return vfs_ExistsEx(path, path, result, flags);
}

int vfs_ExistsEx(
        const char *abspath, const char *relpath, int *result, int flags
        ) {
    if ((flags & VFSFLAG_NO_VIRTUALPAK_ACCESS) == 0) {
        char *p = vfs_NormalizePath(relpath);
        if (!p)
            return 0;

        int _result = PHYSFS_exists(p);
        if (_result) {
            free(p);
            *result = 1;
            return 1;
        }
        free(p);
    }
    if ((flags & VFSFLAG_NO_REALDISK_ACCESS) == 0) {
        int _result = filesys_FileExists(abspath);
        if (_result) {
            *result = 1;
            return 1;
        }
    }
    *result = 0;
    return 1;
}

int vfs_IsDirectory(const char *path, int *result, int flags) {
    return vfs_IsDirectoryEx(path, path, result, flags);
}

int vfs_IsDirectoryEx(
        const char *abspath, const char *relpath, int *result, int flags
        ) {
    if ((flags & VFSFLAG_NO_VIRTUALPAK_ACCESS) == 0) {
        char *p = vfs_NormalizePath(relpath);
        if (!p)
            return 0;

        PHYSFS_Stat stat;
        if (PHYSFS_stat(p, &stat)) {
            free(p);
            *result = (stat.filetype == PHYSFS_FILETYPE_DIRECTORY);
            return 1;
        }
        free(p);
    }
    if ((flags & VFSFLAG_NO_REALDISK_ACCESS) == 0) {
        *result = filesys_IsDirectory(abspath);
        if (*result)
            return 1;
    }
    *result = 0;
    return 1;
}

int vfs_Size(const char *path, uint64_t *result, int flags) {
    return vfs_SizeEx(path, path, result, flags);
}

int vfs_SizeEx(
        const char *abspath, const char *relpath,
        uint64_t *result, int flags
        ) {
    if ((flags & VFSFLAG_NO_VIRTUALPAK_ACCESS) == 0) {
        char *p = vfs_NormalizePath(relpath);
        if (!p)
            return 0;
        if (PHYSFS_exists(p)) {
            PHYSFS_File *ffile = PHYSFS_openRead(p);
            if (ffile) {
                free(p);
                *result = PHYSFS_fileLength(ffile);
                PHYSFS_close(ffile);
                return 1;
            }
        }
        free(p);
    }
    if ((flags & VFSFLAG_NO_REALDISK_ACCESS) == 0 &&
            filesys_FileExists(abspath)) {
        if (filesys_GetSize(abspath, result))
            return 1;
    }

    *result = 0;
    return 0;
}

int vfs_GetBytes(
        const char *path, uint64_t offset,
        uint64_t bytesamount, char *buffer,
        int flags
        ) {
    return vfs_GetBytesEx(
        path, path, offset, bytesamount, buffer, flags
    );
}

int vfs_GetBytesEx(
        const char *abspath, const char *relpath,
        uint64_t offset,
        uint64_t bytesamount, char *buffer,
        int flags
        ) {
    if ((flags & VFSFLAG_NO_VIRTUALPAK_ACCESS) == 0) {
        char *p = vfs_NormalizePath(relpath);
        if (!p)
            return 0;

        if (PHYSFS_exists(p)) {
            PHYSFS_File *ffile = PHYSFS_openRead(p);
            if (ffile) {
                free(p);
                if (offset > 0) {
                    if (!PHYSFS_seek(ffile, offset)) {
                        PHYSFS_close(ffile);
                        return 0;
                    }
                }

                int64_t result = PHYSFS_readBytes(
                    ffile, buffer, bytesamount
                );
                PHYSFS_close(ffile);
                if (result != (int64_t)bytesamount)
                    return 0;
                return 1;
            }
        }
        free(p);
    }
    if ((flags & VFSFLAG_NO_REALDISK_ACCESS) == 0 &&
            filesys_FileExists(abspath)) {
        FILE *f = fopen64(abspath, "rb");
        if (f) {
            if (fseek64(f, (int64_t)offset, SEEK_SET) != 0) {
                fclose(f);
                return 0;
            }
            size_t result = fread(
                buffer, 1, bytesamount, f
            );
            fclose(f);
            if (result != (size_t)bytesamount)
                return 0;
            return 1;
        }
    }

    return 0;
}

static int _initdone = 0;

void vfs_Init(const char *argv0) {
    if (_initdone)
        return;
    _initdone = 1;

    PHYSFS_init(argv0);

    char *execpath = filesys_GetOwnExecutable();
    if (!execpath) {
        h64fprintf(
            stderr, "horse64/vfs.c: error: fatal, "
            "failed to locate binary directory. "
            "out of memory?\n"
        );
        exit(1);
        return;
    }
    if (!vfs_AddPaksEmbeddedInBinary(execpath)) {
        h64fprintf(
            stderr, "horse64/vfs.c: error: fatal, "
            "failed to load appended VFS data. "
            "out of memory or disk error??\n"
        );
        exit(1);
        return;
    }
    free(execpath);
}

void vfs_FreeFolderList(char **list) {
    int i = 0;
    while (list[i]) {
        free(list[i]);
        i++;
    }
    free(list);
}

int vfs_ListFolder(
        const char *path,
        char ***contents,
        int returnFullPath,
        int flags
        ) {
    return vfs_ListFolderEx(
        path, path, contents, returnFullPath, flags
    );
}

int vfs_ListFolderEx(
        const char *abspath,
        const char *relpath,
        char ***contents,
        int returnFullPath,
        int flags
        ) {
    if ((flags & VFSFLAG_NO_REALDISK_ACCESS) == 0) {
        char *p = vfs_NormalizePath(relpath);
        if (!p)
            return 0;

        if (PHYSFS_exists(p)) {
            while (strlen(p) > 0 && (p[strlen(p) - 1] == '/'
                    #if defined(_WIN32) || defined(_WIN64)
                    || p[strlen(p) - 1] == '\\'
                    #endif
                    )) {
                p[strlen(p) - 1] = '\0';
            }
            char **physfs_alloc_list = PHYSFS_enumerateFiles(p);
            if (physfs_alloc_list) {
                free(p);
                int count = 0;
                const char **ptr = (const char **)physfs_alloc_list;
                while (*ptr) {
                    count++; ptr++;
                }
                char **result = malloc(sizeof(*result) * (count + 1));
                if (!result) {
                    PHYSFS_freeList(result);
                    return 0;
                }
                int i = 0;
                while (i < count) {
                    if (!returnFullPath) {
                        result[i] = strdup(physfs_alloc_list[i]);
                    } else {
                        result[i] = filesys_Join(
                            relpath, physfs_alloc_list[i]
                        );
                    }
                    if (!result[i]) {
                        int k = 0;
                        while (k < i) {
                            free(result[k]);
                            k++;
                        }
                        free(result);
                        PHYSFS_freeList(physfs_alloc_list);
                        return 0;
                    }
                    i++;
                }
                result[count] = NULL;
                *contents = result;
                PHYSFS_freeList(physfs_alloc_list);
                return 1;
            }
        }
        free(p);
    }
    if ((flags & VFSFLAG_NO_REALDISK_ACCESS) == 0 &&
            filesys_FileExists(abspath)) {
        if (filesys_ListFolder(
                abspath, contents, returnFullPath
                )) {
            return 1;
        }
    }
    *contents = NULL;
    return 0;
}
