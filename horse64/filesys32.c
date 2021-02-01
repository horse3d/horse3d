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
#else
#define fseek64 fseeko64
#define ftell64 ftello64
#endif

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#if defined(_WIN32) || defined(_WIN64)
#define _O_RDONLY 0x0000
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

#include "filesys.h"
#include "filesys32.h"
#include "secrandom.h"
#include "widechar.h"


FILE *filesys32_OpenFromPath(
        const h64wchar *path, int64_t pathlen,
        const char *mode
        ) {
    if (filesys32_IsObviouslyInvalidPath(path, pathlen)) {
        errno = ENOENT;
        return NULL;
    }
    #if defined(_WIN32) || defined(_WIN64)
    assert(sizeof(uint16_t) == sizeof(wchar_t));
    uint16_t *wpath = malloc(
        sizeof(uint16_t) * (pathlen * 2 + 3)
    );
    if (!wpath) {
        errno = ENOMEM;
        return NULL;
    }
    int64_t out_len = 0;
    int result = utf32_to_utf16(
        path, pathlen, (char *) wpath,
        sizeof(uint16_t) * (pathlen * 2 + 3),
        &out_len, 1
    );
    if (!result || (uint64_t)out_len >=
            (uint64_t)(pathlen * 2 + 3)) {
        free(wpath);
        errno = ENOMEM;
        return NULL;
    }
    wpath[out_len] = '\0';
    int mode_read = strstr(mode, "r") || strstr(mode, "a");
    int mode_write = strstr(mode, "w") || strstr(mode, "a");
    int mode_append = strstr(mode, "r+") || strstr(mode, "a");
    HANDLE f = CreateFileW(
        (LPCWSTR)wpath,
        0 | (mode_read ? GENERIC_READ : 0)
        | (mode_write ? GENERIC_WRITE : 0),
        (mode_write ? 0 : FILE_SHARE_READ),
        NULL,
        OPEN_EXISTING | (
            (mode_write && !mode_append) ? CREATE_NEW : 0
        ),
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    free(wpath);  wpath = NULL;
    if (f == INVALID_HANDLE_VALUE) {
        uint32_t err = GetLastError();
        if (err == ERROR_SHARING_VIOLATION ||
                err == ERROR_ACCESS_DENIED) {
            errno = EACCES;
        } else if (err== ERROR_PATH_NOT_FOUND ||
                err == ERROR_FILE_NOT_FOUND) {
            errno = ENOENT;
        } else if (err == ERROR_TOO_MANY_OPEN_FILES) {
            errno = EMFILE;
        } else if (err == ERROR_INVALID_NAME ||
                err == ERROR_LABEL_TOO_LONG ||
                err == ERROR_BUFFER_OVERFLOW ||
                err == ERROR_FILENAME_EXCED_RANGE
                ) {
            errno = EINVAL;
        } else {
            errno = ENOMEM;
        }
        return NULL;
    }
    int filedescr = _open_osfhandle(
        (intptr_t)f, _O_RDONLY
    );
    if (filedescr < 0) {
        errno = ENOMEM;
        CloseHandle(f);
        return NULL;
    }
    f = NULL;  // now owned by 'filedescr'
    errno = 0;
    FILE *fresult = _fdopen(filedescr, "rb");
    return fresult;
    #else
    char *pathu8 = AS_U8(path, pathlen);
    if (!pathu8) {
        errno = ENOMEM;
        return NULL;
    }
    FILE *f = fopen64(pathu8, mode);
    free(pathu8);
    return f;
    #endif
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
        *error = FS32_CONTENTASSTR_TARGETNOTAFILE;
        return NULL;
    }
    FILE *f = filesys32_OpenFromPath(
        path, pathlen, "rb"
    );
    if (!f) {
        *error = FS32_CONTENTASSTR_OTHERERROR;
        if (errno == EACCES)
            *error = FS32_CONTENTASSTR_NOPERMISSION;
        else if (errno == ENOENT)
            *error = FS32_CONTENTASSTR_TARGETNOTAFILE;
        else if (errno == EMFILE || errno == ENFILE)
            *error = FS32_CONTENTASSTR_OUTOFFDS;
        else if (errno == EINVAL)
            *error = FS32_CONTENTASSTR_INVALIDFILENAME;
        else if (errno == ENOMEM)
            *error = FS32_CONTENTASSTR_OUTOFMEMORY;
        return NULL;
    }
    if (fseek64(f, 0, SEEK_END) != 0) {
        *error = FS32_CONTENTASSTR_IOERROR;
        fclose(f);
        return NULL;
    }
    int64_t len = ftell64(f);
    if (len < 0) {
        *error = FS32_CONTENTASSTR_IOERROR;
        fclose(f);
        return NULL;
    }
    if (fseek64(f, 0, SEEK_SET) != 0) {
        *error = FS32_CONTENTASSTR_IOERROR;
        fclose(f);
        return NULL;
    }
    char *resultstr = malloc(len + 1);
    if (!resultstr) {
        *error = FS32_CONTENTASSTR_OUTOFMEMORY;
        fclose(f);
        return NULL;
    }
    size_t result = fread(resultstr, 1, len, f);
    fclose(f);
    if ((int64_t)result != len) {
        *error = FS32_CONTENTASSTR_IOERROR;
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
        *error = FS32_REMOVEERR_NOSUCHTARGET;
        return 0;
    }

    #if defined(_WIN32) || defined(_WIN64)
    assert(sizeof(wchar_t) == sizeof(uint16_t));
    wchar_t *targetpath = malloc(
        sizeof(*targetpath) * (path32len * 2 + 1)
    );
    if (!targetpath) {
        *error = FS32_REMOVEERR_OUTOFMEMORY;
        return 0;
    }
    int64_t targetpathlen = 0;
    int result = utf32_to_utf16(
        path32, path32len, (char *)targetpath,
        sizeof(*targetpath) * (path32len * 2 + 1),
        &targetpathlen, 1
    );
    if (!result || targetpathlen >= (path32len * 2 + 1)) {
        *error = FS32_REMOVEERR_OUTOFMEMORY;
        return 0;
    }
    targetpath[targetpathlen] = '\0';
    if (DeleteFileW(targetpath) != TRUE) {
        uint32_t werror = GetLastError();
        *error = FS32_REMOVEERR_OTHERERROR;
        if (werror == ERROR_DIRECTORY_NOT_SUPPORTED ||
                werror == ERROR_DIRECTORY) {
            if (RemoveDirectoryW(targetpath) != TRUE) {
                free(targetpath);
                werror = GetLastError();
                *error = FS32_REMOVEERR_OTHERERROR;
                if (werror == ERROR_PATH_NOT_FOUND ||
                        werror == ERROR_FILE_NOT_FOUND ||
                        werror == ERROR_INVALID_PARAMETER ||
                        werror == ERROR_INVALID_NAME ||
                        werror == ERROR_INVALID_DRIVE)
                    *error = FS32_REMOVEERR_NOSUCHTARGET;
                else if (werror == ERROR_ACCESS_DENIED ||
                        werror == ERROR_WRITE_PROTECT)
                    *error = FS32_REMOVEERR_NOPERMISSION;
                else if (werror == ERROR_NOT_ENOUGH_MEMORY)
                    *error = FS32_REMOVEERR_OUTOFMEMORY;
                else if (werror == ERROR_TOO_MANY_OPEN_FILES)
                    *error = FS32_REMOVEERR_OUTOFFDS;
                else if (werror == ERROR_PATH_BUSY ||
                        werror == ERROR_BUSY ||
                        werror == ERROR_CURRENT_DIRECTORY)
                    *error = FS32_REMOVEERR_DIRISBUSY;
                else if (werror == ERROR_DIR_NOT_EMPTY)
                    *error = FS32_REMOVEERR_NONEMPTYDIRECTORY;
                return 0;
            }
            free(targetpath);
            *error = FS32_REMOVEERR_SUCCESS;
            return 1;
        }
        free(targetpath);
        if (werror == ERROR_PATH_NOT_FOUND ||
                werror == ERROR_FILE_NOT_FOUND ||
                werror == ERROR_INVALID_PARAMETER ||
                werror == ERROR_INVALID_NAME ||
                werror == ERROR_INVALID_DRIVE)
            *error = FS32_REMOVEERR_NOSUCHTARGET;
        else if (werror == ERROR_ACCESS_DENIED ||
                werror == ERROR_WRITE_PROTECT)
            *error = FS32_REMOVEERR_NOPERMISSION;
        else if (werror == ERROR_NOT_ENOUGH_MEMORY)
            *error = FS32_REMOVEERR_OUTOFMEMORY;
        else if (werror == ERROR_TOO_MANY_OPEN_FILES)
            *error = FS32_REMOVEERR_OUTOFFDS;
        else if (werror == ERROR_PATH_BUSY ||
                werror == ERROR_BUSY)
            *error = FS32_REMOVEERR_DIRISBUSY;
        return 0;
    }
    free(targetpath);
    *error = FS32_REMOVEERR_SUCCESS;
    #else
    int64_t plen = 0;
    char *p = malloc(path32len * 5 + 1);
    if (!p) {
        *error = FS32_REMOVEERR_OUTOFMEMORY;
        return 0;
    }
    int result = utf32_to_utf8(
        path32, path32len, p, path32len * 5 + 1,
        &plen, 1, 1
    );
    if (!result || plen >= path32len * 5 + 1) {
        free(p);
        *error = FS32_REMOVEERR_OUTOFMEMORY;
        return 0;
    }
    p[plen] = '\0';
    errno = 0;
    result = remove(p);
    free(p);
    if (result != 0) {
        *error = FS32_REMOVEERR_OTHERERROR;
        if (errno == EACCES || errno == EPERM ||
                errno == EROFS) {
            *error = FS32_REMOVEERR_NOPERMISSION;
        } else if (errno == ENOTEMPTY) {
            *error = FS32_REMOVEERR_NONEMPTYDIRECTORY;
        } else if (errno == ENOENT || errno == ENAMETOOLONG ||
                errno == ENOTDIR) {
            *error = FS32_REMOVEERR_NOSUCHTARGET;
        } else if (errno == EBUSY) {
            *error = FS32_REMOVEERR_DIRISBUSY;
        }
        return 0;
    }
    *error = FS32_REMOVEERR_SUCCESS;
    #endif
    return 1;
}

