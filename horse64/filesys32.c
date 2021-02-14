// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"
#include <stdint.h>

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
#define fdopen64 fdopen
#else
#define fseek64 fseeko64
#define ftell64 ftello64
#define fdopen64 fdopen
#endif

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#if defined(_WIN32) || defined(_WIN64)
#define _O_RDONLY 0x0000
#define _O_WRONLY 0x0001
#define _O_RDWR 0x0002
#define _O_APPEND 0x0008
#include <malloc.h>
#include <windows.h>
#include <shlobj.h>
#if defined(__MING32__) || defined(__MINGW64__)
#ifndef ERROR_DIRECTORY_NOT_SUPPORTED
#define ERROR_DIRECTORY_NOT_SUPPORTED (0x150)
#endif
#endif
int _open_osfhandle(intptr_t osfhandle, int flags);
#else
#if !defined(ANDROID) && !defined(__ANDROID__)
#include <pwd.h>
#endif
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif
#if defined(__FreeBSD__) || defined(__FREEBSD__)
#include <sys/sysctl.h>
#endif

#include "filesys.h"
#include "filesys32.h"
#include "nonlocale.h"
#include "secrandom.h"
#include "threading.h"
#include "widechar.h"


FILE *filesys32_OpenFromPath(
        const h64wchar *path, int64_t pathlen,
        const char *mode, int *err
        ) {
    if (filesys32_IsObviouslyInvalidPath(path, pathlen)) {
        *err = FS32_ERR_NOSUCHTARGET;
        return NULL;
    }

    int innererr = 0;
    h64filehandle os_f = filesys32_OpenFromPathAsOSHandleEx(
        path, pathlen, mode, 0, &innererr
    );
    if (os_f == H64_NOFILE) {
        *err = innererr;
        return NULL;
    }

    #if defined(_WIN32) || defined(_WIN64)
    int mode_read = (
        strstr(mode, "r") || strstr(mode, "a") || strstr(mode, "w+")
    );
    int mode_write = (
        strstr(mode, "w") || strstr(mode, "a") || strstr(mode, "r+")
    );
    int mode_append = strstr(mode, "r+") || strstr(mode, "a");
    int filedescr = _open_osfhandle(
        (intptr_t)os_f,
        ((mode_read && !mode_read && !mode_write) ? _O_RDONLY : 0) |
        (((mode_write || mode_append) && !mode_read) ? _O_WRONLY : 0) |
        (((mode_write || mode_append) && mode_read) ? _O_RDWR : 0) |
        (mode_append ? _O_APPEND : 0)
    );
    if (filedescr < 0) {
        *err = FS32_ERR_OTHERERROR;
        CloseHandle(os_f);
        return NULL;
    }
    os_f = NULL;  // now owned by 'filedescr'
    errno = 0;
    FILE *fresult = _fdopen(filedescr, mode);
    if (!fresult) {
        *err = FS32_ERR_OTHERERROR;
        _close(filedescr);
        return NULL;
    }
    return fresult;
    #else
    FILE *f = fdopen64(os_f, mode);
    if (!f) {
        *err = FS32_ERR_OTHERERROR;
        if (errno == ENOENT)
            *err = FS32_ERR_NOSUCHTARGET;
        else if (errno == EMFILE || errno == ENFILE)
            *err = FS32_ERR_OUTOFFDS;
        else if (errno == EACCES || errno == EPERM)
            *err = FS32_ERR_NOPERMISSION;
        return NULL;
    }
    return f;
    #endif
}

int filesys32_SetOctalPermissions(
        const h64wchar *path, int64_t pathlen, int *err,
        int permission_extra,
        int permission_user, int permission_group,
        int permission_any
        ) {
    if (filesys32_IsObviouslyInvalidPath(path, pathlen)) {
        *err = FS32_ERR_NOSUCHTARGET;
        return 0;
    }
    #if defined(_WIN32) || defined(_WIN64)
    *err = FS32_ERR_UNSUPPORTEDPLATFORM;
    return 0;
    #else
    char *pathu8 = AS_U8(path, pathlen);
    if (!pathu8) {
        *err = FS32_ERR_OUTOFMEMORY;
        return 0;
    }
    char number[] = "0000";
    if (permission_extra >= 0 && permission_extra <= 9)
        number[0] = '0' + permission_extra;
    if (permission_user >= 0 && permission_user <= 9)
        number[1] = '0' + permission_user;
    if (permission_group >= 0 && permission_group <= 9)
        number[2] = '0' + permission_group;
    if (permission_any >= 0 && permission_any <= 9)
        number[3] = '0' + permission_any;
    int octal_perm = h64strtoll(number, NULL, 8);
    if (octal_perm > 0) {
        if (chmod(pathu8, octal_perm) != 0) {
            free(pathu8);
            *err = FS32_ERR_OTHERERROR;
            if (errno == ENOENT || errno == ENOTDIR)
                *err = FS32_ERR_NOSUCHTARGET;
            else if (errno == EMFILE || errno == ENFILE)
                *err = FS32_ERR_OUTOFFDS;
            else if (errno == EACCES || errno == EPERM)
                *err = FS32_ERR_NOPERMISSION;
            else if (errno == ENOMEM)
                *err = FS32_ERR_OUTOFMEMORY;
            return 0;
        }
    }
    free(pathu8);
    return 1;
    #endif
}

int filesys32_GetOctalPermissions(
        const h64wchar *path, int64_t pathlen, int *err,
        int *permission_extra,
        int *permission_user, int *permission_group,
        int *permission_any
        ) {
    if (filesys32_IsObviouslyInvalidPath(path, pathlen)) {
        *err = FS32_ERR_NOSUCHTARGET;
        return 0;
    }
    #if defined(_WIN32) || defined(_WIN64)
    *err = FS32_ERR_UNSUPPORTEDPLATFORM;
    return 0;
    #else
    char *targetpath = AS_U8(path, pathlen);
    if (!targetpath) {
        *err = FS32_ERR_OUTOFMEMORY;
        return 0;
    }
    struct stat64 sb = {0};
    int statresult = (stat64(targetpath, &sb) == 0);
    free(targetpath);
    if (!statresult) {
        *err = FS32_ERR_OTHERERROR;
        if (errno == EACCES || errno == EPERM)
            *err = FS32_ERR_NOPERMISSION;
        if (errno == EMFILE || errno == ENFILE)
            *err = FS32_ERR_OUTOFFDS;
        if (errno == ENOMEM)
            *err = FS32_ERR_OUTOFMEMORY;
        if (errno == ENOENT || errno == ENOTDIR)
            *err = FS32_ERR_NOSUCHTARGET;
        return 1;
    }
    int permissions = (
        sb.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO)
    );
    char permissionsoctal[10];
    h64snprintf(
        permissionsoctal, sizeof(permissionsoctal) - 1,
        "%o", (int)permissions
    );
    *permission_extra = (
        (strlen(permissionsoctal) >= 4 &&
        permissionsoctal[strlen(permissionsoctal) - 4] >= '0' &&
        permissionsoctal[strlen(permissionsoctal) - 4] <= '9') ?
        (permissionsoctal[strlen(permissionsoctal) - 4] - '0') : 0
    );
    *permission_user = (
        (strlen(permissionsoctal) >= 3 &&
        permissionsoctal[strlen(permissionsoctal) - 3] >= '0' &&
        permissionsoctal[strlen(permissionsoctal) - 3] <= '9') ?
        (permissionsoctal[strlen(permissionsoctal) - 3] - '0') : 0
    );
    *permission_group = (
        (strlen(permissionsoctal) >= 2 &&
        permissionsoctal[strlen(permissionsoctal) - 2] >= '0' &&
        permissionsoctal[strlen(permissionsoctal) - 2] <= '9') ?
        (permissionsoctal[strlen(permissionsoctal) - 2] - '0') : 0
    );
    *permission_any = (
        (strlen(permissionsoctal) >= 1 &&
        permissionsoctal[strlen(permissionsoctal) - 1] >= '0' &&
        permissionsoctal[strlen(permissionsoctal) - 1] <= '9') ?
        (permissionsoctal[strlen(permissionsoctal) - 1] - '0') : 0
    );
    return 1;
    #endif
}

int filesys32_SetExecutable(
        const h64wchar *path, int64_t pathlen, int *err
        ) {
    if (filesys32_IsObviouslyInvalidPath(path, pathlen)) {
        *err = FS32_ERR_NOSUCHTARGET;
        return 0;
    }
    #if defined(_WIN32) || defined(_WIN64)
    int _exists = 0;
    if (filesys32_TargetExists(
            path, pathlen, &_exists
            )) {
        *err = FS32_ERR_OTHERERROR;
        return 0;
    }
    if (!_exists) {
        *err = FS32_ERR_NOSUCHTARGET;
        return 0;
    }
    return 1;
    #else
    int permission_extra = 0;
    int permission_user = 0;
    int permission_group = 0;
    int permission_any = 0;
    int _err = 0;
    int result = filesys32_GetOctalPermissions(
        path, pathlen, &_err, &permission_extra,
        &permission_user, &permission_group, &permission_any
    );
    if (!result) {
        *err = _err;
        return 0;
    }
    if (permission_user == 6)
        permission_user = 7;
    else if (permission_user == 4)
        permission_user = 5;
    else if (permission_user == 2)
        permission_user = 3;
    if (permission_group == 6)
        permission_group = 7;
    else if (permission_group == 4)
        permission_group = 5;
    else if (permission_group == 2)
        permission_group = 3;
    if (permission_any == 6)
        permission_any = 7;
    else if (permission_any == 4)
        permission_any = 5;
    else if (permission_any == 2)
        permission_any = 3;
    result = filesys32_SetOctalPermissions(
        path, pathlen, &_err,
        permission_extra, permission_user,
        permission_group, permission_any
    );
    if (!result) {
        *err = _err;
        return 0;
    }
    return 1;
    #endif
    *err = FS32_ERR_OTHERERROR;
    return 0;
}

int filesys32_IsObviouslyInvalidPath(
        const h64wchar *p, int64_t plen
        ) {
    int64_t i = 0;
    while (i < plen) {
        if (p[i] == '\0')
            return 1;
        #if defined(_WIN32) || defined(_WIN64)
        if (p[i] == '*' || p[i] == '%' ||
                (p[i] == ':' && i != 2) ||
                p[i] == '"' || p[i] == '?' ||
                p[i] == '|' || p[i] == '>' ||
                p[i] == '<')
            return 1;
        #endif
        i++;
    }
    return 0;
}

char *filesys23_ContentsAsStr(
        const h64wchar *path, int64_t pathlen, int *error
        ) {
    if (filesys32_IsObviouslyInvalidPath(path, pathlen)) {
        *error = FS32_ERR_NOSUCHTARGET;
        return NULL;
    }
    int _innererr = 0;
    FILE *f = filesys32_OpenFromPath(
        path, pathlen, "rb", &_innererr
    );
    if (!f) {
        *error = _innererr;
        return NULL;
    }
    if (fseek64(f, 0, SEEK_END) != 0) {
        *error = FS32_ERR_IOERROR;
        fclose(f);
        return NULL;
    }
    int64_t len = ftell64(f);
    if (len < 0) {
        *error = FS32_ERR_IOERROR;
        fclose(f);
        return NULL;
    }
    if (fseek64(f, 0, SEEK_SET) != 0) {
        *error = FS32_ERR_IOERROR;
        fclose(f);
        return NULL;
    }
    char *resultstr = malloc(len + 1);
    if (!resultstr) {
        *error = FS32_ERR_OUTOFMEMORY;
        fclose(f);
        return NULL;
    }
    size_t result = fread(resultstr, 1, len, f);
    fclose(f);
    if ((int64_t)result != len) {
        *error = FS32_ERR_IOERROR;
        free(resultstr);
        return NULL;
    }
    resultstr[len] = '\0';
    return resultstr;
}

void filesys32_FreeFolderList(h64wchar **list, int64_t *listlen) {
    if (list) {
        int64_t k = 0;
        while (list[k]) {
            free(list[k]);
            k++;
        }
        free(list);
    }
    if (listlen)
        free(listlen);
}

