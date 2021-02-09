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

#include <assert.h>
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
#include "filesys32.h"
#include "nonlocale.h"
#include "vfs.h"
#include "vfspak.h"
#include "vfspartialfileio.h"
#include "vfsstruct.h"


//#define DEBUG_VFS

void vfs_fclose(VFSFILE *f) {
    if (!f)
        return;
    if (!f->via_physfs) {
        if (f->diskhandle)
            fclose(f->diskhandle);
    } else {
        PHYSFS_close(f->physfshandle);
    }
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
    int64_t result = -1;
    if (!f->via_physfs)
        result = ftell(f->diskhandle);
    else
        result = PHYSFS_tell(f->physfshandle);
    if (result >= 0 && f->is_limited) {
        result -= f->limit_start;
        if (result < 0 || result > (int64_t)f->limit_len)
            result = -1;
    }
    return result;
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
        fnew->diskhandle = _dupfhandle(fnew->diskhandle, f->mode);
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
            if (PHYSFS_seek(f->physfshandle, pos)) {
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
        // Seek worked.
        f->offset = offset;
        return 0;
    }
    return -1;
}

int vfs_fseektoend(VFSFILE *f) {
    if (f->is_limited)
        return (vfs_fseek(f, f->limit_len) == 0);
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
        (!f->via_physfs) ? ftell64(f->diskhandle) :
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
            fseek64(f->diskhandle, newpos, SEEK_SET) < 0) {
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
    int64_t pathu32len = 0;
    h64wchar *pathu32 = AS_U32(path, &pathu32len);
    if (!pathu32) {
        errno = ENOMEM;
        return 0;
    }
    VFSFILE *f = vfs_fopen_u32(pathu32, pathu32len, mode, flags);
    free(pathu32);
    return f;
}

