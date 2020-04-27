
#define _FILE_OFFSET_BITS 64
#define __USE_LARGEFILE64
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#if defined(_WIN32) || defined(_WIN64)
#define fseek64 _fseeki64
#define ftell64 _ftelli64
#else
#define fseek64 fseeko64
#define ftell64 ftello64
#endif

#include <errno.h>
#include <physfs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "filesys.h"
#include "vfs.h"

//#define DEBUG_VFS

static int vfs_includes_cwd = 1;

typedef struct VFSFILE {
    int via_physfs;
    union {
        PHYSFS_File *physfshandle;
        FILE *diskhandle;
    };
    int64_t size;
    uint64_t offset;
} VFSFILE;

void vfs_fclose(VFSFILE *f) {
    if (!f->via_physfs)
        fclose(f->diskhandle);
    else
        PHYSFS_close(f->physfshandle);
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
    if (!f->via_physfs)
        return (fseek64(f->diskhandle, offset, SEEK_SET) == 0);

    return (PHYSFS_seek(f->physfshandle, offset) != 0);
}

size_t vfs_fread(
        char *buffer, int bytes, int numn, VFSFILE *f
        ) {
    if (bytes <= 0 || numn <= 0)
        return 0;

    if (!f->via_physfs)
        return fread(buffer, bytes, numn, f->diskhandle);

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

VFSFILE *vfs_fopen(const char *path, const char *mode) {
    char *p = vfs_NormalizePath(path);
    if (!p) {
        #if !defined(_WIN32) && !defined(_WIN64)
        errno = ENOMEM;
        #endif
        return 0;
    }

    VFSFILE *vfile = malloc(sizeof(*vfile));
    if (!vfile) {
        free(p);
        #if !defined(_WIN32) && !defined(_WIN64)
        errno = ENOMEM;
        #endif
        return 0;
    }
    memset(vfile, 0, sizeof(*vfile));
    vfile->size = -1;

    int _result = PHYSFS_exists(p);
    if (!_result && !vfs_includes_cwd) {
        free(vfile);
        free(p);
        #if !defined(_WIN32) && !defined(_WIN64)
        errno = ENOENT;
        #endif
        return 0;
    } else if (!_result) {
        vfile->via_physfs = 0;
        errno = 0;
        vfile->diskhandle = fopen64(p, mode);
        free(p);
        if (!vfile->diskhandle) {
            free(vfile);
            #if !defined(_WIN32) && !defined(_WIN64)
            errno = ENOENT;
            #endif
            return 0;
        }
        return vfile;
    }

    vfile->via_physfs = 1;
    vfile->physfshandle = PHYSFS_openRead(p);
    free(p);
    if (!vfile->physfshandle) {
        free(vfile);
        #if !defined(_WIN32) && !defined(_WIN64)
        errno = ENOENT;
        #endif
        return 0;
    }
    vfile->size = (int64_t)PHYSFS_fileLength(vfile->physfshandle);
    return vfile;
}

void vfs_EnableCurrentDirectoryDiskAccess(int enable) {
    vfs_includes_cwd = (enable != 0);
}

int vfs_AddPak(const char *path) {
    if (!path || !filesys_FileExists(path) ||
            filesys_IsDirectory(path) ||
            strlen(path) < strlen(".h3dpak") ||
            memcmp(path + strlen(path) - strlen(".h3dpak"),
                   ".h3dpak", strlen(".h3dpak")) != 0)
        return 0;
    #if defined(DEBUG_VFS) && !defined(NDEBUG)
    printf("horse64/vfs.c: debug: "
           "adding resource pack: %s\n", path);
    #endif
    if (!PHYSFS_mount(path, "/", 1)) {
        fprintf(stderr,
            "horse64/vfs.c: warning: "
            "failed to add resource pack: %s\n", path
        );
        return 0;
    }

    return 1;
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
    int k = 0;
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

int vfs_Exists(const char *path, int *result) {
    char *p = vfs_NormalizePath(path);
    if (!p)
        return 0;

    int _result = PHYSFS_exists(p);
    if (!_result && vfs_includes_cwd) {
        _result = filesys_FileExists(p);
    }
    free(p);
    *result = _result;
    return 1;
}

int vfs_ExistsIgnoringCurrentDirectoryDiskAccess(
        const char *path, int *result
        ) {
    char *p = vfs_NormalizePath(path);
    if (!p)
        return 0;

    int _result = PHYSFS_exists(p);
    free(p);
    *result = _result;
    return 1;
}

int vfs_IsDirectory(const char *path, int *result) {
    char *p = vfs_NormalizePath(path);
    if (!p)
        return 0;

    if (!PHYSFS_exists(p) && filesys_FileExists(p) &&
            vfs_includes_cwd) {
        *result = filesys_IsDirectory(p);
    } else {
        PHYSFS_Stat stat;
        if (!PHYSFS_stat(p, &stat)) {
            free(p);
            return 0;
        }
        *result = (stat.filetype == PHYSFS_FILETYPE_DIRECTORY);
    }
    free(p);
    return 1;
}

int vfs_Size(const char *path, uint64_t *result) {
    char *p = vfs_NormalizePath(path);
    if (!p)
        return 0;

    if (!PHYSFS_exists(p) && filesys_FileExists(p) &&
            vfs_includes_cwd) {
        if (!filesys_GetSize(p, result)) {
            free(p);
            return 0;
        }
        free(p);
        return 1;
    }

    PHYSFS_File *ffile = PHYSFS_openRead(p);
    free(p);
    if (!ffile)
        return 0;

    *result = PHYSFS_fileLength(ffile);
    PHYSFS_close(ffile);
    return 1;
}

int vfs_GetBytes(
        const char *path, uint64_t offset,
        uint64_t bytesamount, char *buffer
        ) {
    char *p = vfs_NormalizePath(path);
    if (!p)
        return 0;

    if (!PHYSFS_exists(p) && filesys_FileExists(p) &&
            vfs_includes_cwd) {
        FILE *f = fopen64(p, "rb");
        free(p);
        if (!f) {
            return 0;
        }
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

    PHYSFS_File *ffile = PHYSFS_openRead(p);
    free(p);
    if (!ffile)
        return 0;

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

static int _initdone = 0;

void vfs_Init(const char *argv0) {
    if (_initdone)
        return;
    _initdone = 1;

    PHYSFS_init(argv0);

    char *execdir = filesys_GetOwnExecutable();
    if (execdir) {
        char *_s = filesys_ParentdirOfItem(execdir);
        free(execdir);
        execdir = _s;
    }
    if (!execdir) {
        fprintf(stderr, "horse64/vfs.c: warning: "
                "failed to locate binary directory");
        return;
    }
    char *coreapipath = filesys_Join(
        execdir, "coreapi.h3dpak"
    );
    free(execdir);
    execdir = NULL;
    if (!coreapipath) {
        fprintf(stderr, "horse64/vfs.c: warning: "
                "failed to allocate coreapi path");
        return;
    }
    vfs_AddPak(coreapipath);
    free(coreapipath);
}