int filesys32_RemoveFileOrEmptyDir(
        const h64wchar *path32, int64_t path32len, int *error
        ) {
    if (filesys32_IsObviouslyInvalidPath(path32, path32len)) {
        *error = FS32_ERR_NOSUCHTARGET;
        return 0;
    }

    #if defined(_WIN32) || defined(_WIN64)
    assert(sizeof(wchar_t) == sizeof(uint16_t));
    wchar_t *targetpath = malloc(
        sizeof(*targetpath) * (path32len * 2 + 1)
    );
    if (!targetpath) {
        *error = FS32_ERR_OUTOFMEMORY;
        return 0;
    }
    int64_t targetpathlen = 0;
    int result = utf32_to_utf16(
        path32, path32len, (char *)targetpath,
        sizeof(*targetpath) * (path32len * 2 + 1),
        &targetpathlen, 1
    );
    if (!result || targetpathlen >= (path32len * 2 + 1)) {
        *error = FS32_ERR_OUTOFMEMORY;
        return 0;
    }
    targetpath[targetpathlen] = '\0';
    if (DeleteFileW(targetpath) != TRUE) {
        uint32_t werror = GetLastError();
        *error = FS32_ERR_OTHERERROR;
        if (werror == ERROR_DIRECTORY_NOT_SUPPORTED ||
                werror == ERROR_DIRECTORY) {
            if (RemoveDirectoryW(targetpath) != TRUE) {
                free(targetpath);
                werror = GetLastError();
                *error = FS32_ERR_OTHERERROR;
                if (werror == ERROR_PATH_NOT_FOUND ||
                        werror == ERROR_FILE_NOT_FOUND ||
                        werror == ERROR_INVALID_PARAMETER ||
                        werror == ERROR_INVALID_NAME ||
                        werror == ERROR_INVALID_DRIVE)
                    *error = FS32_ERR_NOSUCHTARGET;
                else if (werror == ERROR_ACCESS_DENIED ||
                        werror == ERROR_WRITE_PROTECT ||
                        werror == ERROR_SHARING_VIOLATION)
                    *error = FS32_ERR_NOPERMISSION;
                else if (werror == ERROR_NOT_ENOUGH_MEMORY)
                    *error = FS32_ERR_OUTOFMEMORY;
                else if (werror == ERROR_TOO_MANY_OPEN_FILES)
                    *error = FS32_ERR_OUTOFFDS;
                else if (werror == ERROR_PATH_BUSY ||
                        werror == ERROR_BUSY ||
                        werror == ERROR_CURRENT_DIRECTORY)
                    *error = FS32_ERR_DIRISBUSY;
                else if (werror == ERROR_DIR_NOT_EMPTY)
                    *error = FS32_ERR_NONEMPTYDIRECTORY;
                return 0;
            }
            free(targetpath);
            *error = FS32_ERR_SUCCESS;
            return 1;
        }
        free(targetpath);
        if (werror == ERROR_PATH_NOT_FOUND ||
                werror == ERROR_FILE_NOT_FOUND ||
                werror == ERROR_INVALID_PARAMETER ||
                werror == ERROR_INVALID_NAME ||
                werror == ERROR_INVALID_DRIVE)
            *error = FS32_ERR_NOSUCHTARGET;
        else if (werror == ERROR_ACCESS_DENIED ||
                werror == ERROR_WRITE_PROTECT ||
                werror == ERROR_SHARING_VIOLATION)
            *error = FS32_ERR_NOPERMISSION;
        else if (werror == ERROR_NOT_ENOUGH_MEMORY)
            *error = FS32_ERR_OUTOFMEMORY;
        else if (werror == ERROR_TOO_MANY_OPEN_FILES)
            *error = FS32_ERR_OUTOFFDS;
        else if (werror == ERROR_PATH_BUSY ||
                werror == ERROR_BUSY)
            *error = FS32_ERR_DIRISBUSY;
        return 0;
    }
    free(targetpath);
    *error = FS32_ERR_SUCCESS;
    #else
    int64_t plen = 0;
    char *p = malloc(path32len * 5 + 1);
    if (!p) {
        *error = FS32_ERR_OUTOFMEMORY;
        return 0;
    }
    int result = utf32_to_utf8(
        path32, path32len, p, path32len * 5 + 1,
        &plen, 1, 1
    );
    if (!result || plen >= path32len * 5 + 1) {
        free(p);
        *error = FS32_ERR_OUTOFMEMORY;
        return 0;
    }
    p[plen] = '\0';
    errno = 0;
    result = remove(p);
    free(p);
    if (result != 0) {
        *error = FS32_ERR_OTHERERROR;
        if (errno == EACCES || errno == EPERM ||
                errno == EROFS) {
            *error = FS32_ERR_NOPERMISSION;
        } else if (errno == ENOTEMPTY) {
            *error = FS32_ERR_NONEMPTYDIRECTORY;
        } else if (errno == ENOENT || errno == ENAMETOOLONG ||
                errno == ENOTDIR) {
            *error = FS32_ERR_NOSUCHTARGET;
        } else if (errno == EBUSY) {
            *error = FS32_ERR_DIRISBUSY;
        }
        return 0;
    }
    *error = FS32_ERR_SUCCESS;
    #endif
    return 1;
}

int filesys32_ListFolderEx(
        const h64wchar *path32, int64_t path32len,
        h64wchar ***contents, int64_t **contentslen,
        int returnFullPath, int allowsymlink, int *error
        ) {
    if (filesys32_IsObviouslyInvalidPath(path32, path32len)) {
        *error = FS32_ERR_TARGETNOTADIRECTORY;
        return 0;
    }

    // Hack for "" referring to cwd:
    static h64wchar dotfolder[] = {'.'};
    if (path32len == 0) {
        path32 = dotfolder;
        path32len = 1;
    }

    // Start scanning the files:
    #if defined(_WIN32) || defined(_WIN64)
    assert(sizeof(wchar_t) == sizeof(uint16_t));
    WIN32_FIND_DATAW ffd;
    int isfirst = 1;
    wchar_t *folderpath = malloc(
        sizeof(*folderpath) * (path32len * 2 + 1)
    );
    if (!folderpath) {
        *error = FS32_ERR_OUTOFMEMORY;
        return 0;
    }
    int64_t folderpathlen = 0;
    int result = utf32_to_utf16(
        path32, path32len, (char *)folderpath,
        sizeof(*folderpath) * (path32len * 2 + 1),
        &folderpathlen, 1
    );
    if (!result || folderpathlen >= (path32len * 2)) {
        free(folderpath);
        *error = FS32_ERR_OUTOFMEMORY;
        return 0;
    }
    folderpath[folderpathlen] = '\0';
    wchar_t *p = malloc(wcslen(folderpath) + 3);
    if (!p) {
        free(folderpath);
        *error = FS32_ERR_OUTOFMEMORY;
        return 0;
    }
    memcpy(p, folderpath, wcslen(folderpath) + 1);
    if (p[wcslen(p) - 1] != '\\') {
        p[wcslen(p) + 1] = '\0';
        p[wcslen(p)] = '\\';
    }
    p[wcslen(p) + 1] = '\0';
    p[wcslen(p)] = '*';
    free((void *)folderpath);
    folderpath = NULL;
    HANDLE hFind = FindFirstFileW(p, &ffd);
    if (hFind == INVALID_HANDLE_VALUE) {
        free(p);
        uint64_t werror = GetLastError();
        if (werror == ERROR_NO_MORE_FILES) {
            // Special case, empty directory.
            *contents = malloc(sizeof(*contents) * 1);
            *contentslen = malloc(sizeof(*contentslen) * 1);
            if (!*contents || !*contentslen) {
                free(*contents);
                free(*contentslen);
                *error = FS32_ERR_OUTOFMEMORY;
                return 0;
            }
            (*contents)[0] = NULL;
            (*contentslen)[0] = -1;
            *error = FS32_ERR_SUCCESS;
            return 1;
        }
        *error = FS32_ERR_OTHERERROR;
        if (werror == ERROR_PATH_NOT_FOUND ||
                werror == ERROR_FILE_NOT_FOUND)
            *error = FS32_ERR_NOSUCHTARGET;
        else if (werror == ERROR_INVALID_PARAMETER ||
                werror == ERROR_INVALID_NAME ||
                werror == ERROR_INVALID_DRIVE ||
                werror == ERROR_DIRECTORY_NOT_SUPPORTED)
            *error = FS32_ERR_TARGETNOTADIRECTORY;
        else if (werror == ERROR_ACCESS_DENIED ||
                werror == ERROR_SHARING_VIOLATION)
            *error = FS32_ERR_NOPERMISSION;
        else if (werror == ERROR_NOT_ENOUGH_MEMORY)
            *error = FS32_ERR_OUTOFMEMORY;
        else if (werror == ERROR_TOO_MANY_OPEN_FILES)
            *error = FS32_ERR_OUTOFFDS;
        return 0;
    }
    free(p);
    #else
    int64_t plen = 0;
    char *p = malloc(path32len * 5 + 1);
    if (!p) {
        *error = FS32_ERR_OUTOFMEMORY;
        return 0;
    }
    int result = utf32_to_utf8(
        path32, path32len, p, path32len * 5 + 1,
        &plen, 1, 1
    );
    if (!result || plen >= path32len * 5 + 1) {
        free(p);
        *error = FS32_ERR_OUTOFMEMORY;
        return 0;
    }
    p[plen] = '\0';
    errno = 0;
    DIR *d = NULL;
    if (allowsymlink) {
        // Allow using regular mechanism:
        d = opendir(
            (strlen(p) > 0 ? p : ".")
        );
    } else {
        // Open as fd first, so we can avoid symlinks.
        errno = 0;
        int dirfd = open64(
            (strlen(p) > 0 ? p : "."),
            O_RDONLY | O_NOFOLLOW | O_LARGEFILE | O_NOCTTY
        );
        if (dirfd < 0) {
            free(p);
            *error = FS32_ERR_OTHERERROR;
            if (errno == EMFILE || errno == ENFILE)
                *error = FS32_ERR_OUTOFFDS;
            else if (errno == ENOMEM)
                *error = FS32_ERR_OUTOFMEMORY;
            else if (errno == ELOOP)
                *error = FS32_ERR_SYMLINKSWEREEXCLUDED;
            else if (errno == EACCES)
                *error = FS32_ERR_NOPERMISSION;
            return 0;
        }
        d = fdopendir(dirfd);
    }
    free(p);
    if (!d) {
        *error = FS32_ERR_OTHERERROR;
        if (errno == EMFILE || errno == ENFILE)
            *error = FS32_ERR_OUTOFFDS;
        else if (errno == ENOMEM)
            *error = FS32_ERR_OUTOFMEMORY;
        else if (errno == ENOTDIR || errno == ENOENT)
            *error = FS32_ERR_TARGETNOTADIRECTORY;
        else if (errno == EACCES)
            *error = FS32_ERR_NOPERMISSION;
        return 0;
    }
    #endif
    // Now, get all the files entries and put them into the list:
    h64wchar **list = malloc(sizeof(*list));
    int64_t *listlen = malloc(sizeof(*listlen));
    if (!list || !listlen) {
        free(list);
        free(listlen);
        *error = FS32_ERR_OUTOFMEMORY;
        return 0;
    }
    list[0] = NULL;
    listlen[0] = 0;
    h64wchar **fullPathList = NULL;
    int64_t *fullPathListLen = 0;
    int entriesSoFar = 0;
    while (1) {
        #if defined(_WIN32) || defined(_WIN64)
        if (isfirst) {
            isfirst = 0;
        } else {
            if (FindNextFileW(hFind, &ffd) == 0) {
                uint32_t werror = GetLastError();
                if (werror != ERROR_NO_MORE_FILES) {
                    *error = FS32_ERR_OTHERERROR;
                    if (werror == ERROR_NOT_ENOUGH_MEMORY)
                        *error = FS32_ERR_OUTOFMEMORY;
                    goto errorquit;
                }
                break;
            }
        }
        const wchar_t *entryNameWide = ffd.cFileName;
        int _conversionoom = 0;
        int64_t entryNameLen = 0;
        h64wchar *entryName = utf16_to_utf32(
            (uint16_t *)entryNameWide, wcslen(entryNameWide),
            &entryNameLen, 1, &_conversionoom
        );
        if (!entryName) {
            *error = FS32_ERR_OUTOFMEMORY;
            goto errorquit;
        }
        #else
        errno = 0;
        struct dirent *entry = readdir(d);
        if (!entry && errno != 0) {
            *error = FS32_ERR_OUTOFMEMORY;
            goto errorquit;
        }
        if (!entry)
            break;
        const char *entryName8 = entry->d_name;
        if (strcmp(entryName8, ".") == 0 ||
                strcmp(entryName8, "..") == 0) {
            continue;
        }
        int64_t entryNameLen = 0;
        h64wchar *entryName = utf8_to_utf32(
            entryName8, strlen(entryName8),
            NULL, NULL, &entryNameLen
        );
        if (!entryName) {
            *error = FS32_ERR_OUTOFMEMORY;
            goto errorquit;
        }
        #endif
        h64wchar **nlist = realloc(
            list, sizeof(*nlist) * (entriesSoFar + 2)
        );
        if (nlist)
            list = nlist;
        int64_t *nlistlen = realloc(
            listlen, sizeof(*nlistlen) * (entriesSoFar + 2)
        );
        if (nlistlen)
            listlen = nlistlen;
        if (!nlist || !nlistlen) {
            *error = FS32_ERR_OUTOFMEMORY;
            free(entryName);
            errorquit: ;
            #if defined(_WIN32) || defined(_WIN64)
            if (hFind != INVALID_HANDLE_VALUE)
                FindClose(hFind);
            #else
            if (d)
                closedir(d);
            #endif
            if (list) {
                int k = 0;
                while (k < entriesSoFar) {
                    if (list[k])
                        free(list[k]);
                    else
                        break;
                    k++;
                }
                free(list);
                free(listlen);
            }
            if (fullPathList) {
                int k = 0;
                while (fullPathList[k]) {
                    free(fullPathList[k]);
                    k++;
                }
                free(fullPathList);
                free(fullPathListLen);
            }
            return 0;
        }
        list = nlist;
        entriesSoFar++;
        list[entriesSoFar] = NULL;
        list[entriesSoFar - 1] = entryName;
        listlen[entriesSoFar - 1] = entryNameLen;
    }

    // Ok, done with extracting all names:
    #if defined(_WIN32) || defined(_WIN64)
    FindClose(hFind);
    hFind = INVALID_HANDLE_VALUE;
    #else
    closedir(d);
    d = NULL;
    #endif

    // Convert names to full path if requested:
    if (!returnFullPath) {
        // No conversion needed:
        *contents = list;
        *contentslen = listlen;
        *error = FS32_ERR_SUCCESS;
    } else {
        // Ok, allocate new array for conversion:
        fullPathList = malloc(sizeof(*fullPathList) * (entriesSoFar + 1));
        if (!fullPathList) {
            *error = FS32_ERR_OUTOFMEMORY;
            goto errorquit;
        }
        memset(fullPathList, 0, sizeof(*fullPathList) * (entriesSoFar + 1));
        fullPathListLen = malloc(
            sizeof(*fullPathListLen) * (entriesSoFar + 1)
        );
        if (!fullPathListLen) {
            *error = FS32_ERR_OUTOFMEMORY;
            goto errorquit;
        }
        int k = 0;
        while (k < entriesSoFar) {
            fullPathList[k] = malloc(
                (path32len + 1 + listlen[k]) * sizeof(*list[k])
            );
            if (!fullPathList[k]) {
                *error = FS32_ERR_OUTOFMEMORY;
                goto errorquit;
            }
            memcpy(fullPathList[k], path32, sizeof(*path32) * path32len);
            fullPathListLen[k] = path32len;
            #if defined(_WIN32) || defined(_WIN64)
            if (path32len > 0 && fullPathList[k][path32len - 1] != '/' &&
                    fullPathList[k][path32len - 1] != '\\') {
                fullPathList[k][path32len] = '\\';
                fullPathListLen[k]++;
            }
            #else
            if (path32len > 0 && fullPathList[k][path32len - 1] != '/') {
                fullPathList[k][path32len] = '/';
                fullPathListLen[k]++;
            }
            #endif
            memcpy(
                fullPathList[k] + fullPathListLen[k],
                list[k], listlen[k] * sizeof(*list[k])
            );
            fullPathListLen[k] += listlen[k];
            k++;
        }
        fullPathList[entriesSoFar] = NULL;
        k = 0;
        while (k < entriesSoFar && list[k]) {  // free orig data
            free(list[k]);
            k++;
        }
        free(list);
        free(listlen);
        // Return the full path arrays:
        *contents = fullPathList;
        *contentslen = fullPathListLen;
        *error = FS32_ERR_SUCCESS;
    }
    return 1;
}

