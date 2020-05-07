
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

VFSFILE *vfs_fopen(const char *path, const char *mode, int flags) {
    VFSFILE *vfile = malloc(sizeof(*vfile));
    if (!vfile) {
        #if !defined(_WIN32) && !defined(_WIN64)
        errno = ENOMEM;
        #endif
        return 0;
    }
    memset(vfile, 0, sizeof(*vfile));
    vfile->size = -1;

    if ((flags & VFSFLAG_NO_VIRTUALPAK_ACCESS) == 0) {
        char *p = vfs_NormalizePath(path);
        if (!p) {
            #if !defined(_WIN32) && !defined(_WIN64)
            errno = ENOMEM;
            #endif
            return 0;
        }
        if (PHYSFS_exists(p)) {
            vfile->via_physfs = 1;
            vfile->physfshandle = PHYSFS_openRead(p);
            if (vfile->physfshandle) {
                vfile->size = (int64_t)PHYSFS_fileLength(vfile->physfshandle);
                free(p);
                return vfile;
            }
        }
        free(p);
    }
    if ((flags & VFSFLAG_NO_VIRTUALPAK_ACCESS) == 0) {
        vfile->via_physfs = 0;
        errno = 0;
        vfile->diskhandle = fopen64(path, mode);
        if (vfile->diskhandle) {
            return vfile;
        }
        return vfile;
    }

    free(vfile);
    #if !defined(_WIN32) && !defined(_WIN64)
    errno = ENOENT;
    #endif
    return 0;
}

int vfs_AddPak(const char *path) {
    if (!path || !filesys_FileExists(path) ||
            filesys_IsDirectory(path) ||
            strlen(path) < strlen(".h64pak") ||
            memcmp(path + strlen(path) - strlen(".h64pak"),
                   ".h64pak", strlen(".h64pak")) != 0)
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

int vfs_Exists(const char *path, int *result, int flags) {
    if ((flags & VFSFLAG_NO_VIRTUALPAK_ACCESS) == 0) {
        char *p = vfs_NormalizePath(path);
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
        int _result = filesys_FileExists(path);
        if (_result) {
            *result = 1;
            return 1;
        }
    }
    *result = 0;
    return 1;
}

int vfs_IsDirectory(const char *path, int *result, int flags) {
    if ((flags & VFSFLAG_NO_VIRTUALPAK_ACCESS) == 0) {
        char *p = vfs_NormalizePath(path);
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
        *result = filesys_IsDirectory(path);
        if (*result)
            return 1;
    }
    *result = 0;
    return 1;
}

int vfs_Size(const char *path, uint64_t *result, int flags) {
    if ((flags & VFSFLAG_NO_VIRTUALPAK_ACCESS) == 0) {
        char *p = vfs_NormalizePath(path);
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
            filesys_FileExists(path)) {
        if (filesys_GetSize(path, result))
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
    if ((flags & VFSFLAG_NO_VIRTUALPAK_ACCESS) == 0) {
        char *p = vfs_NormalizePath(path);
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
            filesys_FileExists(path)) {
        FILE *f = fopen64(path, "rb");
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
        execdir, "coreapi.h64pak"
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
    if ((flags & VFSFLAG_NO_REALDISK_ACCESS) == 0) {
        char *p = vfs_NormalizePath(path);
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
                            path, physfs_alloc_list[i]
                        );
                    }
                    if (!result[i]) {
                        int k = 0;
                        while (k < i) {
                            free(result[k]);
                            k++;
                        }
                        free(result);
                        PHYSFS_freeList(result);
                        return 0;
                    }
                    i++;
                }
                result[count] = NULL;
                *contents = result;
                PHYSFS_freeList(result);
                return 1;
            }
        }
        free(p);
    }
    if ((flags & VFSFLAG_NO_REALDISK_ACCESS) == 0 &&
            filesys_FileExists(path)) {
        if (filesys_ListFolder(
                path, contents, returnFullPath
                )) {
            return 1;
        }
    }
    *contents = NULL;
    return 0;
}
