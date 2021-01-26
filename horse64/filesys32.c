// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#define _FILE_OFFSET_BITS 64
#ifndef __USE_LARGEFILE64
#define __USE_LARGEFILE64 1
#endif
#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif
#define _LARGEFILE_SOURCE

#include <assert.h>
#include <errno.h>
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
#include "widechar.h"


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
        path32, path32len, targetpath,
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
        *error = FS32_REMOVEFILEERR_OTHERERROR;
        if (werror == ERROR_DIRECTORY_NOT_SUPPORTED ||
                werror == ERROR_DIRECTORY) {
            if (RemoveDirectoryW(targetpath) != TRUE) {
                free(targetpath);
                werror = GetLastError();
                *error = FS32_REMOVEFILEERR_OTHERERROR;
                if (werror == ERROR_PATH_NOT_FOUND ||
                        werror == ERROR_FILE_NOT_FOUND ||
                        werror == ERROR_INVALID_PARAMETER ||
                        werror == ERROR_INVALID_NAME ||
                        werror == ERROR_INVALID_DRIVE)
                    *error = FS32_REMOVEERR_NOSUCHTARGET;
                else if (werror == ERROR_ACCESS_DENIED)
                    *error = FS32_REMOVEERR_NOPERMISSION;
                else if (werror == ERROR_NOT_ENOUGH_MEMORY)
                    *error = FS32_REMOVEERR_OUTOFMEMORY;
                return 0;
            }
            free(targetpath);
            *error = FS32_REMOVEFILEERR_SUCCESS;
            return 1;
        }
        free(targetpath);
        if (werror == ERROR_PATH_NOT_FOUND ||
                werror == ERROR_FILE_NOT_FOUND ||
                werror == ERROR_INVALID_PARAMETER ||
                werror == ERROR_INVALID_NAME ||
                werror == ERROR_INVALID_DRIVE)
            *error = FS32_REMOVEERR_NOSUCHTARGET;
        else if (werror == ERROR_ACCESS_DENIED)
            *error = FS32_REMOVEERR_NOPERMISSION;
        else if (werror == ERROR_NOT_ENOUGH_MEMORY)
            *error = FS32_REMOVEERR_OUTOFMEMORY;
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
        return 0;
    }
    *error = FS32_REMOVEERR_SUCCESS;
    #endif
    return 1;
}

int filesys32_ListFolder(
        const h64wchar *path32, int64_t path32len,
        h64wchar ***contents, int64_t **contentslen,
        int returnFullPath, int *error
        ) {
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
        path32, path32len, folderpath,
        sizeof(*folderpath) * (path32len * 2 + 1),
        &folderpathlen, 1
    );
    if (!result || folderpathlen >= (path32len * 2)) {
        free(folderpath);
        *error = FS32_LISTFOLDERERR_OUTOFMEMORY;
        return NULL;
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
                werror == ERROR_FILE_NOT_FOUND ||
                werror == ERROR_INVALID_PARAMETER ||
                werror == ERROR_INVALID_NAME ||
                werror == ERROR_INVALID_DRIVE)
            *error = FS32_LISTFOLDERERR_TARGETNOTDIRECTORY;
        else if (werror == ERROR_ACCESS_DENIED)
            *error = FS32_LISTFOLDERERR_NOPERMISSION;
        else if (werror == ERROR_NOT_ENOUGH_MEMORY)
            *error = FS32_LISTFOLDERERR_OUTOFMEMORY;
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
    DIR *d = opendir(
        (strlen(p) > 0 ? p : ".")
    );
    if (!d) {
        free(p);
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
    free(p);
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
        p[plen - 1] = '\0';
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
    char *cwd = filesys_GetCurrentDirectory();
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