int filesys32_ListFolder(
        const h64wchar *path32, int64_t path32len,
        h64wchar ***contents, int64_t **contentslen,
        int returnFullPath, int *error
        ) {
    return filesys32_ListFolderEx(
        path32, path32len, contents, contentslen,
        returnFullPath, 1, error
    );
}

int filesys32_RemoveFolderRecursively(
        const h64wchar *path32, int64_t path32len, int *error
        ) {
    if (filesys32_IsObviouslyInvalidPath(path32, path32len)) {
        *error = FS32_ERR_TARGETNOTADIRECTORY;
        return 0;
    }

    int final_error = FS32_ERR_SUCCESS;
    const h64wchar *scan_next = path32;
    int64_t scan_next_len = path32len;
    int operror = 0;
    int64_t queue_scan_index = 0;
    h64wchar **removal_queue = NULL;
    int64_t *removal_queue_lens = NULL;
    int64_t queue_len = 0;

    h64wchar **contents = NULL;
    int64_t *contentslen = NULL;
    int firstitem = 1;
    while (1) {
        if (!scan_next) {
            firstitem = 0;
            if (queue_scan_index < queue_len) {
                scan_next = removal_queue[queue_scan_index];
                scan_next_len = removal_queue_lens[queue_scan_index];
                queue_scan_index++;
                continue;
            } else {
                break;
            }
        }
        assert(scan_next != NULL);
        int listingworked = filesys32_ListFolderEx(
            scan_next, scan_next_len, &contents, &contentslen, 1,
            0,  // fail on symlinks!!! (no delete-descend INTO those!!)
            &operror
        );
        if (!listingworked) {
            if (operror == FS32_ERR_TARGETNOTADIRECTORY ||
                    operror == FS32_ERR_SYMLINKSWEREEXCLUDED ||
                    (firstitem && operror ==
                        FS32_ERR_NOSUCHTARGET)) {
                // It's a file or symlink.
                // If it's a file, and this is our first item, error:
                if (firstitem && operror ==
                        FS32_ERR_TARGETNOTADIRECTORY) {
                    // We're advertising recursive DIRECTORY deletion,
                    // so resuming here would be unexpected.
                    *error = FS32_ERR_TARGETNOTADIRECTORY;
                    assert(!contents && queue_len == 0);
                    return 0;
                } else if (firstitem &&
                        operror == FS32_ERR_NOSUCHTARGET) {
                    *error = FS32_ERR_NOSUCHTARGET;
                    assert(!contents && queue_len == 0);
                    return 0;
                }
                // Instantly remove it instead:
                if (operror != FS32_ERR_NOSUCHTARGET &&
                        !filesys32_RemoveFileOrEmptyDir(
                        scan_next, scan_next_len, &operror
                        )) {
                    if (operror == FS32_ERR_NOSUCHTARGET) {
                        // Maybe it was removed in parallel?
                        // ignore this.
                    } else if (final_error == FS32_ERR_SUCCESS) {
                        // No error yet, so take this one.
                        final_error = operror;
                    }
                }
                if (queue_scan_index > 0 &&
                        queue_scan_index < queue_len &&
                        memcmp(
                            removal_queue[queue_scan_index - 1],
                            scan_next,
                            sizeof(*scan_next) * scan_next_len
                        ) == 0) {
                    free(removal_queue[queue_scan_index - 1]);
                    memmove(
                        &removal_queue[queue_scan_index - 1],
                        &removal_queue[queue_scan_index],
                        sizeof(*removal_queue) * (
                            queue_len - queue_scan_index
                        )
                    );
                    memmove(
                        &removal_queue_lens[queue_scan_index - 1],
                        &removal_queue_lens[queue_scan_index],
                        sizeof(*removal_queue_lens) * (
                            queue_len - queue_scan_index
                        )
                    );
                    queue_len--;
                    queue_scan_index--;
                }
                scan_next = NULL;
                firstitem = 0;
                continue;
            }
            // Another error. Consider it for returning at the end:
            if (final_error == FS32_ERR_SUCCESS) {
                // No error yet, so take this one.
                final_error = operror;
            }
        } else if (contents[0]) {  // one new item or more
            int64_t addc = 0;
            while (contents[addc])
                addc++;
            h64wchar **new_removal_queue = realloc(
                removal_queue, sizeof(*removal_queue) * (
                    queue_len + addc
                )
            );
            if (new_removal_queue)
                removal_queue = new_removal_queue;
            int64_t *new_removal_queue_lens = realloc(
                removal_queue_lens, sizeof(*removal_queue_lens) * (
                    queue_len + addc
                )
            );
            if (new_removal_queue_lens)
                removal_queue_lens = new_removal_queue_lens;
            if (!new_removal_queue || !new_removal_queue_lens) {
                *error = FS32_ERR_OUTOFMEMORY;
                int64_t k = 0;
                while (k < queue_len) {
                    free(removal_queue[k]);
                    k++;
                }
                free(removal_queue);
                free(removal_queue_lens);
                filesys32_FreeFolderList(contents, contentslen);
                contents = NULL;
                contentslen = 0;
                return 0;
            }
            memcpy(
                &removal_queue[queue_len],
                contents,
                sizeof(*contents) * addc
            );
            memcpy(
                &removal_queue_lens[queue_len],
                contentslen,
                sizeof(*contentslen) * addc
            );
            queue_len += addc;
            free(contents);  // we copied contents, so only free outer
            free(contentslen);
        } else {
            filesys32_FreeFolderList(contents, contentslen);
        }
        contents = NULL;
        contentslen = 0;
        scan_next = NULL;
        scan_next_len = 0;
        firstitem = 0;
    }
    // Now remove everything left in the queue, since we should have
    // gotten rid of all the files. However, there might still be nested
    // directories, so let's go through the queue BACKWARDS (from inner
    // to outer):
    int64_t k = queue_len - 1;
    while (k >= 0) {
        if (!filesys32_RemoveFileOrEmptyDir(
                removal_queue[k], removal_queue_lens[k],
                &operror
                )) {
            if (operror == FS32_ERR_NOSUCHTARGET) {
                // Maybe it was removed in parallel?
                // ignore this.
            } else if (final_error == FS32_ERR_SUCCESS) {
                // No error yet, so take this one.
                final_error = operror;
            }
        }
        free(removal_queue[k]);
        k--;
    }
    free(removal_queue);
    free(removal_queue_lens);
    if (!filesys32_RemoveFileOrEmptyDir(
            path32, path32len, &operror
            )) {
        if (operror == FS32_ERR_NOSUCHTARGET) {
            // Maybe it was removed in parallel?
            // ignore this.
        } else if (final_error == FS32_ERR_SUCCESS) {
            // No error yet, so take this one.
            final_error = operror;
        }
    }
    if (final_error != FS32_ERR_SUCCESS) {
        *error = final_error;
        return 0;
    }
    return 1;
}

int filesys32_ChangeDirectory(
        h64wchar *path, int64_t pathlen
        ) {
    if (filesys32_IsObviouslyInvalidPath(path, pathlen)) {
        return FS32_ERR_TARGETNOTADIRECTORY;
    }

    #if defined(_WIN32) || defined(_WIN64)
    wchar_t *targetpath = malloc(
        sizeof(*targetpath) * (pathlen * 2 + 1)
    );
    if (!targetpath) {
        return FS32_ERR_OUTOFMEMORY;
    }
    int64_t targetpathlen = 0;
    int uresult = utf32_to_utf16(
        path, pathlen, (char *)targetpath,
        sizeof(*targetpath) * (pathlen * 2 + 1),
        &targetpathlen, 1
    );
    if (!uresult || targetpathlen >= (pathlen * 2 + 1)) {
        free(targetpath);
        return FS32_ERR_OUTOFMEMORY;
    }
    targetpath[targetpathlen] = '\0';
    BOOL result = SetCurrentDirectoryW(targetpath);
    free(targetpath);
    if (result == FALSE) {
        uint32_t werror = GetLastError();
        if (werror == ERROR_PATH_NOT_FOUND ||
                werror == ERROR_FILE_NOT_FOUND ||
                werror == ERROR_INVALID_PARAMETER ||
                werror == ERROR_INVALID_NAME ||
                werror == ERROR_INVALID_DRIVE ||
                werror == ERROR_BAD_PATHNAME) {
            return FS32_ERR_TARGETNOTADIRECTORY;
        } else if (werror == ERROR_ACCESS_DENIED ||
                werror == ERROR_SHARING_VIOLATION) {
            return FS32_ERR_NOPERMISSION;
        } else if (werror == ERROR_NOT_ENOUGH_MEMORY) {
            return FS32_ERR_OUTOFMEMORY;
        }
        return FS32_ERR_OTHERERROR;
    }
    return 1;
    #else
    char *targetpath = malloc(
        sizeof(*targetpath) * (pathlen * 5 + 1)
    );
    if (!targetpath) {
        return FS32_ERR_OUTOFMEMORY;
    }
    int64_t targetpathlen = 0;
    int uresult = utf32_to_utf8(
        path, pathlen, (char *)targetpath,
        sizeof(*targetpath) * (pathlen * 2 + 1),
        &targetpathlen, 1, 0
    );
    if (!uresult || targetpathlen >= (pathlen * 5 + 1)) {
        free(targetpath);
        return FS32_ERR_OUTOFMEMORY;
    }
    targetpath[targetpathlen] = '\0';
    int statresult = chdir(targetpath);
    free(targetpath);
    if (statresult < 0) {
        if (errno == EACCES || errno == EPERM) {
            return FS32_ERR_NOPERMISSION;
        } else if (errno == ENOMEM) {
            return FS32_ERR_OUTOFMEMORY;
        } else if (errno == ENOENT) {
            return FS32_ERR_NOSUCHTARGET;
        } else if (errno == ENOTDIR) {
            return FS32_ERR_TARGETNOTADIRECTORY;
        }
        return FS32_ERR_OTHERERROR;
    }
    return 1;
    #endif
}

h64wchar *filesys32_ParentdirOfItem(
        const h64wchar *path, int64_t pathlen,
        int64_t *out_len
        ) {
    if (!path)
        return NULL;
    int64_t plen = 0;
    h64wchar *p = strdupu32(path, pathlen, &plen);
    if (!p)
        return NULL;

    // If this is already shortened to absolute path root, abort:
    #if defined(_WIN32) || defined(_WIN64)
    if (pathlen >= 2 && pathlen <= 3 &&
            path[1] == ':' && (pathlen == 2 ||
            path[2] == '/' || path[2] == '\\') &&
            ((path[0] >= 'a' && path[0] <= 'z') ||
              (path[0] >= 'A' && path[0] <= 'Z'))) {
        *out_len = plen;
        return p;
    }
    #else
    if (pathlen == 1 && path[0] == '/') {
        *out_len = plen;
        return p;
    }
    #endif

    // Strip trailing slash if any, then go back one component:
    #if defined(_WIN32) || defined(_WIN64)
    while (plen > 0 && (
            p[plen - 1] == '/' ||
            p[plen - 1] == '\\'))
        plen--;
    while (plen > 0 &&
            p[plen - 1] != '/' &&
            p[plen - 1] != '\\')
        plen--;
    #else
    while (plen > 0 &&
            p[plen - 1] == '/')
        plen--;
    while (plen> 0 &&
            p[plen - 1] != '/')
        plen--;
    #endif
    *out_len = plen;
    return p;
}