int filesys32_ListFolderEx(
        const h64wchar *path32, int64_t path32len,
        h64wchar ***contents, int64_t **contentslen,
        int returnFullPath, int allowsymlink, int *error
        ) {
    if (filesys32_IsObviouslyInvalidPath(path32, path32len)) {
        *error = FS32_LISTFOLDERERR_TARGETNOTDIRECTORY;
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
        *error = FS32_LISTFOLDERERR_OUTOFMEMORY;
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
        *error = FS32_LISTFOLDERERR_OUTOFMEMORY;
        return 0;
    }
    folderpath[folderpathlen] = '\0';
    wchar_t *p = malloc(wcslen(folderpath) + 3);
    if (!p) {
        free(folderpath);
        *error = FS32_LISTFOLDERERR_OUTOFMEMORY;
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
                *error = FS32_LISTFOLDERERR_OUTOFMEMORY;
                return 0;
            }
            (*contents)[0] = NULL;
            (*contentslen)[0] = -1;
            *error = FS32_LISTFOLDERERR_SUCCESS;
            return 1;
        }
        *error = FS32_LISTFOLDERERR_OTHERERROR;
        if (werror == ERROR_PATH_NOT_FOUND ||
                werror == ERROR_FILE_NOT_FOUND)
            *error = FS32_LISTFOLDERERR_NOSUCHTARGET;
        else if (werror == ERROR_INVALID_PARAMETER ||
                werror == ERROR_INVALID_NAME ||
                werror == ERROR_INVALID_DRIVE ||
                werror == ERROR_DIRECTORY_NOT_SUPPORTED)
            *error = FS32_LISTFOLDERERR_TARGETNOTDIRECTORY;
        else if (werror == ERROR_ACCESS_DENIED)
            *error = FS32_LISTFOLDERERR_NOPERMISSION;
        else if (werror == ERROR_NOT_ENOUGH_MEMORY)
            *error = FS32_LISTFOLDERERR_OUTOFMEMORY;
        else if (werror == ERROR_TOO_MANY_OPEN_FILES)
            *error = FS32_LISTFOLDERERR_OUTOFFDS;
        return 0;
    }
    free(p);
    #else
    int64_t plen = 0;
    char *p = malloc(path32len * 5 + 1);
    if (!p) {
        *error = FS32_LISTFOLDERERR_OUTOFMEMORY;
        return 0;
    }
    int result = utf32_to_utf8(
        path32, path32len, p, path32len * 5 + 1,
        &plen, 1, 1
    );
    if (!result || plen >= path32len * 5 + 1) {
        free(p);
        *error = FS32_LISTFOLDERERR_OUTOFMEMORY;
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
        int dirfd = open(
            (strlen(p) > 0 ? p : "."),
            O_RDONLY | O_NOFOLLOW | O_LARGEFILE | O_NOCTTY
        );
        if (dirfd < 0) {
            free(p);
            *error = FS32_LISTFOLDERERR_OTHERERROR;
            if (errno == EMFILE || errno == ENFILE)
                *error = FS32_LISTFOLDERERR_OUTOFFDS;
            else if (errno == ENOMEM)
                *error = FS32_LISTFOLDERERR_OUTOFMEMORY;
            else if (errno == ELOOP)
                *error = FS32_LISTFOLDERERR_SYMLINKSWEREEXCLUDED;
            else if (errno == EACCES)
                *error = FS32_LISTFOLDERERR_NOPERMISSION;
            return 0;
        }
        d = fdopendir(dirfd);
    }
    free(p);
    if (!d) {
        *error = FS32_LISTFOLDERERR_OTHERERROR;
        if (errno == EMFILE || errno == ENFILE)
            *error = FS32_LISTFOLDERERR_OUTOFFDS;
        else if (errno == ENOMEM)
            *error = FS32_LISTFOLDERERR_OUTOFMEMORY;
        else if (errno == ENOTDIR || errno == ENOENT)
            *error = FS32_LISTFOLDERERR_TARGETNOTDIRECTORY;
        else if (errno == EACCES)
            *error = FS32_LISTFOLDERERR_NOPERMISSION;
        return 0;
    }
    #endif
    // Now, get all the files entries and put them into the list:
    h64wchar **list = malloc(sizeof(*list));
    int64_t *listlen = malloc(sizeof(*listlen));
    if (!list || !listlen) {
        free(list);
        free(listlen);
        *error = FS32_LISTFOLDERERR_OUTOFMEMORY;
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
                    *error = FS32_LISTFOLDERERR_OTHERERROR;
                    if (werror == ERROR_NOT_ENOUGH_MEMORY)
                        *error = FS32_LISTFOLDERERR_OUTOFMEMORY;
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
            *error = FS32_LISTFOLDERERR_OUTOFMEMORY;
            goto errorquit;
        }
        #else
        errno = 0;
        struct dirent *entry = readdir(d);
        if (!entry && errno != 0) {
            *error = FS32_LISTFOLDERERR_OUTOFMEMORY;
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
            *error = FS32_LISTFOLDERERR_OUTOFMEMORY;
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
            *error = FS32_LISTFOLDERERR_OUTOFMEMORY;
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
        *error = FS32_LISTFOLDERERR_SUCCESS;
    } else {
        // Ok, allocate new array for conversion:
        fullPathList = malloc(sizeof(*fullPathList) * (entriesSoFar + 1));
        if (!fullPathList) {
            *error = FS32_LISTFOLDERERR_OUTOFMEMORY;
            goto errorquit;
        }
        memset(fullPathList, 0, sizeof(*fullPathList) * (entriesSoFar + 1));
        fullPathListLen = malloc(
            sizeof(*fullPathListLen) * (entriesSoFar + 1)
        );
        if (!fullPathListLen) {
            *error = FS32_LISTFOLDERERR_OUTOFMEMORY;
            goto errorquit;
        }
        int k = 0;
        while (k < entriesSoFar) {
            fullPathList[k] = malloc(
                (path32len + 1 + listlen[k]) * sizeof(*list[k])
            );
            if (!fullPathList[k]) {
                *error = FS32_LISTFOLDERERR_OUTOFMEMORY;
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
        *error = FS32_LISTFOLDERERR_SUCCESS;
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
        *error = FS32_REMOVEDIR_NOTADIR;
        return 0;
    }

    int final_error = FS32_REMOVEDIR_SUCCESS;
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
            if (operror == FS32_LISTFOLDERERR_TARGETNOTDIRECTORY ||
                    operror == FS32_LISTFOLDERERR_SYMLINKSWEREEXCLUDED ||
                    (firstitem && operror ==
                        FS32_LISTFOLDERERR_NOSUCHTARGET)) {
                // It's a file or symlink.
                // If it's a file, and this is our first item, error:
                if (firstitem && operror ==
                        FS32_LISTFOLDERERR_TARGETNOTDIRECTORY) {
                    // We're advertising recursive DIRECTORY deletion,
                    // so resuming here would be unexpected.
                    *error = FS32_REMOVEDIR_NOTADIR;
                    assert(!contents && queue_len == 0);
                    return 0;
                } else if (firstitem &&
                        operror == FS32_LISTFOLDERERR_NOSUCHTARGET) {
                    *error = FS32_REMOVEDIR_NOSUCHTARGET;
                    assert(!contents && queue_len == 0);
                    return 0;
                }
                // Instantly remove it instead:
                if (operror != FS32_LISTFOLDERERR_NOSUCHTARGET &&
                        !filesys32_RemoveFileOrEmptyDir(
                        scan_next, scan_next_len, &operror
                        )) {
                    if (operror == FS32_REMOVEERR_NOSUCHTARGET) {
                        // Maybe it was removed in parallel?
                        // ignore this.
                    } else if (final_error == FS32_REMOVEDIR_SUCCESS) {
                        // No error yet, so take this one.
                        if (operror == FS32_REMOVEERR_NOPERMISSION)
                            final_error = FS32_REMOVEDIR_NOPERMISSION;
                        else if (operror == FS32_REMOVEERR_OUTOFMEMORY)
                            final_error = FS32_REMOVEDIR_OUTOFMEMORY;
                        else if (operror == FS32_REMOVEERR_OUTOFFDS)
                            final_error = FS32_REMOVEDIR_OUTOFFDS;
                        else if (operror == FS32_REMOVEERR_DIRISBUSY)
                            final_error = FS32_REMOVEDIR_DIRISBUSY;
                        else if (operror ==
                                FS32_REMOVEERR_NONEMPTYDIRECTORY)
                            final_error = (
                                FS32_REMOVEDIR_NONEMPTYDIRECTORY
                            );
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
            if (final_error == FS32_REMOVEDIR_SUCCESS) {
                // No error yet, so take this one.
                final_error = FS32_REMOVEDIR_OTHERERROR;
                if (operror == FS32_LISTFOLDERERR_NOPERMISSION)
                    final_error = FS32_REMOVEDIR_NOPERMISSION;
                else if (operror == FS32_LISTFOLDERERR_OUTOFMEMORY)
                    final_error = FS32_REMOVEDIR_OUTOFMEMORY;
                else if (operror == FS32_LISTFOLDERERR_OUTOFFDS)
                    final_error = FS32_REMOVEDIR_OUTOFFDS;
                else if (operror == FS32_LISTFOLDERERR_NOSUCHTARGET)
                    final_error = (
                        FS32_REMOVEDIR_NOSUCHTARGET
                    );
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
                *error = FS32_REMOVEDIR_OUTOFMEMORY;
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
            if (operror == FS32_REMOVEERR_NOSUCHTARGET) {
                // Maybe it was removed in parallel?
                // ignore this.
            } else if (final_error == FS32_REMOVEDIR_SUCCESS) {
                // No error yet, so take this one.
                if (operror == FS32_REMOVEERR_NOPERMISSION)
                    final_error = FS32_REMOVEDIR_NOPERMISSION;
                else if (operror == FS32_REMOVEERR_OUTOFMEMORY)
                    final_error = FS32_REMOVEDIR_OUTOFMEMORY;
                else if (operror == FS32_REMOVEERR_OUTOFFDS)
                    final_error = FS32_REMOVEDIR_OUTOFFDS;
                else if (operror == FS32_REMOVEERR_DIRISBUSY)
                    final_error = FS32_REMOVEDIR_DIRISBUSY;
                else if (operror == FS32_REMOVEERR_NONEMPTYDIRECTORY)
                    final_error = (
                        FS32_REMOVEDIR_NONEMPTYDIRECTORY
                    );
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
        if (operror == FS32_REMOVEERR_NOSUCHTARGET) {
            // Maybe it was removed in parallel?
            // ignore this.
        } else if (final_error == FS32_REMOVEDIR_SUCCESS) {
            // No error yet, so take this one.
            if (operror == FS32_REMOVEERR_NOPERMISSION)
                final_error = FS32_REMOVEDIR_NOPERMISSION;
            else if (operror == FS32_REMOVEERR_OUTOFMEMORY)
                final_error = FS32_REMOVEDIR_OUTOFMEMORY;
            else if (operror == FS32_REMOVEERR_OUTOFFDS)
                final_error = FS32_REMOVEDIR_OUTOFFDS;
            else if (operror == FS32_REMOVEERR_DIRISBUSY)
                final_error = FS32_REMOVEDIR_DIRISBUSY;
            else if (operror == FS32_REMOVEERR_NONEMPTYDIRECTORY)
                final_error = (
                    FS32_REMOVEDIR_NONEMPTYDIRECTORY
                );
        }
    }
    if (final_error != FS32_REMOVEDIR_SUCCESS) {
        *error = final_error;
        return 0;
    }
    return 1;
}

int filesys32_ChangeDirectory(
        h64wchar *path, int64_t pathlen
        ) {
    if (filesys32_IsObviouslyInvalidPath(path, pathlen)) {
        return FS32_CHDIRERR_TARGETNOTADIRECTORY;
    }

    #if defined(_WIN32) || defined(_WIN64)
    wchar_t *targetpath = malloc(
        sizeof(*targetpath) * (pathlen * 2 + 1)
    );
    if (!targetpath) {
        return FS32_CHDIRERR_OUTOFMEMORY;
    }
    int64_t targetpathlen = 0;
    int uresult = utf32_to_utf16(
        path, pathlen, (char *)targetpath,
        sizeof(*targetpath) * (pathlen * 2 + 1),
        &targetpathlen, 1
    );
    if (!uresult || targetpathlen >= (pathlen * 2 + 1)) {
        free(targetpath);
        return FS32_CHDIRERR_OUTOFMEMORY;
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
            return FS32_CHDIRERR_TARGETNOTADIRECTORY;
        } else if (werror == ERROR_ACCESS_DENIED) {
            return FS32_CHDIRERR_NOPERMISSION;
        } else if (werror == ERROR_NOT_ENOUGH_MEMORY) {
            return FS32_CHDIRERR_OUTOFMEMORY;
        }
        return FS32_CHDIRERR_OTHERERROR;
    }
    return 1;
    #else
    char *targetpath = malloc(
        sizeof(*targetpath) * (pathlen * 5 + 1)
    );
    if (!targetpath) {
        return FS32_MKDIRERR_OUTOFMEMORY;
    }
    int64_t targetpathlen = 0;
    int uresult = utf32_to_utf8(
        path, pathlen, (char *)targetpath,
        sizeof(*targetpath) * (pathlen * 2 + 1),
        &targetpathlen, 1, 0
    );
    if (!uresult || targetpathlen >= (pathlen * 5 + 1)) {
        free(targetpath);
        return FS32_MKDIRERR_OUTOFMEMORY;
    }
    targetpath[targetpathlen] = '\0';
    int statresult = chdir(targetpath);
    free(targetpath);
    if (statresult < 0) {
        if (errno == EACCES || errno == EPERM) {
            return FS32_CHDIRERR_NOPERMISSION;
        } else if (errno == ENOMEM) {
            return FS32_CHDIRERR_OUTOFMEMORY;
        } else if (errno == ENOENT) {
            return FS32_CHDIRERR_TARGETNOTADIRECTORY;
        }
        return FS32_CHDIRERR_OTHERERROR;
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
        return FS32_MKDIRERR_INVALIDNAME;
    }

    #if defined(_WIN32) || defined(_WIN64)
    wchar_t *targetpath = malloc(
        sizeof(*targetpath) * (pathlen * 2 + 1)
    );
    if (!targetpath) {
        return FS32_MKDIRERR_OUTOFMEMORY;
    }
    int64_t targetpathlen = 0;
    int uresult = utf32_to_utf16(
        path, pathlen, (char *)targetpath,
        sizeof(*targetpath) * (pathlen * 2 + 1),
        &targetpathlen, 1
    );
    if (!uresult || targetpathlen >= (pathlen * 2 + 1)) {
        free(targetpath);
        return FS32_MKDIRERR_OUTOFMEMORY;
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
            return FS32_MKDIRERR_PARENTSDONTEXIST;
        } else if (werror == ERROR_ACCESS_DENIED) {
            return FS32_MKDIRERR_NOPERMISSION;
        } else if (werror == ERROR_NOT_ENOUGH_MEMORY) {
            return FS32_MKDIRERR_OUTOFMEMORY;
        } else if (werror == ERROR_TOO_MANY_OPEN_FILES) {
            return FS32_MKDIRERR_OUTOFFDS;
        } else if (werror == ERROR_ALREADY_EXISTS) {
            return FS32_MKDIRERR_TARGETALREADYEXISTS;
        } else if (werror == ERROR_BAD_PATHNAME) {
            return FS32_MKDIRERR_INVALIDNAME;
        }
        return FS32_MKDIRERR_OTHERERROR;
    }
    return 1;
    #else
    char *targetpath = malloc(
        sizeof(*targetpath) * (pathlen * 5 + 1)
    );
    if (!targetpath) {
        return FS32_MKDIRERR_OUTOFMEMORY;
    }
    int64_t targetpathlen = 0;
    int uresult = utf32_to_utf8(
        path, pathlen, (char *)targetpath,
        sizeof(*targetpath) * (pathlen * 2 + 1),
        &targetpathlen, 1, 0
    );
    if (!uresult || targetpathlen >= (pathlen * 5 + 1)) {
        free(targetpath);
        return FS32_MKDIRERR_OUTOFMEMORY;
    }
    targetpath[targetpathlen] = '\0';
    int statresult = (
        mkdir(targetpath,
              (user_readable_only ? 0700 : 0775)) == 0);
    free(targetpath);
    if (!statresult) {
        if (errno == EACCES || errno == EPERM) {
            return FS32_MKDIRERR_NOPERMISSION;
        } else if (errno == EMFILE || errno == ENFILE) {
            return FS32_MKDIRERR_OUTOFFDS;
        } else if (errno == ENOMEM) {
            return FS32_MKDIRERR_OUTOFMEMORY;
        } else if (errno == ENOENT) {
            return FS32_MKDIRERR_PARENTSDONTEXIST;
        } else if (errno == EEXIST) {
            return FS32_MKDIRERR_TARGETALREADYEXISTS;
        }
        return FS32_MKDIRERR_OTHERERROR;
    }
    return FS32_MKDIRERR_SUCCESS;
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
                werror == ERROR_BUSY) {
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
    struct stat sb = {0};
    int statresult = (stat(targetpath, &sb) == 0);
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
            resultlen -= movelen;
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
            #if defined(_WIN32) || defined(_WIN64)
            result[i] = '\\';
            #else
            result[i] = '/';
            #endif
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


FILE *_filesys32_TempFile_SingleTry(
        int subfolder,
        const h64wchar *prefix, int64_t prefixlen,
        const h64wchar *suffix, int64_t suffixlen,
        h64wchar **folder_path, int64_t* folder_path_len,
        h64wchar **path, int64_t *path_len,
        int *do_retry
        ) {
    *do_retry = 0;
    *path = NULL;
    *path_len = 0;
    *folder_path = NULL;
    *folder_path_len = 0;

    // Get the folder path for system temp location:
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
    assert(wcslen(tempbufw) < (unsigned)tempbufwsize - 2);
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
    #endif

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
            randomu32len + 1)
        );
        if (!combined_path) {
            free(tempbuf);
            free(randomu32);
            return NULL;
        }
        memcpy(combined_path, tempbuf,
               sizeof(*combined_path) * tempbuffill);
        memcpy(combined_path + tempbuffill, randomu32,
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
            if (_mkdirerror == FS32_MKDIRERR_TARGETALREADYEXISTS) {
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
    FILE *f = filesys32_OpenFromPath(
        file_path, file_path_len, "wb"
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
        int subfolder,
        const h64wchar *prefix, int64_t prefixlen,
        const h64wchar *suffix, int64_t suffixlen,
        h64wchar **folder_path, int64_t* folder_path_len,
        h64wchar **path, int64_t *path_len
        ) {
    while (1) {
        int retry = 0;
        FILE *f = _filesys32_TempFile_SingleTry(
            subfolder, prefix, prefixlen,
            suffix, suffixlen, folder_path,
            folder_path_len,
            path, path_len, &retry
        );
        if (!f && !retry)
            return NULL;
        if (f)
            return f;
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

int filesys32_GetSize(
        const h64wchar *pathu32, int64_t pathu32len, uint64_t *size
        ) {
    if (filesys32_IsObviouslyInvalidPath(pathu32, pathu32len))
        return 0;

    #if defined(ANDROID) || defined(__ANDROID__) || defined(__unix__) || defined(__linux__) || defined(__APPLE__) || defined(__OSX__)
    struct stat64 statbuf;

    char *p = AS_U8(pathu32, pathu32len);
    if (!p)
        return 0;
    if (stat64(p, &statbuf) == -1) {
        free(p);
        return 0;
    }
    free(p);
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
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(pathw, GetFileExInfoStandard, &fad)) {
        free(pathw);
        return 0;
    }
    free(pathw);
    LARGE_INTEGER v;
    v.HighPart = fad.nFileSizeHigh;
    v.LowPart = fad.nFileSizeLow;
    *size = (uint64_t)v.QuadPart;
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
    struct stat sb;
    char *p = AS_U8(pathu32, pathu32len);
    if (!p)
        return 0;
    int statcheck = stat(p, &sb);
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

h64wchar *filesys32_GetOwnExecutable(int64_t *out_len) {
    #if defined(_WIN32) || defined(_WIN64)
    int fplen = 1024;
    wchar_t *fp = malloc(1024 * sizeof(*fp));
    if (!fp)
        return NULL;
    while (1) {
        SetLastError(0);
        size_t written = (
            GetModuleFileNameW(NULL, fp, MAX_PATH + 1)
        );
        if (written >= (unsigned)fplen - 1 ||
                GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
            fplen *= 2;
            free(fp);
            fp = malloc(fplen * sizeof(*fp));
            if (!fp)
                return NULL;
            continue;
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
    *out_len = result_u32len;
    return result_u32;
    #else
    int alloc = 16;
    char *fpath = malloc(alloc);
    if (!fpath)
        return NULL;
    while (1) {
        fpath[0] = '\0';
        #if defined(APPLE) || defined(__APPLE__)
        int i = alloc;
        if (_NSGetExecutablePath(fpath, &i) != 0) {
            free(fpath);
            fpath = malloc(i + 1);
            if (_NSGetExecutablePath(fpath, &i) != 0) {
                free(fpath);
                return NULL;
            }
            return fpath;
        }     
        #else
        int written = readlink("/proc/self/exe", fpath, alloc);
        if (written >= alloc) {
            alloc *= 2;
            free(fpath);
            fpath = malloc(alloc);
            if (!fpath)
                return NULL;
            continue;
        } else if (written <= 0) {
            free(fpath);
            return NULL;
        }
        fpath[written] = '\0';
         int64_t result_u32len = 0;
        h64wchar *result_u32 = AS_U32(fpath, &result_u32len);
        free(fpath);
        *out_len = result_u32len;
        return result_u32;
        #endif
    }
    #endif
}

int filesys32_CreateDirectoryRecursively(
        h64wchar *path, int64_t pathlen, int user_readable_only
        ) {
    // Normalize file paths:
    int64_t cleanpathlen = 0;
    h64wchar *cleanpath = filesys32_Normalize(
        path, pathlen, &cleanpathlen
    );
    if (!cleanpath)
        return FS32_MKDIRERR_OUTOFMEMORY;

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
            return FS32_MKDIRERR_OTHERERROR;
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
            return FS32_MKDIRERR_OTHERERROR;
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
        return FS32_MKDIRERR_SUCCESS;
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
        if (result < 0 && result != FS32_MKDIRERR_TARGETALREADYEXISTS)
            return result;
        // Advance to next component:
        continue;
    }
    free(cleanpath);
    return FS32_MKDIRERR_SUCCESS;
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