VFSFILE *vfs_fopen_u32(
        const h64wchar *path, int64_t pathlen,
        const char *mode, int flags) {
    VFSFILE *vfile = malloc(sizeof(*vfile));
    if (!vfile) {
        errno = ENOMEM;
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
        char *pathu8 = AS_U8(path, pathlen);
        if (!pathu8)
            return 0;
        char *p = vfs_NormalizePath(pathu8);
        free(pathu8);
        pathu8 = NULL;
        if (!p) {
            errno = ENOMEM;
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
                    vfile->path = strdup(p);
                    if (!vfile->path) {
                        PHYSFS_close(vfile->physfshandle);
                        free(p);
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
        int innererr = 0;
        vfile->diskhandle = filesys32_OpenFromPath(
            path, pathlen, mode, &innererr
        );
        if (!vfile->diskhandle) {
            free(vfile->mode);
            free(vfile);
            return 0;
        }
        return vfile;
    }

    free(vfile->mode);
    free(vfile);
    errno = ENOENT;
    return 0;
}

char *vfs_NormalizePath(const char *path) {
    char *p = strdup(path);
    if (!p)
        return NULL;
    if (filesys_IsAbsolutePath(p)) {
        int64_t pu32len = 0;
        h64wchar *pu32 = AS_U32(p, &pu32len);
        if (!pu32) {
            free(p);
            return NULL;
        }
        int64_t pnewu32len = 0;
        h64wchar *pnewu32 = filesys32_TurnIntoPathRelativeTo(
            pu32, pu32len, NULL, 0, &pnewu32len
        );
        free(p);
        char *pnew = NULL;
        if (pnewu32)
            pnew = AS_U8(pnewu32, pnewu32len);
        free(pnewu32);
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

int vfs_Exists(const char *path, int *result, int flags) {
    return vfs_ExistsEx(path, path, result, flags);
}

int vfs_ExistsU32(
        const h64wchar *path, int64_t pathlen,
        int *result, int flags
        ) {
    return vfs_ExistsExU32(
        path, pathlen, path, pathlen, result, flags
    );
}


int vfs_ExistsEx(
        const char *abspath, const char *relpath, int *result, int flags
        ) {
    int64_t absu32len = 0;
    h64wchar *absu32 = NULL;
    if (abspath)
        absu32 = AS_U32(abspath, &absu32len);
    int64_t relu32len = 0;
    h64wchar *relu32 = NULL;
    if (relpath)
        relu32 = AS_U32(relpath, &relu32len);
    if ((abspath && !absu32) || (relpath && !relu32)) {
        free(absu32);
        free(relu32);
        return 0;
    }
    int retval = vfs_ExistsExU32(
        absu32, absu32len, relu32, relu32len, result, flags
    );
    free(absu32);
    free(relu32);
    return retval;
}

int vfs_ExistsExU32(
        const h64wchar *abspath, int64_t abspathlen,
        const h64wchar *relpath, int64_t relpathlen,
        int *result, int flags
        ) {
    if (!abspath && !relpath)
        return 0;
    if ((flags & VFSFLAG_NO_VIRTUALPAK_ACCESS) == 0) {
        char *relpathu8 = AS_U8(
            relpath, relpathlen
        );
        if (!relpathu8)
            return 0;
        char *p = vfs_NormalizePath(relpathu8);
        free(relpathu8);
        relpathu8 = NULL;
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
        int _exists = 0;
        int _result = filesys32_TargetExists(
            abspath, abspathlen, &_exists
        );
        if (!_result)
            return 0;  // since I/O error or OOM
        *result = _exists;
        return 1;
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
    int64_t absu32len = 0;
    h64wchar *absu32 = NULL;
    if (abspath)
        absu32 = AS_U32(abspath, &absu32len);
    int64_t relu32len = 0;
    h64wchar *relu32 = NULL;
    if (relpath)
        relu32 = AS_U32(relpath, &relu32len);
    if ((abspath && !absu32) || (relpath && !relu32)) {
        free(absu32);
        free(relu32);
        return 0;
    }
    int retval = vfs_IsDirectoryExU32(
        absu32, absu32len, relu32, relu32len, result, flags
    );
    free(absu32);
    free(relu32);
    return retval;
}

int vfs_IsDirectoryU32(
        const h64wchar *path, int64_t pathlen,
        int *result, int flags
        ) {
    return vfs_IsDirectoryExU32(
        path, pathlen, path, pathlen, result, flags
    );
}

int vfs_IsDirectoryExU32(
        const h64wchar *abspath, int64_t abspathlen,
        const h64wchar *relpath, int64_t relpathlen,
        int *result, int flags
        ) {
    if ((flags & VFSFLAG_NO_VIRTUALPAK_ACCESS) == 0) {
        char *relpathu8 = AS_U8(
            relpath, relpathlen
        );
        if (!relpathu8)
            return 0;
        char *p = vfs_NormalizePath(relpathu8);
        free(relpathu8);
        relpathu8 = NULL;
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
        int _isdir = 0;
        int _result = filesys32_IsDirectory(
            abspath, abspathlen, &_isdir
        );
        if (!_result)
            return 0;
        *result = _isdir;
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
    int64_t absu32len = 0;
    h64wchar *absu32 = NULL;
    if (abspath)
        absu32 = AS_U32(abspath, &absu32len);
    int64_t relu32len = 0;
    h64wchar *relu32 = NULL;
    if (relpath)
        relu32 = AS_U32(relpath, &relu32len);
    if ((abspath && !absu32) || (relpath && !relu32)) {
        free(absu32);
        free(relu32);
        return 0;
    }
    int retval = vfs_SizeExU32(
        absu32, absu32len, relu32, relu32len, result, flags
    );
    free(absu32);
    free(relu32);
    return retval;
}

int vfs_SizeU32(
        const h64wchar *path, int64_t pathlen,
        uint64_t *result, int flags
        ) {
    return vfs_SizeExU32(
        path, pathlen, path, pathlen, result, flags
    );
}

int vfs_SizeExU32(
        const h64wchar *abspath, int64_t abspathlen,
        const h64wchar *relpath, int64_t relpathlen,
        uint64_t *result, int flags
        ) {
    if ((flags & VFSFLAG_NO_VIRTUALPAK_ACCESS) == 0) {
        char *relpathu8 = AS_U8(
            relpath, relpathlen
        );
        if (!relpathu8)
            return 0;
        char *p = vfs_NormalizePath(relpathu8);
        free(relpathu8);
        relpathu8 = NULL;
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
    if ((flags & VFSFLAG_NO_REALDISK_ACCESS) == 0) {
        int _exists = 0;
        int _result = filesys32_TargetExists(
            abspath, abspathlen, &_exists
        );
        if (!_result)
            return 0;
        int innererr = 0;
        if (filesys32_GetSize(abspath, abspathlen, result, &innererr))
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
    int64_t absu32len = 0;
    h64wchar *absu32 = NULL;
    if (abspath)
        absu32 = AS_U32(abspath, &absu32len);
    int64_t relu32len = 0;
    h64wchar *relu32 = NULL;
    if (relpath)
        relu32 = AS_U32(relpath, &relu32len);
    if ((abspath && !absu32) || (relpath && !relu32)) {
        free(absu32);
        free(relu32);
        return 0;
    }
    int retval = vfs_GetBytesExU32(
        absu32, absu32len, relu32, relu32len,
        offset, bytesamount, buffer, flags
    );
    free(absu32);
    free(relu32);
    return retval;
}

int vfs_GetBytesU32(
        const h64wchar *path, int64_t pathlen, uint64_t offset,
        uint64_t bytesamount, char *buffer,
        int flags
        ) {
    return vfs_GetBytesExU32(
        path, pathlen, path, pathlen, offset, bytesamount,
        buffer, flags
    );
}

int vfs_GetBytesExU32(
        const h64wchar *abspath, int64_t abspathlen,
        const h64wchar *relpath, int64_t relpathlen,
        uint64_t offset,
        uint64_t bytesamount, char *buffer,
        int flags
        ) {
    if ((flags & VFSFLAG_NO_VIRTUALPAK_ACCESS) == 0) {
        char *relpathu8 = AS_U8(
            relpath, relpathlen
        );
        if (!relpathu8)
            return 0;
        char *p = vfs_NormalizePath(relpathu8);
        free(relpathu8);
        relpathu8 = NULL;
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
    if ((flags & VFSFLAG_NO_REALDISK_ACCESS) == 0) {
        int _exists = 0;
        int _result = filesys32_TargetExists(
            abspath, abspathlen, &_exists
        );
        if (!_result)
            return 0;
        int innererr = 0;
        FILE *f = filesys32_OpenFromPath(
            abspath, abspathlen, "rb", &innererr
        );
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

void vfs_Init() {
    if (_initdone)
        return;
    _initdone = 1;

    PHYSFS_init(NULL);

    FILE *f = filesys32_OpenOwnExecutable();
    if (!f) {
        h64fprintf(
            stderr, "horse64/vfs.c: error: fatal, "
            "failed to access own binary. "
            "out of memory or file deleted?\n"
        );
        exit(1);
        return;
    }
    if (!vfs_AddPaksEmbeddedInBinary(f)) {
        h64fprintf(
            stderr, "horse64/vfs.c: error: fatal, "
            "failed to load appended VFS data. "
            "out of memory or disk error??\n"
        );
        fclose(f);
        exit(1);
        return;
    }
    fclose(f);
}

void vfs_DetachFD(VFSFILE *f) {
    assert(f->via_physfs == 0);
    f->diskhandle = NULL;
}

VFSFILE *vfs_ownThisFD(FILE *f, const char *reopenmode) {
    VFSFILE *vfile = malloc(sizeof(*vfile));
    if (!vfile)
        return NULL;
    memset(vfile, 0, sizeof(*vfile));
    vfile->mode = strdup(reopenmode);
    if (!vfile->mode) {
        free(vfile);
        return NULL;
    }
    vfile->size = -1;
    vfile->via_physfs = 0;
    vfile->diskhandle = f;
    int64_t i = ftell64(vfile->diskhandle);
    if (i < 0) {
        free(vfile->mode);
        free(vfile);
        return NULL;
    }
    vfile->offset = i;
    return vfile;
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