int filesys32_CreateDirectory(
        h64wchar *path, int64_t pathlen,
        int user_readable_only
        ) {
    if (filesys32_IsObviouslyInvalidPath(path, pathlen)) {
        return FS32_ERR_INVALIDNAME;
    }

    #if defined(_WIN32) || defined(_WIN64)
    wchar_t *targetpath = malloc(
        sizeof(*targetpath) * (pathlen * 2 + 1)
    );
    if (!targetpath) {
        return FS32_ERR_OUTOFMEMORY;
    }
    int64_t targetpathlen = 0;
    int uresult = utf32_to_utf16(
        path, pathlen, (char *)targetpath,
        sizeof(*targetpath) * (pathlen * 2 + 1),
        &targetpathlen, 1
    );
    if (!uresult || targetpathlen >= (pathlen * 2 + 1)) {
        free(targetpath);
        return FS32_ERR_OUTOFMEMORY;
    }
    targetpath[targetpathlen] = '\0';
    BOOL result = CreateDirectoryW(targetpath, NULL);
    free(targetpath);
    if (result == FALSE) {
        uint32_t werror = GetLastError();
        if (werror == ERROR_PATH_NOT_FOUND ||
                werror == ERROR_FILE_NOT_FOUND ||
                werror == ERROR_INVALID_PARAMETER ||
                werror == ERROR_INVALID_NAME ||
                werror == ERROR_INVALID_DRIVE) {
            return FS32_ERR_PARENTSDONTEXIST;
        } else if (werror == ERROR_ACCESS_DENIED ||
                werror == ERROR_SHARING_VIOLATION ||
                werror == ERROR_WRITE_PROTECT) {
            return FS32_ERR_NOPERMISSION;
        } else if (werror == ERROR_NOT_ENOUGH_MEMORY) {
            return FS32_ERR_OUTOFMEMORY;
        } else if (werror == ERROR_TOO_MANY_OPEN_FILES) {
            return FS32_ERR_OUTOFFDS;
        } else if (werror == ERROR_ALREADY_EXISTS) {
            return FS32_ERR_TARGETALREADYEXISTS;
        } else if (werror == ERROR_BAD_PATHNAME) {
            return FS32_ERR_INVALIDNAME;
        }
        return FS32_ERR_OTHERERROR;
    }
    return 1;
    #else
    char *targetpath = malloc(
        sizeof(*targetpath) * (pathlen * 5 + 1)
    );
    if (!targetpath) {
        return FS32_ERR_OUTOFMEMORY;
    }
    int64_t targetpathlen = 0;
    int uresult = utf32_to_utf8(
        path, pathlen, (char *)targetpath,
        sizeof(*targetpath) * (pathlen * 2 + 1),
        &targetpathlen, 1, 0
    );
    if (!uresult || targetpathlen >= (pathlen * 5 + 1)) {
        free(targetpath);
        return FS32_ERR_OUTOFMEMORY;
    }
    targetpath[targetpathlen] = '\0';
    int statresult = (
        mkdir(targetpath,
              (user_readable_only ? 0700 : 0775)) == 0);
    free(targetpath);
    if (!statresult) {
        if (errno == EACCES || errno == EPERM) {
            return FS32_ERR_NOPERMISSION;
        } else if (errno == EMFILE || errno == ENFILE) {
            return FS32_ERR_OUTOFFDS;
        } else if (errno == ENOMEM) {
            return FS32_ERR_OUTOFMEMORY;
        } else if (errno == ENOENT) {
            return FS32_ERR_PARENTSDONTEXIST;
        } else if (errno == EEXIST) {
            return FS32_ERR_TARGETALREADYEXISTS;
        }
        return FS32_ERR_OTHERERROR;
    }
    return FS32_ERR_SUCCESS;
    #endif
}

int filesys32_TargetExists(
        const h64wchar *path, int64_t pathlen, int *result
        ) {
    if (filesys32_IsObviouslyInvalidPath(path, pathlen)) {
        *result = 0;
        return 0;
    }

    // Hack for "" referring to cwd:
    const h64wchar _cwdbuf[] = {'.'};
    if (pathlen == 0) {
        path = _cwdbuf;
        pathlen = 1;
    }

    #if defined(_WIN32) || defined(_WIN64)
    wchar_t *targetpath = malloc(
        sizeof(*targetpath) * (pathlen * 2 + 1)
    );
    if (!targetpath) {
        *result = 0;
        return 0;
    }
    int64_t targetpathlen = 0;
    int uresult = utf32_to_utf16(
        path, pathlen, (char *)targetpath,
        sizeof(*targetpath) * (pathlen * 2 + 1),
        &targetpathlen, 1
    );
    if (!uresult || targetpathlen >= (pathlen * 2 + 1)) {
        free(targetpath);
        *result = 0;
        return 0;
    }
    targetpath[targetpathlen] = '\0';
    DWORD dwAttrib = GetFileAttributesW(targetpath);
    free(targetpath);
    if (dwAttrib == INVALID_FILE_ATTRIBUTES) {
        uint32_t werror = GetLastError();
        if (werror == ERROR_PATH_NOT_FOUND ||
                werror == ERROR_FILE_NOT_FOUND ||
                werror == ERROR_INVALID_PARAMETER ||
                werror == ERROR_INVALID_NAME ||
                werror == ERROR_INVALID_DRIVE ||
                werror == ERROR_ACCESS_DENIED) {
            *result = 0;
            return 1;
        } else if (werror == ERROR_NOT_ENOUGH_MEMORY ||
                werror == ERROR_TOO_MANY_OPEN_FILES ||
                werror == ERROR_PATH_BUSY ||
                werror == ERROR_BUSY ||
                werror == ERROR_SHARING_VIOLATION) {
            *result = 0;
            return 0;  // unexpected I/O error!
        }
        *result = 0;
        return 1;
    }
    return (dwAttrib != INVALID_FILE_ATTRIBUTES);
    #else
    char *targetpath = malloc(
        sizeof(*targetpath) * (pathlen * 5 + 1)
    );
    if (!targetpath) {
        *result = 0;
        return 0;
    }
    int64_t targetpathlen = 0;
    int uresult = utf32_to_utf8(
        path, pathlen, (char *)targetpath,
        sizeof(*targetpath) * (pathlen * 2 + 1),
        &targetpathlen, 1, 0
    );
    if (!uresult || targetpathlen >= (pathlen * 5 + 1)) {
        free(targetpath);
        *result = 0;
        return 0;
    }
    targetpath[targetpathlen] = '\0';
    struct stat64 sb = {0};
    int statresult = (stat64(targetpath, &sb) == 0);
    free(targetpath);
    if (!statresult) {
        if (errno == EACCES || errno == EPERM ||
                errno == EMFILE || errno == ENFILE ||
                errno == ENOMEM) {
            *result = 0;
            return 0;  // unexpected I/O error!
        }
    }
    *result = statresult;
    return 1;
    #endif
}

h64wchar *filesys_Basename(
        const h64wchar *path, int64_t pathlen, int64_t *out_len
        ) {
    int i = 0;
    while (i < pathlen &&
            path[pathlen - i - 1] != '/'
            #if defined(_WIN32) || defined(_WIN64)
            &&
            path[pathlen - i - 1] != '\\'
            #endif
            )
        i++;
    if (i == pathlen)
        i = 0;
    h64wchar *result = malloc(
        sizeof(*path) * (pathlen - i > 0 ? pathlen - i : 1)
    );
    if (!result)
        return result;
    memcpy(
        result, path + i,
        sizeof(*path) * (pathlen - i)
    );
    *out_len = (pathlen - i);
    return result;
}

h64wchar *filesys32_Dirname(
        const h64wchar *path, int64_t pathlen, int64_t *out_len
        ) {
    if (!path)
        return NULL;
    int64_t slen = 0;
    h64wchar *s = strdupu32(path, pathlen, &slen);
    if (!s)
        return NULL;
    int cutoffdone = 0;
    int evernotslash = 0;
    int64_t i = slen - 1;
    while (i >= 0) {
        if (evernotslash && (s[i] == '/'
                #if defined(_WIN32) || defined(_WIN64)
                || s[i] == '\\'
                #endif
                )) {
            slen = i;
            i--;
            while (i >= 0 &&
                    (s[i] == '/'
                    #if defined(_WIN32) || defined(_WIN64)
                    || s[i] == '\\'
                    #endif
                    )) {
                slen = i;
                i--;
            }
            cutoffdone = 1;
            break;
        } else if (s[i] != '/'
                #if defined(_WIN32) || defined(_WIN64)
                && s[i] != '\\'
                #endif
                ) {
            evernotslash = 1;
        }
        i--;
    }
    if (!cutoffdone) slen = 0;
    *out_len = slen;
    return s;
}

h64wchar *filesys32_RemoveDoubleSlashes(
        const h64wchar *path, int64_t pathlen,
        int couldbewinpath, int64_t *out_len
        ) {
    // MAINTENANCE NOTE: KEEP IN SYNC WITH filesys_RemoveDoubleSlashes()!!!

    if (!path)
        return NULL;
    if (pathlen <= 0) {
        if (out_len) *out_len = 0;
        return malloc(1);
    }
    h64wchar *p = malloc(sizeof(*path) * pathlen);
    if (!p)
        return NULL;
    memcpy(p, path, sizeof(*p) * pathlen);
    int64_t plen = pathlen;

    // Remove double slashes:
    int lastwassep = 0;
    int64_t i = 0;
    while (i < plen) {
        if (p[i] == '/'
                || (couldbewinpath && p[i] == '\\')
                ) {
            #if defined(_WIN32) || defined(_WIN64)
            p[i] = '\\';
            #else
            p[i] = '/';
            #endif
            if (!lastwassep) {
                lastwassep = 1;
            } else {
                memmove(
                    p + i, p + i + 1,
                    (plen - i - 1) * sizeof(*path)
                );
                plen--;
                continue;
            }
        } else {
            lastwassep = 0;
        }
        i++;
    }
    if (plen > 1 && (
            p[plen - 1] == '/'
            || (couldbewinpath && p[plen - 1] == '\\')
            )) {
        plen--;
    }

    if (out_len) *out_len = plen;
    return p;
}

h64wchar *filesys32_NormalizeEx(
        const h64wchar *path, int64_t pathlen, int couldbewinpath,
        int64_t *out_len
        ) {
    // MAINTENANCE NOTE: KEEP THIS IN SINC WITH filesys_Normalize()!!!

    if (couldbewinpath == -1) {
        #if defined(_WIN32) || defined(_WIN64)
        couldbewinpath = 1;
        #else
        couldbewinpath = 0;
        #endif
    }

    int64_t resultlen = 0;
    h64wchar *result = filesys32_RemoveDoubleSlashes(
        path, pathlen, couldbewinpath, &resultlen
    );
    if (!result)
        return NULL;

    // Remove all unnecessary ../ and ./ inside the path:
    int last_component_start = -1;
    int64_t i = 0;
    while (i < resultlen) {
        if ((result[i] == '/'
                || (couldbewinpath && result[i] == '\\')
                ) && i + 2 < resultlen &&
                result[i + 1] == '.' &&
                result[i + 2] == '.' && (
                i + 3 >= resultlen ||
                result[i + 3] == '/' ||
                (couldbewinpath && result[i + 3] == '\\')
                ) && i > last_component_start && i > 0 &&
                (result[last_component_start + 1] != '.' ||
                 result[last_component_start + 2] != '.' ||
                 (result[last_component_start + 3] != '/' &&
                  (!couldbewinpath ||
                   result[last_component_start + 3] != '\\')
                 )
                )) {
            // Collapse ../ into previous component:
            int movelen = 4;
            if (i + 3 >= resultlen)
                movelen = 3;
            memmove(
                result + last_component_start + 1,
                result + (i + movelen),
                sizeof(*result) * (resultlen - (i + movelen))
            );
            resultlen -= ((i + movelen) - (last_component_start + 1));
            // Start over from beginning:
            i = 0;
            last_component_start = 0;
            continue;
        } else if ((result[i] == '/' ||
                (couldbewinpath && result[i] == '\\')
                ) && result[i + 1] == '.' && (
                result[i + 2] == '/' ||
                (couldbewinpath && result[i + 2] == '\\')
                )) {
            // Collapse unncessary ./ away:
            last_component_start = i;
            memmove(
                result + i, result + (i + 2),
                sizeof(*result) * (resultlen - (i - 2))
            );
            resultlen -= 2;
            continue;
        } else if (result[i] == '/'
                || (couldbewinpath && result[i] == '\\')
                ) {
            last_component_start = i;
            // Collapse all double slashes away:
            while (result[i + 1] == '/' ||
                    (couldbewinpath && result[i + 1] == '\\')
                    ) {
                memmove(
                    result + i, result + (i + 1),
                    sizeof(*result) * (resultlen - (i - 1))
                );
                resultlen--;
            }
        }
        i++;
    }

    // Remove leading ./ instances:
    while (resultlen >= 2 && result[0] == '.' && (
            result[1] == '/' ||
            (couldbewinpath && result[1] == '\\')
            )) {
        memmove(
            result, result + 2,
            sizeof(*result) * (resultlen - 2)
        );
        resultlen -= 2;
    }

    // Unify path separators:
    i = 0;
    while (i < resultlen) {
        if (result[i] == '/' ||
                (couldbewinpath && result[i] == '\\')
                ) {
            if (couldbewinpath) {
                #if defined(_WIN32) || defined(_WIN64)
                result[i] = '\\';
                #else
                result[i] = '/';
                #endif
            } else {
                result[i] = '/';
            }
        }
        i++;
    }

    // Remove trailing path separators:
    while (resultlen > 0) {
        if (result[resultlen - 1] == '/' ||
                result[resultlen - 1] == '\\'
                ) {
            resultlen--;
        } else {
            break;
        }
    }
    if (out_len) *out_len = resultlen;
    return result;
}

h64wchar *filesys32_Normalize(
        const h64wchar *path, int64_t pathlen,
        int64_t *out_len
        ) {
    return filesys32_NormalizeEx(
        path, pathlen, -1, out_len
    );
}

h64wchar *filesys32_ToAbsolutePath(
        const h64wchar *path, int64_t pathlen,
        int64_t *out_len    
        ) {
    if (filesys32_IsAbsolutePath(path, pathlen)) {
        h64wchar *result = malloc(
            (pathlen > 0 ? pathlen : 1) * sizeof(*path)
        );
        if (result) {
            memcpy(result, path, sizeof(*path) * pathlen);
            *out_len = pathlen;
        } else {
            *out_len = 0;
        }
        return result;
    }
    int64_t cwdlen = 0;
    h64wchar *cwd = filesys32_GetCurrentDirectory(&cwdlen);
    if (!cwd)
        return NULL;
    int64_t resultlen = 0;
    h64wchar *result = filesys32_Join(
        cwd, cwdlen, path, pathlen, &resultlen
    );
    free(cwd);
    if (out_len) *out_len = resultlen;
    return result;
}

int filesys32_AssumeCaseSensitiveHostFS() {
    #if defined(_WIN32) || defined(_WIN64)
    return 1;
    #else
    #if defined(__APPLE__)
    return 1;
    #endif
    #endif
    return 0;
}

int filesys32_WinApiInsensitiveCompare(
        ATTR_UNUSED const h64wchar *path1_o,
        ATTR_UNUSED int64_t path1len_o,
        ATTR_UNUSED const h64wchar *path2_o,
        ATTR_UNUSED int64_t path2len_o,
        int *wasoom
        ) {
    #if defined(_WIN32) || defined(_WIN64)
    uint16_t *path1 = NULL;
    uint16_t *path2 = NULL;
    path1 = malloc(
        sizeof(*path1) * (path1len_o * 2 + 1)
    );
    if (!path1) {
        oom:
        free(path1);
        free(path2);
        if (wasoom) *wasoom = 1;
        return 0;
    }
    path2 = malloc(
        sizeof(*path2) * (path2len_o * 2 + 1)
    );
    assert(
        sizeof(*path1) == sizeof(wchar_t)
        // should be true for windows
    );
    if (!path2)
        goto oom;
    int64_t path2len = 0;
    int64_t path1len = 0;
    int result1 = utf32_to_utf16(
        path1_o, path1len_o, (char *)path1,
        path1len_o * 2 * sizeof(*path1),
        &path1len, 1
    );
    int result2 = utf32_to_utf16(
        path2_o, path2len_o, (char *)path2,
        path2len_o * 2 * sizeof(*path2),
        &path2len, 1
    );
    if (!result1 || !result2) {
        // This shouldn't happen. But we'd rather not crash here.
        goto oom;
    }
    if (path1len != path2len) {
        free(path1);
        free(path2);
        if (wasoom) *wasoom = 0;
        return 0;
    }
    path1[path1len] = '\0';
    path2[path2len] = '\0';
    CharUpperW((wchar_t *)path1);  // winapi case folding.
    CharUpperW((wchar_t *)path2);
    if (memcmp(path1, path2, path1len * sizeof(*path1)) == 0) {
        free(path1);
        free(path2);
        if (wasoom) *wasoom = 0;
        return 1;
    }
    free(path1);
    free(path2);
    if (wasoom) *wasoom = 0;
    return 0;
    #else
    if (wasoom) *wasoom = 1;
    return 0;
    #endif
}

int filesys32_PathCompare(
        const h64wchar *p1, int64_t p1len,
        const h64wchar *p2, int64_t p2len
        ) {
    int64_t p1normalizedlen = 0;
    h64wchar *p1normalized = filesys32_Normalize(
        p1, p1len, &p1normalizedlen
    );
    int64_t p2normalizedlen = 0;
    h64wchar *p2normalized = filesys32_Normalize(
        p2, p2len, &p2normalizedlen
    );
    if (!p1normalized || !p2normalized) {
        free(p1normalized);
        free(p2normalized);
        return -1;
    }
    int result = 0;
    #if defined(_WIN32) || defined(_WIN64)
    if (filesys32_AssumeCaseSensitiveHostFS()) {
        int wasoom = 0;
        result = (filesys32_WinApiInsensitiveCompare(
            p1normalized, p1normalizedlen,
            p2normalized, p2normalizedlen, &wasoom
        ));
        free(p1normalized);
        free(p2normalized);
        if (!result && wasoom)
            return -1;
        return result;
    }
    #else
    result = (memcmp(
        p1normalized, p2normalized,
        p1normalizedlen * sizeof(*p1normalized)
    ) == 0);
    free(p1normalized);
    free(p2normalized);
    #endif
    return result;
}

char *_unix_getcwd() {
    #if defined(_WIN32) || defined(_WIN64)
    return NULL;
    #else
    int allocsize = 32;
    while (1) {
        allocsize *= 2;
        char *s = malloc(allocsize);
        if (!s)
            return NULL;
        char *result = getcwd(s, allocsize - 1);
        if (result == NULL) {
            free(s);
            if (errno == ERANGE) {
                continue;
            }
            return NULL;
        }
        s[allocsize - 1] = '\0';
        return s;
    }
    #endif
}

h64wchar *filesys32_GetCurrentDirectory(int64_t *out_len) {
    #if defined(_WIN32) || defined(_WIN64)
    assert(sizeof(wchar_t) == sizeof(uint16_t));  // winapi specific
    DWORD size = GetCurrentDirectoryW(0, NULL);
    uint16_t *s = malloc(size * sizeof(*s) + 1);
    if (!s)
        return NULL;
    if (GetCurrentDirectoryW(size, (wchar_t *)s) != 0) {
        s[size - 1] = '\0';
    } else {
        free(s);
        return NULL;
    }
    int hadoom = 0;
    int64_t resultlen = 0;
    h64wchar *result = utf16_to_utf32(
        s, size, &resultlen, 1, &hadoom
    );
    free(s);
    if (!result)
        return NULL;
    if (out_len) *out_len = resultlen;
    return result;
    #else
    char *cwd = _unix_getcwd();
    if (!cwd)
        return NULL;
    int wasinvalid = 0;
    int wasoom = 0;
    int64_t resultlen = 0;
    h64wchar *result = utf8_to_utf32_ex(
        cwd, strlen(cwd), NULL, 0, NULL, NULL, &resultlen,
        1, 0, &wasinvalid, &wasoom
    );
    free(cwd);
    if (result)
        if (out_len) *out_len = resultlen;
    return result;
    #endif
}

h64wchar *filesys32_Join(
        const h64wchar *path1, int64_t path1len,
        const h64wchar *path2_orig, int64_t path2_origlen,
        int64_t *out_len
        ) {
    // Quick result paths:
    if (!path1 || !path2_orig)
        return NULL;
    if ((path2_origlen == 1 && path2_orig[0] == '.') ||
            path2_origlen == 0) {
        returnfirst: ;
        h64wchar *result = malloc(
            (path1len > 0 ? path1len : 1) * sizeof(*path1)
        );
        if (result) {
            memcpy(result, path1, sizeof(*path1) * path1len);
            if (out_len)
                *out_len = path1len;
        }
        return result;
    }

    // Clean up path2 for merging:
    int64_t path2len = path2_origlen;
    h64wchar *path2 = malloc(
        sizeof(*path2_orig) * (
            path2_origlen > 0 ? path2_origlen : 1
        )
    );
    if (!path2)
        return NULL;
    memcpy(path2, path2_orig, sizeof(*path2_orig) * path2_origlen);
    while (path2len >= 2 && path2[0] == '.' &&
            (path2[1] == '/'
            #if defined(_WIN32) || defined(_WIN64)
            || path2[1] == '\\'
            #endif
            )) {
        memmove(
            path2, path2 + 2,
            (path2len - 2) * sizeof(*path2)
        );
        if (path2len == 0 || (path2len == 1 && path2[0] == '.')) {
            free(path2);
            goto returnfirst;
        }
    }

    // Do actual merging:
    h64wchar *presult = malloc(
        (path1len + 1 + path2len) * sizeof(*path1)
    );
    int64_t presultlen = 0;
    if (!presult) {
        free(path2);
        return NULL;
    }
    if (path1len > 0)
        memcpy(
            presult, path1, path1len * sizeof(*path1)
        );
    presultlen = path1len;
    if (path1len > 0) {
        #if defined(_WIN32) || defined(_WIN64)
        if (path1[path1len - 1] != '\\' &&
                path1[path1len - 1] != '/' &&
                (path2len == 0 || path2[0] != '\\' ||
                 path2[0] != '/')) {
            presult[presultlen] = '\\';
            presultlen++;
        }
        #else
        if ((path1[path1len - 1] != '/') &&
                (path2len == 0 || path2[0] != '/')) {
            presult[presultlen] = '/';
            presultlen++;
        }
        #endif
        memcpy(
            presult + presultlen, path2,
            sizeof(*path2) * path2len
        );
        presultlen += path2len;
    } else {
        #if defined(_WIN32) || defined(_WIN64)
        if (path2len > 0 && (
                path2[0] == '/' ||
                path2[0] == '\\')) {
            memcpy(
                presult + presultlen,
                path2 + 1, sizeof(*path2) * (path2len - 1)
            );
            presultlen += (path2len - 1);
        } else {
            memcpy(
                presult + presultlen,
                path2, sizeof(*path2) * path2len
            );
            presultlen += path2len;
        }
        #else
        if (path2len > 0 && path2[0] == '/') {
            memcpy(
                presult + presultlen,
                path2 + 1, sizeof(*path2) * (path2len - 1)
            );
            presultlen += (path2len - 1);
        } else if (path2len > 0) {
            memcpy(
                presult + presultlen,
                path2, sizeof(*path2) * path2len
            );
            presultlen += path2len;
        }
        #endif
    }
    free(path2);  // this was a mutable copy of ours
    if (out_len) *out_len = presultlen;
    return presult;
}

int filesys32_IsAbsolutePath(
        const h64wchar *path, int64_t pathlen
        ) {
    if (pathlen == 0)
        return 0;
    if (path[0] == '.')
        return 0;
    #if (!defined(_WIN32) && !defined(_WIN64))
    if (path[0] == '/')
        return 1;
    #endif
    #if defined(_WIN32) || defined(_WIN64)
    if (pathlen > 2 && (
            path[1] == ':' || path[1] == '\\'))
        return 1;
    #endif
    return 0;
}

h64wchar *filesys32_GetSysTempdir(int64_t *output_len) {
    #if defined(_WIN32) || defined(_WIN64)
    int tempbufwsize = 512;
    wchar_t *tempbufw = malloc(tempbufwsize * sizeof(wchar_t));
    assert(
        sizeof(wchar_t) == sizeof(uint16_t)
        // should be true for windows
    );
    if (!tempbufw)
        return NULL;
    unsigned int rval = 0;
    while (1) {
        rval = GetTempPathW(
            tempbufwsize - 1, tempbufw
        );
        if (rval >= (unsigned int)tempbufwsize - 2) {
            tempbufwsize *= 2;
            free(tempbufw);
            tempbufw = malloc(tempbufwsize * sizeof(wchar_t));
            if (!tempbufw)
                return NULL;
            continue;
        }
        if (rval == 0)
            return NULL;
        tempbufw[rval] = '\0';
        break;
    }
    assert(wcslen(tempbufw) < (uint64_t)tempbufwsize - 2);
    if (tempbufw[wcslen(tempbufw) - 1] != '\\') {
        tempbufw[wcslen(tempbufw) + 1] = '\0';
        tempbufw[wcslen(tempbufw)] = '\\';
    }
    int _wasoom = 0;
    int64_t tempbuffill = 0;
    h64wchar *tempbuf = utf16_to_utf32(
        tempbufw, wcslen(tempbufw),
        &tempbuffill, 1, &_wasoom
    );
    free(tempbufw);
    if (!tempbuf)
        return NULL;
    *output_len = tempbuffill;
    return tempbuf;
    #else
    char *tempbufu8 = NULL;
    tempbufu8 = strdup("/tmp/");
    if (!tempbufu8) {
        return NULL;
    }
    int64_t tempbuffill = 0;
    h64wchar *tempbuf = AS_U32(tempbufu8, &tempbuffill);
    free(tempbufu8);
    if (!tempbuf)
        return NULL;
    *output_len = tempbuffill;
    return tempbuf;
    #endif
}


FILE *_filesys32_TempFile_SingleTry(
        int subfolder, int folderonly,
        const h64wchar *prefix, int64_t prefixlen,
        const h64wchar *suffix, int64_t suffixlen,
        h64wchar **folder_path, int64_t* folder_path_len,
        h64wchar **path, int64_t *path_len,
        int *do_retry
        ) {
    assert(!folderonly || subfolder);
    *do_retry = 0;
    *path = NULL;
    *path_len = 0;
    *folder_path = NULL;
    *folder_path_len = 0;

    // Get the folder path for system temp location:
    int64_t tempbuffill = 0;
    h64wchar *tempbuf = filesys32_GetSysTempdir(&tempbuffill);
    if (!tempbuf)
        return NULL;

    // Random bytes we use in the name:
    uint64_t v[4];
    if (!secrandom_GetBytes(
            (char*)&v, sizeof(v)
            )) {
        free(tempbuf);
        return NULL;
    }

    // The secure random part to be inserted as a string:
    char randomu8[512];
    snprintf(
        randomu8, sizeof(randomu8) - 1,
        "%" PRIu64 "%" PRIu64 "%" PRIu64 "%" PRIu64,
        v[0], v[1], v[2], v[3]
    );
    randomu8[
        sizeof(randomu8) - 1
    ] = '\0';
    int64_t randomu32len = 0;
    h64wchar *randomu32 = AS_U32(randomu8, &randomu32len);
    if (!randomu32) {
        free(tempbuf);
        return NULL;
    }
    int64_t combined_path_len = 0;
    h64wchar *combined_path = NULL;
    if (subfolder) {  // Create the subfolder:
        combined_path_len = (
            tempbuffill + randomu32len + 1
        );
        combined_path = malloc(
            sizeof(*combined_path) * (tempbuffill +
            prefixlen + randomu32len + 1)
        );
        if (!combined_path) {
            free(tempbuf);
            free(randomu32);
            return NULL;
        }
        memcpy(combined_path, tempbuf,
               sizeof(*combined_path) * tempbuffill);
        if (prefixlen > 0)
            memcpy(combined_path + tempbuffill, prefix,
               sizeof(*randomu32) * prefixlen);
        memcpy(combined_path + tempbuffill + prefixlen, randomu32,
               sizeof(*randomu32) * randomu32len);
        #if defined(_WIN32) || defined(_WIN64)
        combined_path[tempbuffill + randomu32len] = '\\';
        #else
        combined_path[tempbuffill + randomu32len] = '/';
        #endif
        free(tempbuf);
        tempbuf = NULL;

        int _mkdirerror = 0;
        if ((_mkdirerror = filesys32_CreateDirectory(
                combined_path, combined_path_len, 1
                )) < 0) {
            if (_mkdirerror == FS32_ERR_TARGETALREADYEXISTS) {
                // Oops, somebody was faster/we hit an existing
                // folder out of pure luck. Retry.
                *do_retry = 1;
            }
            free(combined_path);
            free(randomu32);
            return NULL;
        }
        *folder_path = combined_path;
        *folder_path_len = combined_path_len;
        if (folderonly) {
            *path = NULL;
            *path_len = 0;
            free(randomu32);
            return NULL;
        }
    } else {
        combined_path_len = tempbuffill;
        combined_path = tempbuf;
        tempbuf = NULL;
    }

    // Compose the path to the file to create:
    h64wchar *file_path = malloc(sizeof(*file_path) * (
        combined_path_len + (prefix ? prefixlen : 0) +
        randomu32len + (suffix ? suffixlen : 0) + 1
    ));
    int64_t file_path_len = 0;
    if (!file_path) {
        int error = 0;
        if (subfolder) {
            filesys32_RemoveFileOrEmptyDir(
                *folder_path, *folder_path_len, &error
            );
        }
        free(combined_path);
        free(randomu32);
        *folder_path = NULL;  // already free'd through combined_path
        *folder_path_len = 0;
        return NULL;
    }
    memcpy(
        file_path, combined_path,
        sizeof(*file_path) * combined_path_len
    );
    if (prefix && prefixlen > 0)
        memcpy(
            file_path + combined_path_len, prefix,
            sizeof(*prefix) * prefixlen
        );
    memcpy(
        file_path + combined_path_len + (prefix ? prefixlen : 0),
        randomu32, sizeof(*randomu32) * randomu32len
    );
    if (suffix && suffixlen > 0)
        memcpy(
            file_path + combined_path_len +
            (prefix ? prefixlen : 0) + randomu32len,
            suffix, sizeof(*suffix) * suffixlen
        );
    file_path_len = (
        combined_path_len +
        (prefix ? prefixlen : 0) + randomu32len +
        (suffix ? suffixlen : 0)
    );
    if (!subfolder) {
        // (this is otherwise set as *folder_path)
        free(combined_path);
    }
    combined_path = NULL;
    free(randomu32);

    // Create file and return result:
    int _innererr = 0;
    FILE *f = filesys32_OpenFromPath(
        file_path, file_path_len, "wb", &_innererr
    );
    if (!f) {
        if (subfolder) {
            int error = 0;
            filesys32_RemoveFolderRecursively(
                *folder_path, *folder_path_len, &error
            );
            free(*folder_path);
            *folder_path = NULL;
            *folder_path_len = 0;
        }
        free(file_path);
        return NULL;
    }
    *path = file_path;
    *path_len = file_path_len;
    return f;
}

FILE *filesys32_TempFile(
        int subfolder, int folderonly,
        const h64wchar *prefix, int64_t prefixlen,
        const h64wchar *suffix, int64_t suffixlen,
        h64wchar **folder_path, int64_t* folder_path_len,
        h64wchar **path, int64_t *path_len
        ) {
    while (1) {
        int retry = 0;
        FILE *f = _filesys32_TempFile_SingleTry(
            subfolder, folderonly, prefix, prefixlen,
            suffix, suffixlen, folder_path,
            folder_path_len,
            path, path_len, &retry
        );
        if (f)
            return f;
        if (subfolder && folderonly && *folder_path != NULL)
            return NULL;
        if (!f && !retry) {
            *folder_path = NULL;
            *path = NULL;
            return NULL;
        }
    }
}


int filesys32_GetComponentCount(
        const h64wchar *path, int64_t pathlen
        ) {
    int i = 0;
    int component_count = 0;
    #if defined(_WIN32) || defined(_WIN64)
    if (path[0] != '/' && path[0] != '\\' &&
            path[1] == ':' && (path[2] == '/' ||
            path[2] == '\\'))
        i = 2;
    #endif
    while (i < pathlen) {
        if (i > 0 && path[i] != '/' &&
                #if defined(_WIN32) || defined(_WIN64)
                path[i] != '\\' &&
                #endif
                (path[i - 1] == '/'
                #if defined(_WIN32) || defined(_WIN64)
                || path[i - 1] == '\\'
                #endif
                )) {
            component_count++;
        }
        i++;
    }
    return component_count;
}

h64filehandle filesys32_OpenFromPathAsOSHandle(
        const h64wchar *pathu32, int64_t pathu32len,
        const char *mode, int *err
        ) {
    return filesys32_OpenFromPathAsOSHandleEx(
        pathu32, pathu32len, mode, 0, err
    );
}

#if defined(_WIN32) || defined(_WIN64)
int _check_if_symlink_or_junction(HANDLE fhandle) {
    DWORD written = 0;
    char reparse_buf[MAXIMUM_REPARSE_DATA_BUFFER_SIZE] = {0};
    struct reparse_tag {
        ULONG tag;
    };
    if (!DeviceIoControl(
            fhandle, FSCTL_GET_REPARSE_POINT, NULL, 0,
            (LPVOID)reparse_buf, sizeof(reparse_buf),
            &written, 0
            )) {
        // Assume there is no reparse point.
    } else {
        struct reparse_tag *tagcheck = (
            (struct reparse_tag *)reparse_buf
        );
        if (tagcheck->tag == IO_REPARSE_TAG_SYMLINK ||
                tagcheck->tag == IO_REPARSE_TAG_MOUNT_POINT) {
            CloseHandle(fhandle);
            return 1;
        }
    }
    return 0;
}
#endif

h64filehandle filesys32_OpenFromPathAsOSHandleEx(
        const h64wchar *pathu32, int64_t pathu32len,
        const char *mode, int flags, int *err
        ) {
    if (filesys32_IsObviouslyInvalidPath(pathu32, pathu32len)) {
        *err = FS32_ERR_NOSUCHTARGET;
        return H64_NOFILE;
    }

    // Hack for "" referring to cwd:
    const h64wchar _cwdbuf[] = {'.'};
    if (pathu32len == 0) {
        pathu32 = _cwdbuf;
        pathu32len = 1;
    }

    int mode_read = (
        strstr(mode, "r") || strstr(mode, "a") || strstr(mode, "w+")
    );
    int mode_write = (
        strstr(mode, "w") || strstr(mode, "a") || strstr(mode, "r+")
    );
    int mode_append = strstr(mode, "r+") || strstr(mode, "a");

    #if defined(_WIN32) || defined(_WIN64)
    assert(sizeof(uint16_t) == sizeof(wchar_t));
    uint16_t *pathw = malloc(
        sizeof(uint16_t) * (pathu32len * 2 + 3)
    );
    if (!pathw) {
        *err = FS32_ERR_OUTOFMEMORY;
        return H64_NOFILE;
    }
    int64_t out_len = 0;
    int result = utf32_to_utf16(
        pathu32, pathu32len, (char *) pathw,
        sizeof(uint16_t) * (pathu32len * 2 + 3),
        &out_len, 1
    );
    if (!result || (uint64_t)out_len >=
            (uint64_t)(pathu32len * 2 + 3)) {
        free(pathw);
        *err = FS32_ERR_OUTOFMEMORY;
        return H64_NOFILE;
    }
    pathw[out_len] = '\0';
    HANDLE fhandle = INVALID_HANDLE_VALUE;
    if (!mode_write && !mode_append &&
            (flags & _WIN32_OPEN_DIR) != 0) {
        fhandle = CreateFileW(
            pathw,
            GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS |
             // ^ first try: this is needed for dirs
            ((flags & (OPEN_ONLY_IF_NOT_LINK | _WIN32_OPEN_LINK_ITSELF)) ?
             FILE_FLAG_OPEN_REPARSE_POINT : 0),
            0
        );
        if (fhandle != INVALID_HANDLE_VALUE) {
            // If this is not a directory, throw away handle again:
            BY_HANDLE_FILE_INFORMATION finfo = {0};
            if (!GetFileInformationByHandle(fhandle, &finfo)) {
                uint32_t werror = GetLastError();
                *err = FS32_ERR_OTHERERROR;
                if (werror == ERROR_ACCESS_DENIED ||
                        werror == ERROR_SHARING_VIOLATION)
                    *err = FS32_ERR_NOPERMISSION;
                return H64_NOFILE;
            }
            if ((finfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                // Oops, not a dir. We shouldn't open this like we have.
                // (We want to reopen without FILE_FLAG_BACKUP_SEMANTICS)
                CloseHandle(fhandle);
                fhandle = INVALID_HANDLE_VALUE;
            }
        }
    }
    if (!fhandle) {
        fhandle = CreateFileW(
            (LPCWSTR)pathw,
            0 | (mode_read ? GENERIC_READ : 0)
            | (mode_write ? GENERIC_WRITE : 0),
            (mode_write ? 0 : FILE_SHARE_READ),
            NULL,
            ((mode_write && !mode_append) ?
                TRUNCATE_EXISTING : OPEN_EXISTING) |
            ((mode_write && !mode_append) ? CREATE_NEW : 0),
            ((flags & (OPEN_ONLY_IF_NOT_LINK | _WIN32_OPEN_LINK_ITSELF)) ?
            FILE_FLAG_OPEN_REPARSE_POINT : 0),
            NULL
        );
    }
    free(pathw);
    pathw = NULL;
    if (fhandle == INVALID_HANDLE_VALUE) {
        uint32_t werror = GetLastError();
        if (werror == ERROR_SHARING_VIOLATION ||
                werror == ERROR_ACCESS_DENIED) {
            *err = FS32_ERR_NOPERMISSION;
        } else if (werror== ERROR_PATH_NOT_FOUND ||
                werror == ERROR_FILE_NOT_FOUND) {
            *err = FS32_ERR_NOSUCHTARGET;
        } else if (werror == ERROR_TOO_MANY_OPEN_FILES) {
            *err = FS32_ERR_OUTOFFDS;
        } else if (werror == ERROR_INVALID_NAME ||
                werror == ERROR_LABEL_TOO_LONG ||
                werror == ERROR_BUFFER_OVERFLOW ||
                werror == ERROR_FILENAME_EXCED_RANGE
                ) {
            *err = FS32_ERR_INVALIDNAME;
        } else {
            *err = FS32_ERR_OTHERERROR;
        }
        return H64_NOFILE;
    }
    if ((flags & OPEN_ONLY_IF_NOT_LINK) != 0) {
        if (_check_if_symlink_or_junction(fhandle)) {
            *err = FS32_ERR_SYMLINKSWEREEXCLUDED;
            return H64_NOFILE;
        }
    }
    return fhandle;
    #else
    char *pathu8 = AS_U8(pathu32, pathu32len);
    if (!pathu8) {
        *err = FS32_ERR_OUTOFMEMORY;
        return H64_NOFILE;
    }
    assert(mode_read || mode_write);
    int open_options = (
        ((mode_read && !mode_read && !mode_write) ? O_RDONLY : 0) |
        (((mode_write || mode_append) && !mode_read) ? O_WRONLY : 0) |
        (((mode_write || mode_append) && mode_read) ? O_RDWR : 0) |
        (mode_append ? O_APPEND : 0) |
        ((mode_write && !mode_append) ? O_CREAT : 0) |
        ((flags & OPEN_ONLY_IF_NOT_LINK) != 0 ? O_NOFOLLOW : 0) |
        O_LARGEFILE | O_NOCTTY |
        ((mode_write && !mode_append) ? O_TRUNC : 0)
    );
    int fd = -1;
    if ((open_options & O_CREAT) != 0) {
        fd = open64(
            (strlen(pathu8) > 0 ? pathu8 : "."), open_options, 0664
        );
    } else {
        fd = open64(
            (strlen(pathu8) > 0 ? pathu8 : "."), open_options
        );
    }
    free(pathu8);
    if (fd < 0) {
        *err = FS32_ERR_OTHERERROR;
        if (errno == ENOENT)
            *err = FS32_ERR_NOSUCHTARGET;
        else if (errno == EMFILE || errno == ENFILE)
            *err = FS32_ERR_OUTOFFDS;
        else if (errno == EACCES || errno == EPERM)
            *err = FS32_ERR_NOPERMISSION;
        return H64_NOFILE;
    }
    return fd;
    #endif
}

int filesys32_GetSize(
        const h64wchar *pathu32, int64_t pathu32len, uint64_t *size,
        int *err
        ) {
    if (filesys32_IsObviouslyInvalidPath(pathu32, pathu32len)) {
        *err = FS32_ERR_NOSUCHTARGET;
        return 0;
    }

    #if defined(ANDROID) || defined(__ANDROID__) || defined(__unix__) || defined(__linux__) || defined(__APPLE__) || defined(__OSX__)
    struct stat64 statbuf;

    char *p = AS_U8(pathu32, pathu32len);
    if (!p)
        return 0;
    if (stat64(p, &statbuf) < 0) {
        free(p);
        *err = FS32_ERR_OTHERERROR;
        if (errno == ENOENT)
            *err = FS32_ERR_NOSUCHTARGET;
        else if (errno == EACCES || errno == EPERM)
            *err = FS32_ERR_NOPERMISSION;
        return 0;
    }
    free(p);
    if (!S_ISREG(statbuf.st_mode)) {
        *err = FS32_ERR_TARGETNOTAFILE;
        return 0;
    }
    *size = (uint64_t)statbuf.st_size;
    return 1;
    #else
    assert(sizeof(wchar_t) == sizeof(uint16_t));
    wchar_t *pathw = malloc(pathu32len * 2 + 1);
    int64_t resultlen = 0;
    int result = 0;
    if (pathw)
        result = utf32_to_utf16(
            pathu32, pathu32len, (char *)pathw, pathu32len * 2 + 1,
            &resultlen, 1
        );
    if (!result || resultlen >= pathu32len * 2 + 1) {
        free(pathw);
        return 0;
    }
    pathw[resultlen] = '\0';
    HANDLE fhandle = CreateFileW(
        pathw,
        GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS |  // first try: this is needed for dirs
        FILE_FLAG_OPEN_REPARSE_POINT,
		0
    );
    if (!fhandle) {
        fhandle = CreateFileW(
            pathw,
            GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT,  // in case it's not a directory
            0
        );
        if (!fhandle) {
            free(pathw);
            uint32_t werror = GetLastError();
            *err = FS32_ERR_OTHERERROR;
            if (werror == ERROR_ACCESS_DENIED ||
                    werror == ERROR_SHARING_VIOLATION)
                *err = FS32_ERR_NOPERMISSION;
            else if (werror == ERROR_PATH_NOT_FOUND ||
                    werror == ERROR_FILE_NOT_FOUND ||
                    werror == ERROR_INVALID_NAME ||
                    werror == ERROR_INVALID_DRIVE)
                *err = FS32_ERR_NOSUCHTARGET;
            return 0;
        }
    }
    free(pathw);
    DWORD written = 0;
    char reparse_buf[MAXIMUM_REPARSE_DATA_BUFFER_SIZE] = {0};
    struct reparse_tag {
        ULONG tag;
    };
    if (!DeviceIoControl(
            fhandle, FSCTL_GET_REPARSE_POINT, NULL, 0,
            (LPVOID)reparse_buf, sizeof(reparse_buf),
            &written, 0
            )) {
        // Assume there is no reparse point.
    } else {
        struct reparse_tag *tagcheck = (
            (struct reparse_tag *)reparse_buf
        );
        if (tagcheck->tag == IO_REPARSE_TAG_SYMLINK ||
                tagcheck->tag == IO_REPARSE_TAG_MOUNT_POINT) {
            CloseHandle(fhandle);
            *err = FS32_ERR_TARGETNOTAFILE;
            return 0;
        }
    }
    BY_HANDLE_FILE_INFORMATION finfo = {0};
    if (!GetFileInformationByHandle(fhandle, &finfo)) {
        uint32_t werror = GetLastError();
        *err = FS32_ERR_OTHERERROR;
        if (werror == ERROR_ACCESS_DENIED ||
                werror == ERROR_SHARING_VIOLATION)
            *err = FS32_ERR_NOPERMISSION;
        return 0;
    }
    LARGE_INTEGER v;
    v.HighPart = finfo.nFileSizeHigh;
    v.LowPart = finfo.nFileSizeLow;
    *size = (uint64_t)v.QuadPart;
    CloseHandle(fhandle);
    return 1;
    #endif
}

int filesys32_IsDirectory(
        const h64wchar *pathu32, int64_t pathu32len, int *result
        ) {
    if (filesys32_IsObviouslyInvalidPath(pathu32, pathu32len)) {
        *result = 0;
        return 1;
    }

    // Hack for "" referring to cwd:
    const h64wchar _cwdbuf[] = {'.'};
    if (pathu32len == 0) {
        pathu32 = _cwdbuf;
        pathu32len = 1;
    }

    #if defined(ANDROID) || defined(__ANDROID__) || defined(__unix__) || defined(__linux__) || defined(__APPLE__) || defined(__OSX__)
    struct stat64 sb;
    char *p = AS_U8(pathu32, pathu32len);
    if (!p)
        return 0;
    int statcheck = stat64(p, &sb);
    if (statcheck != 0) {
        free(p);
        if (errno == ENOENT) {
            *result = 0;
            return 1;
         }
         return 0;
    }
    *result = S_ISDIR(sb.st_mode);
    free(p);
    return 1;
    #elif defined(_WIN32) || defined(_WIN64)
    assert(sizeof(wchar_t) == sizeof(uint16_t));
    wchar_t *pathw = malloc(pathu32len * 2 + 2);
    if (!pathw)
        return 0;
    int64_t pathwlen = 0;
    int result_conv = utf32_to_utf16(
        pathu32, pathu32len, (char *)pathw, pathu32len * 2 + 2,
        &pathwlen, 1
    );
    if (!result_conv || pathwlen >= pathu32len * 2 + 2) {
        free(pathw);
        return 0;
    }
    pathw[pathwlen] = '\0';
    DWORD dwAttrib = GetFileAttributesW(pathw);
    free(pathw);
    if (dwAttrib == INVALID_FILE_ATTRIBUTES) {
        uint32_t werror = GetLastError();
        if (werror == ERROR_PATH_NOT_FOUND ||
                werror == ERROR_FILE_NOT_FOUND ||
                werror == ERROR_INVALID_PARAMETER ||
                werror == ERROR_INVALID_NAME ||
                werror == ERROR_INVALID_DRIVE) {
            *result = 0;
            return 1;
        }
        return 0;
    }
    *result = (dwAttrib & FILE_ATTRIBUTE_DIRECTORY);
    return 1;
    #else
    #error "unsupported platform"
    #endif
}

h64wchar *filesys32_GetOwnExecutable(int64_t *out_len, int *oom) {
    #if defined(_WIN32) || defined(_WIN64)
    int fplen = 1024;
    wchar_t *fp = malloc(1024 * sizeof(*fp));
    if (!fp) {
        if (oom) *oom = 1;
        return NULL;
    }
    while (1) {
        SetLastError(0);
        size_t written = (
            GetModuleFileNameW(NULL, fp, MAX_PATH + 1)
        );
        if (written >= (uint64_t)fplen - 1 || (written == 0 &&
                GetLastError() == ERROR_INSUFFICIENT_BUFFER)) {
            fplen *= 2;
            free(fp);
            fp = malloc(fplen * sizeof(*fp));
            if (!fp) {
                if (oom) *oom = 1;
                return NULL;
            }
            continue;
        } else if (written == 0) {
            // Other error, likely disk I/O and not oom.
            if (oom) *oom = 0;
            if (GetLastError() == ERROR_NOT_ENOUGH_MEMORY ||
                    GetLastError() == ERROR_OUTOFMEMORY) {
                // Ok, it's oom.
                if (oom) *oom = 1;
            }
            free(fp);
            return NULL;
        }
        fp[written] = '\0';
        break;
    }
    int64_t result_u32len = 0;
    int _wasoom = 0;
    h64wchar *result_u32 = utf16_to_utf32(
        fp, wcslen(fp), &result_u32len, 1, &_wasoom
    );
    free(fp);
    if (!result_u32) {
        if (oom) *oom = 1;
        return NULL;
    }
    *out_len = result_u32len;
    return result_u32;
    #elif defined(__FREEBSD__) || defined(__FreeBSD__)
    char result[4096];
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1};
    size_t len = sizeof(result) - 1;
    if (sysctl(mib, 4, result, &len, NULL, 0) != 0) {
        if (oom) *oom = 0;
        if (errno == ENOMEM) {
            if (oom) *oom = 1;
        }
        return NULL;
    }
    result[sizeof(result) - 1] = '\0';
    int result_u32len = 0;
    h64wchar *result_u32 = AS_U32(result, result_u32len);
    if (!result_u32) {
        if (oom) *oom = 1;
        return NULL;
    }
    *out_len = result_u32len;
    return result_u32;
    #else
    int alloc = 16;
    char *fpath = malloc(alloc);
    if (!fpath) {
        if (oom) *oom = 1;
        return NULL;
    }
    while (1) {
        fpath[0] = '\0';
        #if defined(APPLE) || defined(__APPLE__)
        int i = alloc;
        if (_NSGetExecutablePath(fpath, &i) != 0) {
            free(fpath);
            if (i <= 0) {
                if (oom) *oom = 0;
                return NULL;
            }
            fpath = malloc(i + 1);
            if (!fpath) {
                if (oom) *oom = 1;
                return NULL;
            }
            if (_NSGetExecutablePath(fpath, &i) != 0) {
                if (oom) *oom = 0;
                free(fpath);
                return NULL;
            }
            int64_t result_u32len = 0;
            h64wchar *result_u32 = AS_U32(fpath, result_u32len);
            free(fpath);
            if (!result_u32) {
                if (oom) *oom = 1;
                return NULL;
            }
            *out_len = result_u32len;
            return result_u32;
        }
        #else
        const char *checkpath = NULL;
        char _path1[] = "/proc/self/exe";
        char _path2[] = "/proc/curproc/file";
        char _path3[] = "/proc/curproc/exe";
        if (filesys_FileExists("/proc/self/exe")) {
            checkpath = _path1;
        } else if (filesys_FileExists("/proc/curproc/file")) {
            checkpath = _path2;
        } else if (filesys_FileExists("/proc/curproc/exe")) {
            checkpath = _path3;
        } else {
            if (oom) *oom = 0;
            free(fpath);
            return NULL;
        }
        int written = readlink(checkpath, fpath, alloc);
        if (written >= alloc) {
            alloc *= 2;
            free(fpath);
            fpath = malloc(alloc);
            if (!fpath) {
                if (oom) *oom = 1;
                return NULL;
            }
            continue;
        } else if (written <= 0) {
            if (oom) *oom = 0;
            if (errno == ENOMEM) {
                if (oom) *oom = 1;
            }
            free(fpath);
            return NULL;
        }
        fpath[written] = '\0';
        int64_t result_u32len = 0;
        h64wchar *result_u32 = AS_U32(fpath, &result_u32len);
        free(fpath);
        if (!result_u32) {
            if (oom) *oom = 1;
            return NULL;
        }
        *out_len = result_u32len;
        return result_u32;
        #endif
    }
    #endif
}

FILE *filesys32_OpenOwnExecutable_Uncached() {
    #if defined(APPLE) || defined(__APPLE__) || \
            defined(_WIN32) || defined(_WIN64)
    h64wchar *path;
    int oom = 0;
    int64_t pathlen;
    path = filesys32_GetOwnExecutable(&pathlen, &oom);
    if (!path)
        return NULL;
    #else
    // This should be race-condition free against moving around
    // for /proc/self/exe (Linux) at least:
    int64_t pathlen = 0;
    h64wchar *path = NULL;
    if (filesys_FileExists("/proc/self/exe")) {  // linux
        path = AS_U32("/proc/self/exe", &pathlen);
    } else if (filesys_FileExists("/proc/curproc/file")) {  // freebsd
        path = AS_U32("/proc/curproc/file", &pathlen);
    } else if (filesys_FileExists("/proc/curproc/exe")) {  // netbsd
        path = AS_U32("/proc/curproc/exe", &pathlen);
    } else {  // fallback (which is subject to races):
        int oom = 0;
        path = filesys32_GetOwnExecutable(&pathlen, &oom);
    }
    if (!path)
        return NULL;
    int _err = 0;
    FILE *f = filesys32_OpenFromPath(
        path, pathlen, "rb", &_err
    );
    free(path);
    return f;
    #endif
}

FILE *_cached_ownexecutable_handle = NULL;
mutex *_openownexec_mutex = NULL;

static __attribute__((constructor)) void _filesys32_OpenOwnExec_Mutex() {
    _openownexec_mutex = mutex_Create();
    if (!_openownexec_mutex) {
        fprintf(stderr, "filesys32.c: failed to "
            "create mutex, out of memory?\n");
    }
}

FILE *filesys32_OpenOwnExecutable() {
    mutex_Lock(_openownexec_mutex);
    if (!_cached_ownexecutable_handle) {
        _cached_ownexecutable_handle = (
            filesys32_OpenOwnExecutable_Uncached()
        );
        if (!_cached_ownexecutable_handle) {
            mutex_Release(_openownexec_mutex);
            return NULL;
        }
    }
    FILE *dup = _dupfhandle(_cached_ownexecutable_handle, "rb");
    mutex_Release(_openownexec_mutex);
    if (fseek64(dup, 0, SEEK_SET) != 0) {
        fclose(dup);
        return NULL;
    }
    return dup;
}

int filesys32_IsSymlink(
        h64wchar *pathu32, int64_t pathu32len, int *err, int *result
        ) {
    if (filesys32_IsObviouslyInvalidPath(pathu32, pathu32len)) {
        *err = FS32_ERR_NOSUCHTARGET;
        return 0;
    }

    #if !defined(_WIN32) && !defined(_WIN64)
    struct stat64 statbuf;

    char *p = AS_U8(pathu32, pathu32len);
    if (!p)
        return 0;
    if (stat64(p, &statbuf) < 0) {
        free(p);
        *err = FS32_ERR_OTHERERROR;
        if (errno == ENOENT)
            *err = FS32_ERR_NOSUCHTARGET;
        else if (errno == EACCES || errno == EPERM)
            *err = FS32_ERR_NOPERMISSION;
        return 0;
    }
    free(p);
    *result = (S_ISLNK(statbuf.st_mode) != 0);
    return 1;
    #else
    int innererr = 0;
    h64filehandle os_f = filesys32_OpenFromPathAsOSHandleEx(
        pathu32, pathu32len, "rb", _WIN32_OPEN_LINK_ITSELF,
        &innererr
    );
    if (os_f == H64_NOFILE) {
        *err = innererr;
        return 0;
    }
    HANDLE fhandle = os_f;
    *result = _check_if_symlink_or_junction(fhandle);
    CloseHandle(fhandle);
    return 1;
    #endif
}

int filesys32_CreateDirectoryRecursively(
        h64wchar *path, int64_t pathlen, int user_readable_only
        ) {
    if (filesys32_IsObviouslyInvalidPath(path, pathlen)) {
        return FS32_ERR_INVALIDNAME;
    }

    // Normalize file paths:
    int64_t cleanpathlen = 0;
    h64wchar *cleanpath = filesys32_Normalize(
        path, pathlen, &cleanpathlen
    );
    if (!cleanpath)
        return FS32_ERR_OUTOFMEMORY;

    // Go back in components until we got a subpath that exists:
    int64_t skippedatend = 0;
    int64_t skippedcomponents = 0;
    while (1) {
        // Skip additional unnecessary / and \ characters
        while (skippedatend < cleanpathlen && (
                cleanpath[cleanpathlen - skippedatend - 1] == '/'
                #if defined(_WIN32) || defined(_WIN64)
                || cleanpath[cleanpathlen - skippedatend - 1] == '\\'
                #endif
                )) {
            skippedatend++;
        }
        // Check if component we're at now exists:
        int64_t actuallen = cleanpathlen - skippedatend;
        int _exists = 0;
        if (!filesys32_TargetExists(
                cleanpath, actuallen, &_exists)) {
            free(cleanpath);
            return FS32_ERR_OTHERERROR;
        }
        if (_exists)
            break;
        if (actuallen <= 0 || (actuallen >= 3 &&
                (cleanpath[actuallen - 1] == '/'
                #if defined(_WIN32) || defined(_WIN64)
                || cleanpath[actuallen - 1] == '\\'
                #endif
                ) && cleanpath[actuallen - 2] == '.' &&
                cleanpath[actuallen - 3] == '.' &&
                (actuallen <= 3 || (
                    cleanpath[actuallen - 4] != '/'
                    #if defined(_WIN32) || defined(_WIN64)
                    && cleanpath[actuallen - 4] != '\\'
                    #endif
                )))
                #if defined(_WIN23) || defined(_WIN64)
                || (actuallen == 3 &&
                cleanpath[1] == ':' && cleanpath[2] == '\\')
                #endif
                ) {
            // We reached a ../ or the path root, nothing more to skip.
            // However, this means the base should have existed
            // -> return I/O error
            free(cleanpath);
            return FS32_ERR_OTHERERROR;
        }
        // Remove one additional component at the end:
        skippedcomponents++;
        skippedatend++;
        while (skippedatend < cleanpathlen && (
                cleanpath[cleanpathlen - skippedatend - 1] != '/'
                #if defined(_WIN32) || defined(_WIN64)
                && cleanpath[cleanpathlen - skippedatend - 1] != '\\'
                #endif
                )) {
            skippedatend++;
        }
    }
    // If we didn't skip back any components, we're done already:
    if (skippedcomponents <= 0)
        return FS32_ERR_SUCCESS;
    // Go back to end component by component, and create the dirs:
    while (skippedcomponents > 0) {
        // Reverse by one additional component:
        skippedcomponents--;
        assert(skippedatend > 0);
        skippedatend--;
        while (skippedatend > 0 && (
                cleanpath[cleanpathlen - skippedatend - 1] == '/'
                #if defined(_WIN32) || defined(_WIN64)
                || cleanpath[cleanpathlen - skippedatend - 1] == '\\'
                #endif
                )) {
            skippedatend--;
        }
        while (skippedatend > 0 && (
                cleanpath[cleanpathlen - skippedatend - 1] != '/'
                #if defined(_WIN32) || defined(_WIN64)
                && cleanpath[cleanpathlen - skippedatend - 1] != '\\'
                #endif
                )) {
            skippedatend--;
        }
        // Create the given sub path:
        int64_t actuallen = cleanpathlen - skippedatend;
        int result = filesys32_CreateDirectory(
            cleanpath, actuallen, user_readable_only
        );
        if (result < 0 && result != FS32_ERR_TARGETALREADYEXISTS)
            return result;
        // Advance to next component:
        continue;
    }
    free(cleanpath);
    return FS32_ERR_SUCCESS;
}

h64wchar *filesys32_TurnIntoPathRelativeTo(
        const h64wchar *path, int64_t pathlen,
        const h64wchar *makerelativetopath, int64_t makerelativetopathlen,
        int64_t *out_len
        ) {
    if (!path)
        return NULL;
    int64_t cwdlen = 0;
    h64wchar *cwd = filesys32_GetCurrentDirectory(&cwdlen);
    if (!cwd)
        return NULL;

    // Prepare input to be absolute & normalized:
    h64wchar *input_path = NULL;
    int64_t input_path_len = 0;
    if (filesys32_IsAbsolutePath(path, pathlen)) {
        input_path = strdupu32(
            path, pathlen, &input_path_len
        );
    } else {
        input_path = filesys32_Join(
            cwd, cwdlen, path, pathlen, &input_path_len
        );
    }
    if (!input_path) {
        free(cwd);
        return NULL;
    }
    {
        int64_t slen = 0;
        h64wchar *_s = filesys32_Normalize(
            input_path, input_path_len, &slen
        );
        if (!_s) {
            free(input_path);
            free(cwd);
            return NULL;
        }
        free(input_path);
        input_path = _s;
    }

    // Prepare comparison path to be absolute & normalized:
    int64_t reltopathlen = 0;
    h64wchar *reltopath = NULL;
    if (makerelativetopath &&
            filesys32_IsAbsolutePath(
                makerelativetopath, makerelativetopathlen)) {
        reltopath = strdupu32(
            makerelativetopath, makerelativetopathlen, &reltopathlen
        );
    } else if (makerelativetopath) {
        reltopath = filesys32_Join(
            cwd, cwdlen, makerelativetopath, makerelativetopathlen,
            &reltopathlen
        );
    } else {
        reltopath = strdupu32(cwd, cwdlen, &reltopathlen);
    }
    if (!reltopath) {
        free(input_path);
        free(cwd);
        return NULL;
    }
    {
        int64_t _slen = 0;
        h64wchar *_s = filesys32_Normalize(
            reltopath, reltopathlen, &_slen
        );
        if (!_s) {
            free(reltopath);
            free(input_path);
            free(cwd);
            return NULL;
        }
        free(reltopath);
        reltopath = _s;
        reltopathlen = _slen;
    }

    // Free unneeded resources:
    free(cwd);
    cwd = NULL;

    // Get the similar path base:
    int similar_up_to = -1;
    int last_component = -1;
    int i = 0;
    while (i < reltopathlen && i < input_path_len) {
        if (reltopath[i] == input_path[i]) {
            similar_up_to = i;
        } else {
            break;
        }
        if (reltopath[i] == '/'
                #if defined(_WIN32) || defined(_WIN64)
                || reltopath[i] == '\\'
                #endif
                ) {
            last_component = i;
        }
        i++;
    }
    if (similar_up_to + 1 >= reltopathlen &&
            (similar_up_to + 1 < input_path_len && (
            input_path[similar_up_to + 1] == '/'
            #if defined(_WIN32) || defined(_WIN64)
            || input_path[similar_up_to + 1] == '\\'
            #endif
            ))) {
        last_component = similar_up_to + 1;
    } else if (similar_up_to + 1 >= input_path_len &&
            (similar_up_to + 1 < reltopathlen && (
            reltopath[similar_up_to + 1] == '/'
            #if defined(_WIN32) || defined(_WIN64)
            || reltopath[similar_up_to + 1] == '\\'
            #endif
            ))) {
        last_component = similar_up_to + 1;
    }
    if (similar_up_to > last_component)
        similar_up_to = last_component;

    int64_t samestartlen = 0;
    h64wchar *samestart = strdupu32(
        input_path, input_path_len, &samestartlen
    );
    if (!samestart) {
        free(input_path);
        free(reltopath);
        return NULL;
    }
    if (similar_up_to + 1 > samestartlen)
        samestartlen = similar_up_to + 1;
    {
        int64_t _slen = 0;
        h64wchar *_s = filesys32_Normalize(
            samestart, samestartlen, &_slen
        );
        free(samestart);
        samestart = NULL;
        if (!_s) {
            free(reltopath);
            return NULL;
        }
        samestart = _s;
        samestartlen = _slen;
    }

    int64_t differingendlen = 0;
    h64wchar *differingend = strdupu32(
        input_path, input_path_len, &differingendlen
    );
    free(input_path);
    input_path = NULL;
    if (!differingend) {
        free(reltopath);
        free(samestart);
        return NULL;
    }

    if (similar_up_to > 0) {
        memmove(
            differingend, differingend + (similar_up_to + 1),
            (differingendlen - (similar_up_to + 1)) *
                sizeof(*differingend)
        );
        differingendlen -= (similar_up_to + 1);
    }
    while (differingendlen > 0 && (
            differingend[0] == '/'
            #if defined(_WIN32) || defined(_WIN64)
            || differingend[0] == '\\'
            #endif
            )) {
        memmove(
            differingend, differingend + 1,
            (differingendlen - 1) * sizeof(*differingend)
        );
        differingendlen--;
    }

    int samestart_components = (
        filesys32_GetComponentCount(samestart, samestartlen)
    );
    int reltopath_components = (
        filesys32_GetComponentCount(reltopath, reltopathlen)
    );

    free(reltopath);
    reltopath = NULL;
    free(samestart);
    samestart = NULL;

    i = samestart_components;
    while (i < reltopath_components) {
        int64_t _slen = (differingendlen + strlen("../"));
        h64wchar *_s = malloc(
            (differingendlen + strlen("../")) * sizeof(*differingend)
        );
        if (!_s) {
            free(differingend);
            return NULL;
        }
        #if defined(_WIN32) || defined(_WIN64)
        _s[0] = '.';  _s[1] = '.';  _s[2] = '\\';
        #else
        _s[0] = '.';  _s[1] = '.';  _s[2] = '/';
        #endif
        memcpy(_s + strlen("../"),
            differingend, sizeof(*differingend) * differingendlen);
        free(differingend);
        differingend = _s;
        differingendlen = _slen;
        i++;
    }

    *out_len = differingendlen;
    return differingend;
}

h64wchar *filesys32_Basename(
        const h64wchar *path, int64_t pathlen, int64_t *out_len
        ) {
    h64wchar *result = malloc(sizeof(*path) * (pathlen > 0 ? pathlen : 1));
    if (!result)
        return NULL;
    memcpy(
        result, path, sizeof(*path) * pathlen
    );
    int64_t resultlen = pathlen;
    while (resultlen > 0 && (result[resultlen - 1] == '/'
            #if defined(_WIN32) || defined(_WIN64)
            || result[resultlen - 1] == '\\'
            #endif
            )) {
        resultlen--;
    }
    int64_t lastsep = -1;
    int64_t i = 0;
    while (i < resultlen) {
        if (result[i] == '/'
                #if defined(_WIN32) || defined(_WIN64)
                || result[i] == '\\'
                #endif
                ) {
            lastsep = i;
        }
        i++;
    }
    if (lastsep >= 0) {
        memmove(
            result, result + lastsep + 1,
            sizeof(*result) * (resultlen - lastsep - 1)
        );
        resultlen -= (lastsep + 1);
    }
    *out_len = resultlen;
    return result;
}

int filesys32_FolderContainsPath(
        const h64wchar *folder_path, int64_t folder_path_len,
        const h64wchar *check_path, int64_t check_path_len,
        int *result
        ) {
    if (!folder_path || !check_path)
        return 0;
    int64_t fnormalizedlen = 0;
    h64wchar *fnormalized = filesys32_Normalize(
        folder_path, folder_path_len, &fnormalizedlen
    );
    int64_t checknormalizedlen = 0;
    h64wchar *checknormalized = filesys32_Normalize(
        check_path, check_path_len, &checknormalizedlen
    );
    if (!fnormalized || !checknormalized) {
        free(fnormalized);
        free(checknormalized);
        return 0;
    }
    if (fnormalizedlen < checknormalizedlen && (
            checknormalized[fnormalizedlen] == '/'
            #if defined(_WIN32) || defined(_WIN64)
            || checknormalized[fnormalizedlen] == '\\'
            #endif
            )) {
        free(fnormalized);
        free(checknormalized);
        *result = 1;
        return 1;
    }
    free(fnormalized);
    free(checknormalized);
    *result = 0;
    return 1;
}
