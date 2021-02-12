// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bytecode.h"
#include "compiler/ast.h"
#include "compiler/astparser.h"
#include "compiler/codegen.h"
#include "compiler/codemodule.h"
#include "compiler/compileproject.h"
#include "compiler/main.h"
#include "compiler/result.h"
#include "compiler/scoperesolver.h"
#include "filesys.h"
#include "filesys32.h"
#include "hash.h"
#include "nonlocale.h"
#include "secrandom.h"
#include "threadablechecker.h"
#include "uri32.h"
#include "vfs.h"
#include "widechar.h"

// #define DEBUG_COMPILEPROJECT

h64compileproject *compileproject_New(
        const h64wchar *basefolderuri, int64_t basefolderurilen
        ) {
    if (!basefolderuri)
        return NULL;

    h64compileproject *pr = malloc(sizeof(*pr));
    if (!pr)
        return NULL;
    memset(pr, 0, sizeof(*pr));
    warningconfig_Init(&pr->warnconfig);

    uri32info *uinfo = uri32_ParseExU8Protocol(
        basefolderuri, basefolderurilen, "file",
        URI32_PARSEEX_FLAG_GUESSPORT
    );
    if (!uinfo || !uinfo->path || !uinfo->protocol ||
            h64casecmp_u32u8(uinfo->protocol,
                uinfo->protocollen, "file") != 0) {
        uri32_Free(uinfo);
        free(pr);
        return NULL;
    }

    int64_t slen = 0;
    h64wchar *s = filesys32_ToAbsolutePath(
        uinfo->path, uinfo->pathlen, &slen
    );
    uri32_Free(uinfo); uinfo = NULL;
    if (!s) {
        compileproject_Free(pr);
        return NULL;
    }
    pr->basefolder = filesys32_Normalize(
        s, slen, &pr->basefolderlen
    );
    free(s);
    if (!pr->basefolder) {
        compileproject_Free(pr);
        return NULL;
    }

    pr->astfilemap = hash_NewStringMap(32);
    if (!pr->astfilemap) {
        compileproject_Free(pr);
        return NULL;
    }

    pr->resultmsg = malloc(sizeof(*pr->resultmsg));
    if (!pr->resultmsg) {
        compileproject_Free(pr);
        return NULL;
    }
    memset(pr->resultmsg, 0, sizeof(*pr->resultmsg));
    pr->resultmsg->success = 1;

    pr->program = h64program_New();
    if (!pr->program) {
        compileproject_Free(pr);
        return NULL;
    }

    #ifdef DEBUG_COMPILEPROJECT
    h64printf("horsec: debug: compileproject_New -> %p\n", pr);
    #endif

    return pr;
}

uri32info *compileproject_FilePathToURI(
        const h64wchar *fileuri, int64_t fileurilen,
        int makeabs
        ) {
    int _couldbevfs = 0;
    if (!strstr_u32u8(fileuri, fileurilen, "://"))
        _couldbevfs = 1;
    uri32info *uinfo = uri32_ParseExU8Protocol(
        fileuri, fileurilen, "file", URI32_PARSEEX_FLAG_GUESSPORT
    );
    if (!uinfo || !uinfo->path || !uinfo->protocol ||
            (h64casecmp_u32u8(
                uinfo->protocol, uinfo->protocollen, "file") != 0 &&
             h64casecmp_u32u8(
                 uinfo->protocol, uinfo->protocollen, "vfs") != 0)) {
        uri32_Free(uinfo);
        return NULL;
    }
    if (_couldbevfs &&
            h64casecmp_u32u8(
                uinfo->protocol, uinfo->protocollen, "file") == 0 &&
            !filesys32_IsAbsolutePath(uinfo->path, uinfo->pathlen)) {
        int exists = 0;
        if (!vfs_ExistsU32(uinfo->path,
                uinfo->pathlen, &exists,
                VFSFLAG_NO_REALDISK_ACCESS)) {
            uri32_Free(uinfo);
            return NULL;
        }
        if (exists) {
            free(uinfo->protocol);
            uinfo->protocol = strdup_u32u8("vfs", &uinfo->protocollen);
            if (!uinfo->protocol) {
                uri32_Free(uinfo);
                return NULL;
            }
            int64_t newpathlen = 0;
            h64wchar *newpath = filesys32_Normalize(
                uinfo->path, uinfo->pathlen, &newpathlen
            );
            if (!newpath) {
                uri32_Free(uinfo);
                return NULL;
            }
            free(uinfo->path);
            uinfo->path = newpath;
            uinfo->pathlen = newpathlen;
        }
    }
    if (h64casecmp_u32u8(uinfo->protocol,
            uinfo->protocollen, "file") == 0 &&
            !filesys32_IsAbsolutePath(uinfo->path,
                uinfo->pathlen) && makeabs) {
        int64_t newpathlen = 0;
        h64wchar *newpath = filesys32_ToAbsolutePath(
            uinfo->path, uinfo->pathlen, &newpathlen
        );
        if (newpath) {
            int64_t newpath2len = 0;
            h64wchar *newpath2 = filesys32_Normalize(
                newpath, newpathlen, &newpath2len
            );
            free(newpath);
            newpath = newpath2;
            newpathlen = newpath2len;
        }
        if (!newpath) {
            uri32_Free(uinfo);
            return NULL;
        }
        free(uinfo->path);
        uinfo->path = newpath;
        uinfo->pathlen = newpathlen;
    }
    return uinfo;
}

uri32info *compileproject_URIRelPathToBase(
        const h64wchar *basepath, int64_t basepathlen,
        const h64wchar *fileuri, int64_t fileurilen,
        int *outofmemory
        ) {
    if (!fileuri || !basepath) {
        if (outofmemory) *outofmemory = 1;
        return NULL;
    }

    uri32info *uinfo = compileproject_FilePathToURI(
        fileuri, fileurilen, 1
    );
    if (!uinfo) {
        if (outofmemory) *outofmemory = 1;
        return NULL;
    }

    if (h64casecmp_u32u8(uinfo->protocol, uinfo->protocollen,
            "file") == 0 ||
            h64casecmp_u32u8(uinfo->protocol, uinfo->protocollen,
            "vfs") == 0) {
        // Convert URIs to be relative to given base.
        #ifndef NDEBUG
        if (h64casecmp_u32u8(uinfo->protocol,
                uinfo->protocollen, "vfs") == 0) {
            if (filesys32_IsAbsolutePath(basepath, basepathlen)) {
                // This is invalid.
                if (outofmemory) *outofmemory = 0;
                uri32_Free(uinfo);
                return NULL;
            }
        } else if (h64casecmp_u32u8(
                uinfo->protocol, uinfo->protocollen, "file") == 0) {
            if (!filesys32_IsAbsolutePath(basepath, basepathlen)) {
                // This is invalid.
                if (outofmemory) *outofmemory = 0;
                uri32_Free(uinfo);
                return NULL;
            }
        }
        #endif
        int64_t newpathlen = 0;
        h64wchar *newpath = filesys32_TurnIntoPathRelativeTo(
            uinfo->path, uinfo->pathlen,
            basepath, basepathlen, &newpathlen
        );
        if (newpath) {
            int64_t newpath2len = 0;
            h64wchar *newpath2 = filesys32_Normalize(
                newpath, newpathlen, &newpath2len
            );
            free(newpath);
            newpath = newpath2;
            newpathlen = newpath2len;
        }
        if (!newpath) {
            uri32_Free(uinfo);
            uinfo = NULL;
            if (outofmemory) *outofmemory = 1;
            return NULL;
        }
        if (newpathlen >= (int64_t)strlen("../") &&
                newpath[0] == '.' && newpath[1] == '.' &&
                (newpath[2] == '/'
                #if defined(_WIN32) || defined(_WIN64)
                || newpath[2] == '\\'
                #endif
                )) {
            // A path escaping out of base is not allowed.
            uri32_Free(uinfo);
            uinfo = NULL;
            if (outofmemory) *outofmemory = 0;
            return NULL;
        }
        free(uinfo->path);
        uinfo->path = newpath;
        uinfo->pathlen = newpathlen;
    }
    if (!uinfo) {
        if (outofmemory) *outofmemory = 1;
        return NULL;
    }
    return uinfo;
}

uri32info *compileproject_ToProjectRelPathURI(
        h64compileproject *pr,
        const h64wchar *fileuri, int64_t fileurilen,
        int *outofmemory
        ) {
    if (!fileuri || !pr || !pr->basefolder) {
        if (outofmemory) *outofmemory = 0;
        return NULL;
    }
    int isvfs = 0;
    {
        uri32info *fileuri_info = uri32_ParseExU8Protocol(
            fileuri, fileurilen, "file", URI32_PARSEEX_FLAG_GUESSPORT
        );
        if (!fileuri_info) {
            if (outofmemory) *outofmemory = 1;
            return NULL;
        }
        isvfs = (
            fileuri_info->protocol != NULL &&
            h64casecmp_u32u8(
                fileuri_info->protocol,
                fileuri_info->protocollen, "vfs"
            ) == 0
        );
        uri32_Free(fileuri_info);
    }
    return compileproject_URIRelPathToBase(
        (isvfs ? (const h64wchar *)"" : pr->basefolder),
        (isvfs ? 0 : pr->basefolderlen),
        fileuri, fileurilen, outofmemory
    );
}

int compileproject_GetAST(
        h64compileproject *pr,
        const h64wchar *fileuri, int64_t fileurilen,
        h64ast **out_ast, char **error
        ) {
    int relfileoom = 0;
    uri32info *relfileuri = compileproject_ToProjectRelPathURI(
        pr, fileuri, fileurilen, &relfileoom
    );
    if (!relfileuri) {
        if (relfileoom) {
            *error = strdup("out of memory");
            *out_ast = NULL;
            return 0;
        }
        char msg[] = "cannot get AST of file outside of project root: ";
        const char *fileuriu8 = AS_U8_TMP(fileuri, fileurilen);
        *error = malloc(
            strlen(msg) + (fileuriu8 ? strlen(fileuriu8) : 0) + 1
        );
        if (*error) {
            memcpy(
                *error, msg, strlen(msg)
            );
            memcpy(
                (*error) + strlen(msg),
                (fileuriu8 ? fileuriu8 : ""),
                (fileuriu8 ? strlen(fileuriu8) : 1) + 1
            );
        }
        *out_ast = NULL;
        return 0;
    }
 
    char *hashmap_key = NULL;
    {
        int64_t uriu32len = 0;
        h64wchar *uriu32 = (
            uri32_Dump(relfileuri, &uriu32len)
        );
        if (uriu32) {
            hashmap_key = AS_U8(uriu32, uriu32len);
            free(uriu32);
        }
    }
    if (!hashmap_key) {
        uri32_Free(relfileuri);
        *error = strdup("out of memory");
        *out_ast = NULL;
        return 1;
    }
    uint64_t entry;
    if (hash_StringMapGet(
            pr->astfilemap, hashmap_key, &entry
            ) && entry > 0) {
        h64ast *resultptr = (h64ast*)(uintptr_t)entry;
        free(hashmap_key);
        uri32_Free(relfileuri);
        *out_ast = resultptr;
        *error = NULL;
        return 1;
    }

    int isvfs = (h64casecmp_u32u8(relfileuri->protocol,
        relfileuri->protocollen, "vfs") == 0);
    if (!isvfs) {
        int64_t trueabspathlen = 0;
        h64wchar *trueabspath = filesys32_Join(
            pr->basefolder, pr->basefolderlen,
            relfileuri->path, relfileuri->pathlen,
            &trueabspathlen
        );
        if (!trueabspath) {
            free(hashmap_key);
            uri32_Free(relfileuri);
            *error = strdup("alloc fail (abs file path)");
            *out_ast = NULL;
            return 0;
        }
        free(relfileuri->path);
        relfileuri->path = trueabspath;
        relfileuri->pathlen = trueabspathlen;
    }

    #ifdef DEBUG_COMPILEPROJECT
    h64printf(
        "horsec: debug: compileproject_GetAST -> parsing %s\n",
        relfileuri->path
    );
    #endif

    h64ast *result = codemodule_GetASTUncached(
        pr, relfileuri, &pr->warnconfig
    );
    assert(!fileuri || !result || result->fileuri);
    if (!result) {
        free(hashmap_key);
        uri32_Free(relfileuri);
        *error = strdup("alloc fail (get uncached AST)");
        *out_ast = NULL;
        return 0;
    }

    // Add warnings & errors to collected ones in compileproject:
    if (!result_TransferMessages(
            &result->resultmsg, pr->resultmsg
            )) {
        result_FreeContents(pr->resultmsg);
        pr->resultmsg->success = 0;
        free(hashmap_key);
        uri32_Free(relfileuri);
        ast_FreeContents(result);
        free(result);
        *error = strdup("alloc fail (transfer errors)");
        *out_ast = NULL;
        return 0;
    }
    result_RemoveMessageDuplicates(pr->resultmsg);

    if (!hash_StringMapSet(
            pr->astfilemap, hashmap_key, (uintptr_t)result
            )) {
        free(hashmap_key);
        uri32_Free(relfileuri);
        ast_FreeContents(result);
        free(result);
        *error = strdup("alloc fail (ast file map set)");
        *out_ast = NULL;
        return 0;
    }
    pr->astfilemap_count++;
    *out_ast = result;
    *error = NULL;
    free(hashmap_key);
    uri32_Free(relfileuri);
    return 1;
}

int _compileproject_astfreecallback(
        __attribute__((unused)) hashmap *map,
        __attribute__((unused)) const char *key, uint64_t number,
        __attribute__((unused)) void *userdata
        ) {
    h64ast *resultptr = (h64ast*)(uintptr_t)number;
    if (resultptr) {
        result_FreeContents(&resultptr->resultmsg);
        ast_FreeContents(resultptr);
        free(resultptr);
    }
    return 1;
}

int _free_fakevarinitexpr_cb(
        ATTR_UNUSED hashmap *map, ATTR_UNUSED const char *bytes,
        ATTR_UNUSED uint64_t byteslen, uint64_t number,
        ATTR_UNUSED void *userdata
        ) {
    h64expression *fakefunc = (h64expression *)(uintptr_t)number;
    ast_FreeExpression(fakefunc);
    return 1;
}

void compileproject_Free(h64compileproject *pr) {
    if (!pr) return;

    threadablechecker_FreeGraphInfoFromProject(pr);

    free(pr->basefolder);

    if (pr->_tempglobalfakeinitfunc) {
        ast_FreeExpression(pr->_tempglobalfakeinitfunc);
    }
    free(pr->_class_was_propagated);
    if (pr->_tempclassesfakeinitfunc_map) {
        int result = hash_BytesMapIterate(
            pr->_tempclassesfakeinitfunc_map,
            _free_fakevarinitexpr_cb, NULL
        );
        if (!result)
            h64fprintf(stderr,
                "horsec: warning: out of memory during "
                "iteration in compileproject_Free, "
                "likely memory leaking\n"
            );
        hash_FreeMap(pr->_tempclassesfakeinitfunc_map);
    }
    if (pr->astfilemap) {
        hash_StringMapIterate(
            pr->astfilemap, &_compileproject_astfreecallback, NULL
        );
        hash_FreeMap(pr->astfilemap);
    }
    if (pr->resultmsg) {
        result_FreeContents(pr->resultmsg);
        free(pr->resultmsg);
    }
    if (pr->program) {
        h64program_Free(pr->program);
    }

    free(pr);
}

uri32info *compileproject_GetFileSubProjectURI(
        h64compileproject *pr,
        const h64wchar *sourcefileuri, int64_t sourcefileurilen,
        char **subproject_name, int *outofmemory
        ) {
    // Parse sourcefileuri given to us:
    uri32info *uinfo = uri32_ParseExU8Protocol(
        sourcefileuri, sourcefileurilen, "file",
        URI32_PARSEEX_FLAG_GUESSPORT
    );
    if (!uinfo || !uinfo->path || !uinfo->protocol ||
            (h64casecmp_u32u8(
                uinfo->protocol, uinfo->protocollen, "file") != 0 &&
             h64casecmp_u32u8(
                uinfo->protocol, uinfo->protocollen, "vfs") != 0)) {
        if (outofmemory && !uinfo) *outofmemory = 1;
        if (outofmemory && uinfo) *outofmemory = 0;
        uri32_Free(uinfo);
        return NULL;
    }
    int isvfs = (h64casecmp_u32u8(
        uinfo->protocol, uinfo->protocollen, "vfs"
    ) == 0);

    // Turn it into a relative URI, which is relative to our main project:
    int relfilepathoom = 0;
    if (!isvfs) {
        uri32info *relfileuri = compileproject_ToProjectRelPathURI(
            pr, uinfo->path, uinfo->pathlen, &relfilepathoom
        );
        if (!relfileuri) {
            uri32_Free(uinfo);
            if (outofmemory && relfilepathoom) *outofmemory = 1;
            if (outofmemory && !relfilepathoom) *outofmemory = 0;
            return NULL;
        }
        uri32_Free(uinfo);
        uinfo = relfileuri;
    }

    // If path starts with ./horse_modules/somemodule/<stuff>/ then
    // we want to return ./horse_modules/somemodule/ as root:
    int i = 0;
    while (i < 2) {
        char hmodules_path[] = "horse_modules_builtin";
        if (i == 0) {
            if (!isvfs) {
                // Only VFS paths may refer to builtin modules.
                i++;
                continue;
            }
        }
        if (i == 1)
            memcpy(hmodules_path, "horse_modules",
                   strlen("horse_modules") + 1);
        if (uinfo->pathlen <= (int64_t)strlen(hmodules_path)) {
            // Too short to have horse_modules + sub_folder in path.
            i++;
            continue;
        }
        // Verify it starts with correct path segment:
        char buf[64];
        char *uinfo_path_u8 = AS_U8(uinfo->path, uinfo->pathlen);
        if (!uinfo_path_u8) {
            uri32_Free(uinfo);
            if (outofmemory && relfilepathoom) *outofmemory = 1;
            if (outofmemory && !relfilepathoom) *outofmemory = 0;
            return NULL;
        }
        assert(strlen(hmodules_path) <= strlen(uinfo_path_u8));
        memcpy(buf, uinfo_path_u8, strlen(hmodules_path));
        free(uinfo_path_u8);
        buf[strlen(hmodules_path)] = '\0';
        if (
                #if defined(__linux__) || defined(__LINUX__) || \
                    defined(__ANDROID__)
                strcmp(buf, hmodules_path) == 0
                #else
                h64casecmp(buf, hmodules_path) == 0
                #endif
                && (uinfo->path[strlen(hmodules_path)] == '/'
                #if defined(_WIN32) || defined(_WIN64)
                || uinfo->path[strlen(hmodules_path)] == '\\'
                #endif
                )) {
            int k = strlen(hmodules_path) + 1;  // path + '/'
            while (k < uinfo->pathlen && uinfo->path[k] != '/'
                    #if defined(_WIN32) || defined(_WIN64)
                    && uinfo->path[k] != '\\'
                    #endif
                    )
                k++;
            if (k < uinfo->pathlen) {
                k++;  // go past dir separator
                // Extract actual horse_modules/<name>/(REST CUT OFF) path:
                int64_t relfilepath_shortened_len = 0; 
                h64wchar *relfilepath_shortened = strdupu32(
                    uinfo->path, uinfo->pathlen, &relfilepath_shortened_len
                );
                if (!relfilepath_shortened) {
                    uri32_Free(uinfo);
                    if (outofmemory) *outofmemory = 0;
                    return NULL;
                }
                int firstslashindex = -1;
                int secondslashindex = -1;
                int slashcount = 0;
                int j = 0;
                while (j < relfilepath_shortened_len) {
                    if (relfilepath_shortened[j] == '/'
                            #if defined(_WIN32) || defined(_WIN64)
                            || relfilepath_shortened[j] == '\\'
                            #endif
                            ) {
                        slashcount++;
                        if (slashcount == 1) {
                            firstslashindex = j;
                        } else if (slashcount == 2) {
                            secondslashindex = j;
                            relfilepath_shortened_len = j;
                            break;
                        }
                    }
                    j++;
                }
                if (slashcount != 2 ||
                        secondslashindex <= firstslashindex + 1) {
                    free(relfilepath_shortened);
                    uri32_Free(uinfo);
                    if (outofmemory) *outofmemory = 0;
                    return NULL;
                }

                // Extract project name from our cut off result path:
                char *project_name = malloc(
                    (secondslashindex - firstslashindex) * 5 + 2
                );
                if (!project_name) {
                    free(relfilepath_shortened);
                    uri32_Free(uinfo);
                    if (outofmemory) *outofmemory = 1;
                    return NULL;
                }
                {  // We need to extrac the project name as utf-32 first:
                    h64wchar *project_name_u32 = malloc(
                        (secondslashindex - (firstslashindex + 1)) *
                            sizeof(*project_name_u32)
                    );
                    if (!project_name_u32) {
                        free(project_name);
                        free(relfilepath_shortened);
                        uri32_Free(uinfo);
                        if (outofmemory) *outofmemory = 1;
                        return NULL;
                    }
                    memcpy(
                        project_name_u32, uinfo->path + firstslashindex + 1,
                        (secondslashindex - (firstslashindex + 1)) *
                            sizeof(*project_name_u32)
                    );  // extract name.
                    // Convert the project name to utf-8:
                    int64_t out_len = 0;
                    int result = utf32_to_utf8(
                        project_name_u32, secondslashindex -
                        (firstslashindex + 1), project_name,
                        (secondslashindex - firstslashindex) * 5 + 2,
                        &out_len, 0, 0
                    );
                    free(project_name_u32);
                    if (!result || out_len >=
                            (secondslashindex - firstslashindex) * 5 + 2) {
                        // Conversion failed. Invalid project name!
                        free(project_name);
                        free(relfilepath_shortened);
                        uri32_Free(uinfo);
                        if (outofmemory) *outofmemory = 0;
                        return NULL;
                    }
                    project_name[out_len] = '\0';
                }

                // Turn relative project path into absolute one:
                int64_t parent_abs_len = 0;
                h64wchar *parent_abs = filesys32_ToAbsolutePath(
                    pr->basefolder, pr->basefolderlen, &parent_abs_len
                );  // needs to be relative to main project path
                if (!parent_abs) {
                    uri32_Free(uinfo);
                    free(relfilepath_shortened);
                    if (outofmemory) *outofmemory = 0;
                    return NULL;
                }
                int64_t resultpathlen = 0;
                h64wchar *resultpath = NULL;
                if (!isvfs) {
                    resultpath = filesys32_Join(
                        parent_abs, parent_abs_len,
                        relfilepath_shortened,
                        relfilepath_shortened_len,
                        &resultpathlen
                    );
                } else {
                    resultpath = strdupu32(
                        relfilepath_shortened,
                        relfilepath_shortened_len,
                        &resultpathlen
                    );
                }
                free(parent_abs);
                parent_abs = NULL;
                free(relfilepath_shortened);
                relfilepath_shortened = NULL;
                if (resultpath) {
                    int64_t resultoldlen = resultpathlen;
                    h64wchar *resultold = resultpath;
                    resultpath = filesys32_Normalize(
                        resultold, resultoldlen, &resultpathlen
                    );
                    free(resultold);
                }
                uri32_Free(uinfo);
                if (resultpath) {
                    if (subproject_name) *subproject_name = project_name;
                    if (outofmemory) *outofmemory = 0;
                } else {
                    if (outofmemory) *outofmemory = 1;
                }
                // Ok, return as URI with protocol header stuff:
                uri32info *resulturi = uri32_ParseExU8Protocol(
                    resultpath, resultpathlen,
                    (isvfs ? "vfs" : "file"),
                    URI32_PARSEEX_FLAG_GUESSPORT
                );
                free(resultpath);
                resultpath = NULL;
                if (!resulturi)
                    if (outofmemory) *outofmemory = 1;
                return resulturi;
            }
        }
        i++;
        continue;
    }
    // Not inside horse_modules module folder, so just return the
    // regular project root:
    uri32_Free(uinfo);
    if (subproject_name) {
        *subproject_name = strdup("");
        if (!*subproject_name) {
            if (outofmemory) *outofmemory = 1;
            return NULL;
        }
    }
    int64_t resultpathlen = 0;
    h64wchar *resultpath = NULL;
    if (isvfs) {
        resultpath = (h64wchar *)strdup("");  // (cwd)
        resultpathlen = 0;  // just an empty string.
    } else {
        resultpath = filesys32_ToAbsolutePath(
            pr->basefolder, pr->basefolderlen, &resultpathlen
        );
        if (resultpath) {
            int64_t resultpathlen2 = 0;
            h64wchar *result2 = uri32_Normalize(
                resultpath, resultpathlen, 1, &resultpathlen2
            );
            free(resultpath);
            resultpath = result2;
            resultpathlen = resultpathlen2;
        }
    }
    if (!resultpath) {
        if (outofmemory) *outofmemory = 1;
        return NULL;
    }
    // Ok, return as URI with protocol header stuff:
    uri32info *resulturi = uri32_ParseExU8Protocol(
        resultpath, resultpathlen,
        (isvfs ? "vfs" : "file"), URI32_PARSEEX_FLAG_GUESSPORT
    );
    free(resultpath);
    resultpath = NULL;
    if (!resulturi) {
        if (outofmemory) *outofmemory = 1;
    }
    if (outofmemory) *outofmemory = 0;
    return resulturi;
}

int compileproject_DoesImportMapToCFuncs(
        h64compileproject *pr,
        const char **import_elements, int import_elements_count,
        const char *library_source,
        int print_debug_info, int *outofmemory
        ) {
    if (!pr || !pr->basefolder) {
        if (outofmemory) *outofmemory = 0;
        return 0;
    }
    char *import_modpath = NULL;
    int import_modpath_len = 0;
    int i = 0;
    while (i < import_elements_count) {
        if (i + 1 < import_elements_count)
            import_modpath_len++; // dir separator
        import_modpath_len += strlen(import_elements[i]);
        i++;
    }
    import_modpath = malloc(import_modpath_len + 1);
    if (!import_modpath) {
        if (outofmemory) *outofmemory = 1;
        return 0;
    }
    {
        char *p = import_modpath;
        i = 0;
        while (i < import_elements_count) {
            memcpy(p, import_elements[i], strlen(import_elements[i]));
            p += strlen(import_elements[i]);
            if (i + 1 < import_elements_count) {
                *p = '.';
                p++;
            }
            i++;
        }
        *p = '\0';
    }
    assert(import_modpath_len == (int)strlen(import_modpath));
    if (print_debug_info)
        h64printf(
            "horsec: debug: cimport: finding module: %s ("
            "library: %s) in C modules\n",
            import_modpath, library_source
        );
    h64modulesymbols *msymbols = h64debugsymbols_GetModule(
        pr->program->symbols, import_modpath,
        library_source, 0
    );
    if (!msymbols) {
        if (print_debug_info)
            h64printf(
                "horsec: debug: cimport: no such module found\n"
            );
        free(import_modpath);
        if (outofmemory) *outofmemory = 0;
        return 0;
    } else {
        if (msymbols->noncfunc_count < msymbols->func_count ||
                msymbols->func_count == 0) {
            if (print_debug_info)
                h64printf(
                    "horsec: debug: cimport: module found but "
                    "contains non-C functions, or no functions\n"
                );
            free(import_modpath);
            if (outofmemory) *outofmemory = 0;
            return 0;
        }
    }
    free(import_modpath);
    if (print_debug_info)
        h64printf(
            "horsec: debug: cimport: success, module found "
            "with just plain C functions\n"
        );
    return 1;
}

h64wchar *compileproject_ResolveImportToFile(
        h64compileproject *pr,
        const h64wchar *sourcefileuri, int64_t sourcefileurilen,
        const char **import_elements, int import_elements_count,
        const char *library_source,
        int print_debug_info,
        int64_t *out_len,
        int *outofmemory
        ) {
    if (!pr || !pr->basefolder || !sourcefileuri) {
        if (outofmemory) *outofmemory = 0;
        return NULL;
    }
    char *import_modpath = NULL;
    char *import_relpath = NULL;
    int import_relpath_len = 0;
    int i = 0;
    while (i < import_elements_count) {
        if (i + 1 < import_elements_count)
            import_relpath_len++; // dir separator
        import_relpath_len += strlen(import_elements[i]);
        i++;
    }
    import_relpath_len += strlen(".h64");
    import_relpath = malloc(import_relpath_len + 1);
    if (!import_relpath) {
        if (outofmemory) *outofmemory = 1;
        return NULL;
    }
    import_modpath = malloc(import_relpath_len - strlen(".h64") + 1);
    if (!import_modpath) {
        if (outofmemory) *outofmemory = 1;
        return NULL;
    }
    {
        char *p = import_relpath;
        char *p2 = import_modpath;
        i = 0;
        while (i < import_elements_count) {
            memcpy(p, import_elements[i], strlen(import_elements[i]));
            p += strlen(import_elements[i]);
            memcpy(p2, import_elements[i], strlen(import_elements[i]));
            p2 += strlen(import_elements[i]);
            if (i + 1 < import_elements_count) {
                #if defined(_WIN32) || defined(_WIN64)
                *p = '\\';
                #else
                *p = '/';
                #endif
                p++;
                *p2 = '.';
                p2++;
            }
            i++;
        }
        *p2 = '\0';
        memcpy(p, ".h64", strlen(".h64") + 1);
    }
    assert(import_relpath_len == (int)strlen(import_relpath));
    if (print_debug_info)
        h64printf(
            "horsec: debug: import: finding module: %s (relpath: %s, "
            "library: %s) on disk\n",
            import_modpath, import_relpath, library_source
        );
    free(import_modpath);  // we just needed that for debug info output
    import_modpath = NULL;
    if (library_source) {
        // Load module from horse_modules library folder.
        // We'll check the VFS-mounted horse_modules_builtin first
        // (which e.g. has the built-in "core" module matched to this
        // compiler instance) and then the horse_modules folder which
        // can be either on disk or in the VFS.

        // Allocate the path where we expect the file to be at:
        int library_sourced_path_u8_len = (
            strlen("horse_modules_builtin") + 1 +
            strlen(library_source) + 1 +
            import_relpath_len + 1
        );
        char *library_sourced_path_u8 = malloc(
            library_sourced_path_u8_len
        );
        if (!library_sourced_path_u8) {
            free(import_relpath);
            if (outofmemory) *outofmemory = 1;
            return NULL;
        }

        // Copy in actual path contents:
        memcpy(library_sourced_path_u8, "horse_modules_builtin",
               strlen("horse_modules_builtin"));
        #if defined(_WIN32) || defined(_WIN64)
        library_sourced_path_u8[
            strlen("horse_modules_builtin")
        ] = '\\';
        #else
        library_sourced_path_u8[
            strlen("horse_modules_builtin")
        ] = '/';
        #endif
        memcpy(library_sourced_path_u8 + strlen("horse_modules_builtin/"),
               library_source, strlen(library_source));
        #if defined(_WIN32) || defined(_WIN64)
        library_sourced_path_u8[strlen("horse_modules_builtin") + 1 +
            strlen(library_source)] = '\\';
        #else
        library_sourced_path_u8[strlen("horse_modules_builtin") + 1 +
            strlen(library_source)] = '/';
        #endif
        memcpy(library_sourced_path_u8 +
               strlen("horse_modules_builtin/") +
               strlen(library_source) + 1,
               import_relpath, import_relpath_len + 1);
        free(import_relpath); import_relpath = NULL;

        // Assemble horse_modules-VFS and absolute disk file path:
        char *library_sourced_path_u8_external = (
            strdup(library_sourced_path_u8)
        );
        if (!library_sourced_path_u8_external) {
            free(library_sourced_path_u8);
            if (outofmemory) *outofmemory = 1;
            return NULL;
        }
        memmove(
            library_sourced_path_u8_external + strlen("horse_modules"),
            library_sourced_path_u8_external +
                strlen("horse_modules_builtin"),
            strlen(library_sourced_path_u8_external) + 1 -
            strlen("horse_modules_builtin")
        );

        // Now, convert the library paths to U32 for appending:
        h64wchar *library_sourced_path = NULL;
        int64_t library_sourced_path_len = 0;
        h64wchar *library_sourced_path_external = NULL;
        int64_t library_sourced_path_external_len = 0;
        library_sourced_path = AS_U32(
            library_sourced_path_u8, &library_sourced_path_len
        );
        library_sourced_path_external = AS_U32(
            library_sourced_path_u8_external,
            &library_sourced_path_external_len
        );
        free(library_sourced_path_u8_external);
        free(library_sourced_path_u8);
        if (!library_sourced_path || !library_sourced_path_external) {
            free(import_relpath);
            free(library_sourced_path);
            free(library_sourced_path_external);
            if (outofmemory) *outofmemory = 1;
            return NULL;
        }

        int64_t fullpathlen = 0;
        h64wchar *fullpath = filesys32_Join(
            pr->basefolder, pr->basefolderlen,
            library_sourced_path_external,
            library_sourced_path_external_len,
            &fullpathlen
        );
        if (!fullpath) {
            free(library_sourced_path);
            free(library_sourced_path_external);
            if (outofmemory) *outofmemory = 1;
            return NULL;
        }

        if (print_debug_info)
            h64printf(
                "horsec: debug: import: checking library paths: %s, %s\n",
                library_sourced_path, library_sourced_path_external
            );

        // Check "horse_modules_builtin" first:
        {
            int _vfs_exists_internal = 0;
            if (!vfs_ExistsU32(
                    library_sourced_path,
                    library_sourced_path_len,
                    &_vfs_exists_internal,
                    VFSFLAG_NO_REALDISK_ACCESS)) {
                free(fullpath);
                free(library_sourced_path);
                free(library_sourced_path_external);
                if (outofmemory) *outofmemory = 1;
                return NULL;
            }
            if (_vfs_exists_internal) {
                if (print_debug_info)
                    h64printf(
                        "horsec: debug: import: success, found at %s "
                        "(VFS)\n",
                        library_sourced_path
                    );
                free(fullpath);
                free(library_sourced_path_external);
                if (outofmemory) *outofmemory = 0;
                h64wchar *resulturi = NULL;
                int64_t resulturilen = 0;
                {
                    uri32info *result = uri32_ParseExU8Protocol(
                        library_sourced_path, library_sourced_path_len,
                        "vfs", URI32_PARSEEX_FLAG_GUESSPORT
                    );
                    if (result && result->protocol &&
                            h64casecmp_u32u8(result->protocol,
                                result->protocollen, "file") == 0) {
                        free(result->protocol);
                        result->protocol = strdup_u32u8(
                            "vfs", &result->protocollen
                        );
                        if (!result->protocol) {
                            uri32_Free(result);
                            result = NULL;
                        }
                    }
                    free(library_sourced_path);
                    if (result) {
                        resulturi = uri32_Dump(result, &resulturilen);
                        uri32_Free(result);
                    }
                }
                if (!resulturi)
                    if (outofmemory) *outofmemory = 1;
                *out_len = resulturilen;
                return resulturi;
            }
            free(library_sourced_path);
            library_sourced_path = NULL;
        }

        // Check if it exists in "horse_modules" and return result:
        int _vfs_exists = 0;
        if (!vfs_ExistsExU32(
                fullpath, fullpathlen,
                library_sourced_path_external,
                library_sourced_path_external_len,
                &_vfs_exists, 0
                )) {
            free(fullpath);
            free(library_sourced_path_external);
            if (outofmemory) *outofmemory = 1;
            return NULL;
        }
        if (!_vfs_exists) {
            if (print_debug_info)
                h64printf(
                    "horsec: debug: import: module not found in "
                    "library: %s\n",
                    library_source
                );
            free(fullpath);
            free(library_sourced_path_external);
            if (outofmemory) *outofmemory = 0;
            return NULL;
        }
        int _vfs_exists_nodisk = 0;
        if (!vfs_ExistsU32(
                library_sourced_path_external,
                library_sourced_path_external_len,
                &_vfs_exists_nodisk,
                VFSFLAG_NO_REALDISK_ACCESS)) {
            free(fullpath);
            free(library_sourced_path_external);
            if (outofmemory) *outofmemory = 1;
            return NULL;
        }
        if (_vfs_exists_nodisk) {
            // Return relative VFS-style path (to reduce likeliness
            // of cwd change race conditions):
            if (print_debug_info)
                h64printf(
                    "horsec: debug: import: success, found at %s "
                    "(VFS)\n",
                    library_sourced_path_external
                );
            free(fullpath);
            if (outofmemory) *outofmemory = 0;
            h64wchar *resulturi = NULL;
            int64_t resulturilen = 0;
            {
                uri32info *result = uri32_ParseExU8Protocol(
                    library_sourced_path, library_sourced_path_len,
                    "vfs", URI32_PARSEEX_FLAG_GUESSPORT
                );
                if (result && result->protocol &&
                        h64casecmp_u32u8(result->protocol,
                            result->protocollen, "file") == 0) {
                    free(result->protocol);
                    result->protocol = AS_U32("vfs", &result->protocollen);
                    if (!result->protocol) {
                        uri32_Free(result);
                        result = NULL;
                    }
                }
                free(library_sourced_path_external);
                if (result) {
                    resulturi = uri32_Dump(
                        result, &resulturilen
                    );
                    uri32_Free(result);
                }
            }
            if (!resulturi)
                if (outofmemory) *outofmemory = 1;
            *out_len = resulturilen;
            return resulturi;
        } else {
            // Return full disk path:
            if (print_debug_info)
                h64printf(
                    "horsec: debug: import: success, found at "
                    "%s (disk path)\n",
                    fullpath
                );
            free(library_sourced_path_external);
            if (outofmemory) *outofmemory = 0;
            h64wchar *resulturi = NULL;
            int64_t resulturilen = 0;
            {
                uri32info *result = uri32_ParseExU8Protocol(
                    fullpath, fullpathlen, "file",
                    URI32_PARSEEX_FLAG_GUESSPORT
                );
                free(fullpath);
                if (result) {
                    resulturi = uri32_Dump(result, &resulturilen);
                    uri32_Free(result);
                }
            }
            if (!resulturi)
                if (outofmemory) *outofmemory = 1;
            *out_len = resulturilen;
            return resulturi;
        }
    }

    // Not a library, do local project folder search:
    int projectpathoom = 0;
    uri32info *projecturi = compileproject_GetFileSubProjectURI(
        pr, sourcefileuri, sourcefileurilen, NULL, &projectpathoom
    );
    if (!projecturi) {
        free(import_relpath);
        if (outofmemory) *outofmemory = projectpathoom;
        return NULL;
    }
    int relfilepathoom = 0;
    uri32info *relfileuri = compileproject_ToProjectRelPathURI(
        pr, sourcefileuri, sourcefileurilen, &relfilepathoom
    );
    if (!relfileuri) {
        uri32_Free(projecturi);
        free(import_relpath);
        if (relfilepathoom && outofmemory) *outofmemory = 1;
        return NULL;
    }
    int64_t relfolderpathlen = 0;
    h64wchar *relfolderpath = filesys32_Dirname(
        relfileuri->path, relfileuri->pathlen, &relfolderpathlen
    );
    uri32_Free(relfileuri);
    relfileuri = NULL;
    if (!relfolderpath) {
        uri32_Free(projecturi);
        free(import_relpath);
        if (outofmemory) *outofmemory = 1;
        return NULL;
    }
    while (relfolderpathlen > 0 && (
            relfolderpath[relfolderpathlen - 1] == '/'
            #if defined(_WIN32) || defined(_WIN64)
            || relfolderpath[relfolderpathlen - 1] == '\\'
            #endif
            )) {
        relfolderpathlen--;
    }

    // Split up the relative path in our project into components:
    h64wchar **subdir_components = NULL;
    int64_t *subdir_components_len = 0;
    int subdir_components_count = 0;
    h64wchar currentcomponent[512] = {0};
    int currentcomponentlen = 0;
    i = 0;
    while (1) {
        if (i >= relfolderpathlen || (
                relfolderpath[i] == '/'
                #if defined(_WIN32) || defined(_WIN64)
                || relfolderpath[i] == '\\'
                #endif
                )) {  // component separator in path
            if (currentcomponentlen > 0) {
                // Extract component:
                currentcomponent[currentcomponentlen] = '\0';
                int64_t slen = 0;
                h64wchar *s = strdupu32(
                    currentcomponent, currentcomponentlen,
                    &slen
                );
                if (!s) {
                    subdircheckoom: ;
                    int k = 0;
                    while (k < subdir_components_count) {
                        free(subdir_components[k]);
                        k++;
                    }
                    free(subdir_components);
                    free(subdir_components_len);
                    uri32_Free(projecturi);
                    free(relfolderpath);
                    free(import_relpath);
                    if (outofmemory) *outofmemory = 1;
                    return NULL;
                }
                h64wchar **new_components = realloc(
                    subdir_components,
                    sizeof(*new_components) * (
                    subdir_components_count + 1
                    ));
                if (!new_components) {
                    free(s);
                    goto subdircheckoom;
                }
                subdir_components = new_components;
                int64_t *new_components_len = realloc(
                    subdir_components_len,
                    sizeof(*new_components_len) * (
                    subdir_components_count + 1
                    ));
                if (!new_components_len) {
                    free(s);
                    goto subdircheckoom;
                }
                subdir_components_len = new_components_len;
                subdir_components[subdir_components_count] = s;
                subdir_components_len[subdir_components_count] = (
                    slen
                );
                subdir_components_count++;
            }
            if (i >= relfolderpathlen)
                break;
            currentcomponentlen = 0;
            i++;
            while (i < relfolderpathlen && (
                    relfolderpath[i] == '/'
                    #if defined(_WIN32) || defined(_WIN64)
                    || relfolderpath[i] == '\\'
                    #endif
                    ))
                i++;
            continue;
        }
        // Put together component path:
        if (currentcomponentlen + 1 < (int)(
                sizeof(currentcomponent) / sizeof(currentcomponent[0])
                )) {
            currentcomponent[currentcomponentlen] = (
                relfolderpath[i]
            );
            currentcomponentlen++;
        }
        i++;
    }
    free(relfolderpath); relfolderpath = NULL;

    // Gradually go up the path folder by folder to project root, and
    // see if we can import at each level (with deeper level preferred):
    int64_t resultlen = 0;
    h64wchar *result = NULL;
    int k = subdir_components_count;
    while (k >= 0) {
        // Compute how long the file path will be at this level:
        int subdirspath_len = 0;
        i = 0;
        while (i < k) {
            if (i + 1 < k)
                subdirspath_len++;  // dir sep
            subdirspath_len += subdir_components_len[i];
            i++;
        }
        int64_t checkpath_rel_len = (
            subdirspath_len + 1 + import_relpath_len
        );
        h64wchar *checkpath_rel = malloc(
            sizeof(*checkpath_rel) * (subdirspath_len + 1 +
                import_relpath_len)
        );
        if (!checkpath_rel)
            goto subdircheckoom;

        // Assemble actual file path:
        h64wchar *p = checkpath_rel;
        i = 0;
        while (i < k) {
            memcpy(p, subdir_components[i],
                   sizeof(subdir_components[i]) *
                   subdir_components_len[i]);
            p += subdir_components_len[i];
            if (i + 1 < k) {
                #if defined(_WIN32) || defined(_WIN64)
                *p = '\\';
                #else
                *p = '/';
                #endif
                p++;
            }
            i++;
        }
        #if defined(_WIN32) || defined(_WIN64)
        *p = '\\';
        #else
        *p = '/';
        #endif
        p++;
        memcpy(p, import_relpath,
               sizeof(*p) * import_relpath_len);

        // Get absolute path, and check if we can actually import this path:
        int64_t checkpath_abs_len = 0;
        h64wchar *checkpath_abs = filesys32_Join(
            projecturi->path, projecturi->pathlen,
            checkpath_rel, checkpath_rel_len,
            &checkpath_abs_len
        );
        if (!checkpath_abs) {
            free(checkpath_rel);
            goto subdircheckoom;
        }
        int _exists_result = 0;
        if (!vfs_ExistsExU32(
                checkpath_abs, checkpath_abs_len,
                checkpath_rel, checkpath_rel_len,
                &_exists_result, 0)) {
            free(checkpath_abs);
            free(checkpath_rel);
            goto subdircheckoom;
        }
        if (_exists_result) {
            int _directory_result = 0;
            if (!vfs_IsDirectoryExU32(
                    checkpath_abs, checkpath_abs_len,
                    checkpath_rel, checkpath_rel_len,
                    &_directory_result, 0)) {
                free(checkpath_abs);
                free(checkpath_rel);
                goto subdircheckoom;
            }
            if (!_directory_result) {
                // Match! Import found something, return this.
                int _exists_result_nodisk = 0;
                if (!vfs_ExistsU32(
                        checkpath_rel, checkpath_rel_len,
                        &_exists_result_nodisk,
                        VFSFLAG_NO_REALDISK_ACCESS)) {
                    free(checkpath_abs);
                    free(checkpath_rel);
                    goto subdircheckoom;
                }
                if (_exists_result_nodisk) {
                    // Return relative VFS-style path (to reduce likeliness
                    // of cwd change race conditions):
                    if (print_debug_info)
                        h64printf(
                            "horsec: debug: import: success, found "
                            "at %s (VFS)\n",
                            checkpath_rel
                        );
                    free(checkpath_abs);
                    result = checkpath_rel;
                    resultlen = checkpath_rel_len;
                } else {
                    // Return actual full disk path:
                    if (print_debug_info)
                        h64printf(
                            "horsec: debug: import: success, found "
                            "at %s (disk path)\n",
                            checkpath_abs
                        );
                    free(checkpath_rel);
                    result = checkpath_abs;
                    resultlen = checkpath_abs_len;
                }
                break;
            }
        }
        free(checkpath_abs);
        free(checkpath_rel);

        // No match, go up one folder:
        k--;
    }
    k = 0;
    while (k < subdir_components_count) {
        free(subdir_components[k]);
        k++;
    }
    free(subdir_components);
    uri32_Free(projecturi);
    free(relfolderpath);
    free(import_relpath);
    if (outofmemory) *outofmemory = 0;
    return result;
}

h64wchar *compileproject_FolderGuess(
        const h64wchar *fileuri, int64_t fileurilen,
        int cwd_fallback_if_appropriate,
        int64_t *out_len, char **error
        ) {
    assert(fileuri != NULL);
    uri32info *uinfo = uri32_ParseExU8Protocol(
        fileuri, fileurilen, "file",
        URI32_PARSEEX_FLAG_GUESSPORT
    );
    if (!uinfo || !uinfo->path || !uinfo->protocol ||
            h64casecmp_u32u8(uinfo->protocol,
                uinfo->protocollen, "file") != 0) {
        uri32_Free(uinfo);
        *error = strdup("failed to parse URI, invalid syntax "
            "or not file protocol");
        return NULL;
    }
    int64_t full_path_len = 0;
    h64wchar *full_path = filesys32_ToAbsolutePath(
        uinfo->path, uinfo->pathlen, &full_path_len
    );
    uri32_Free(uinfo); uinfo = NULL;
    int64_t slen = 0;
    h64wchar *s = NULL;
    if (full_path)
        s = strdupu32(full_path, full_path_len, &slen);
    if (!full_path || !s) {
        *error = strdup("allocation failure, out of memory?");
        return NULL;
    }

    {
        int _exists = 0;
        int _isdir = 0;
        if (!filesys32_TargetExists(s, slen, &_exists) ||
                (_exists && !filesys32_IsDirectory(s, slen, &_isdir))) {
            free(s);
            free(full_path);
            *error = strdup("unknown I/O or allocation failure");
            return NULL;
        }
        if (!_exists || _isdir) {
            free(s);
            free(full_path);
            *error = strdup("path not referring to an existing file, "
                "or lacking permission to access");
            return NULL;
        }
    }

    while (1) {
        // Go up one folder:
        {
            int64_t snewlen = 0;
            h64wchar *snew = filesys32_ParentdirOfItem(
                s, slen, &snewlen
            );
            if (!snew) {
                free(s);
                free(full_path);
                *error = strdup("alloc failure");
                return NULL;
            }
            while (snewlen >= 1 && (snew[snewlen - 1] == '/'
                    #if defined(_WIN32) || defined(_WIN64)
                    || snew[snewlen - 1] == '\\'
                    #endif
                    ))
                snewlen--;
            if (slen == snewlen &&
                    memcmp(s, snew, sizeof(*s) * snewlen) == 0) {
                free(s); free(snew);
                s = NULL;
                break;
            }
            free(s);
            s = snew;
            slen = snewlen;
        }

        // Check for .git:
        {
            h64wchar _test_append[] = {
                '.', 'g', 'i', 't'
            };
            int64_t _test_append_len = strlen(".git");
            int64_t check_path_len = 0;
            h64wchar *check_path = filesys32_Join(
                s, slen, _test_append, _test_append_len,
                &check_path_len
            );
            if (!check_path) {
                free(s);
                free(full_path);
                *error = strdup("alloc failure");
                return NULL;
            }
            int _exists = 0;
            if (!filesys32_TargetExists(check_path, check_path_len,
                    &_exists)) {
                free(check_path);
                free(s);
                free(full_path);
                *error = strdup("unexpected I/O failure");
                return NULL;
            }
            if (_exists) {
                free(check_path);
                free(full_path);
                *out_len = slen;
                return s;
            }
            free(check_path);
        }

        // Check for horse_modules:
        {
            h64wchar _test_append[] = {
                'h', 'o', 'r', 's', 'e', '_',
                'm', 'o', 'd', 'u', 'l', 'e', 's'
            };
            int64_t _test_append_len = strlen("horse_modules");
            int64_t check_path_len = 0;
            h64wchar *check_path = filesys32_Join(
                s, slen, _test_append, _test_append_len,
                &check_path_len
            );
            if (!check_path) {
                free(s);
                free(full_path);
                *error = strdup("alloc failure");
                return NULL;
            }
            int _exists = 0;
            if (!filesys32_TargetExists(check_path, check_path_len,
                    &_exists)) {
                free(check_path);
                free(s);
                free(full_path);
                *error = strdup("unexpected I/O failure");
                return NULL;
            }
            if (_exists) {
                free(check_path);
                free(full_path);
                return s;
            }
            free(check_path);
        }
    }

    // Check if we can fall back to the current directory:
    if (cwd_fallback_if_appropriate) {
        int64_t cwd_rel_len = 0;
        h64wchar *cwd_rel = filesys32_GetCurrentDirectory(&cwd_rel_len);
        if (!cwd_rel) {
            free(full_path);
            *error = strdup("alloc failure");
            return NULL;
        }
        int64_t cwdlen = 0;
        h64wchar *cwd = filesys32_ToAbsolutePath(
            cwd_rel, cwd_rel_len, &cwdlen
        );
        free(cwd_rel);
        if (!cwd) {
            free(full_path);
            *error = strdup("alloc failure");
            return NULL;
        }
        int _contained = 0;
        if (!filesys32_FolderContainsPath(
                cwd, cwdlen, full_path, full_path_len,
                &_contained)) {
            free(cwd);
            free(full_path);
            *error = strdup("alloc failure");
            return NULL;
        }
        if (_contained) {
            int64_t resultlen = 0;
            h64wchar *result = filesys32_Normalize(
                cwd, cwdlen, &resultlen
            );
            free(full_path);
            free(cwd);
            if (!result)
                *error = strdup("alloc failure");
            *out_len = resultlen;
            return result;
        }
        free(cwd);
    }

    free(full_path);
    *error = strdup("failed to find project folder");
    return NULL;
}

typedef struct compileallinfo {
    const h64wchar *mainfileuri;
    int64_t mainfileurilen;
    int mainfileseen;
    h64compileproject *pr;
    h64misccompileroptions *miscoptions;
} compileallinfo;

int _resolveallcb(
        ATTR_UNUSED hashmap *map, const char *key,
        uint64_t number, void *userdata
        ) {
    if (!key || number == 0)
        return 0;
    h64ast *ast = (h64ast *)(uintptr_t)number;
    compileallinfo *cinfo = (compileallinfo *)userdata;
    h64compileproject *pr = cinfo->pr;

    // Determine if this is the file that should have "main":
    int fileismain = 0;
    assert(cinfo->mainfileuri != NULL && ast->fileuri != NULL);
    int relfileoom = 0;
    uri32info *relfileuri_main = compileproject_ToProjectRelPathURI(
        pr, cinfo->mainfileuri, cinfo->mainfileurilen, &relfileoom
    );
    if (!relfileuri_main)
        return 0;
    relfileoom = 0;
    uri32info *relfileuri_ast = compileproject_ToProjectRelPathURI(
        pr, ast->fileuri, ast->fileurilen, &relfileoom
    );
    if (!relfileuri_ast) {
        uri32_Free(relfileuri_main);
        return 0;
    }
    if (!uri32_CompareEx(
            relfileuri_main, relfileuri_ast,
            0, -1, &fileismain
            )) {
        // Oom.
        uri32_Free(relfileuri_main);
        uri32_Free(relfileuri_ast);
        return 0;
    }
    uri32_Free(relfileuri_main);
    uri32_Free(relfileuri_ast);
    assert(!fileismain || !cinfo->mainfileseen);
    if (fileismain)
        cinfo->mainfileseen = 1;

    if (!scoperesolver_ResolveAST(pr, cinfo->miscoptions, ast, fileismain))
        return 0;
    return 1;
}

int _codegenallcb(
        ATTR_UNUSED hashmap *map, const char *key,
        uint64_t number, void *userdata
        ) {
    if (!key || number == 0)
        return 0;
    h64ast *ast = (h64ast *)(uintptr_t)number;
    compileallinfo *cinfo = (compileallinfo *)userdata;
    h64compileproject *pr = cinfo->pr;
    if (!codegen_GenerateBytecodeForFile(pr, cinfo->miscoptions, ast))
        return 0;
    return 1;
}

int compileproject_CompileAllToBytecode(
        h64compileproject *project,
        h64misccompileroptions *moptions,
        const h64wchar *mainfileuri, int64_t mainfileurilen,
        char **error
        ) {
    assert(mainfileuri != NULL);
    if (!project) {
        if (error)
            *error = strdup("project pointer is NULL");
        return 0;
    }
    // First, make sure all files are scope resolved:
    compileallinfo cinfo;
    memset(&cinfo, 0, sizeof(cinfo));
    cinfo.pr = project;
    cinfo.miscoptions = moptions;
    cinfo.mainfileuri = mainfileuri;
    cinfo.mainfileurilen = mainfileurilen;
    while (1) {
        int oldcount = project->astfilemap_count;
        if (!hash_StringMapIterate(
                project->astfilemap, &_resolveallcb,
                &cinfo)) {
            if (error)
                *error = strdup(
                    "unexpected resolve callback failure, "
                    "out of memory?"
                );
            return 0;
        }
        if (oldcount == project->astfilemap_count)
            break;
        // We discovered more files during scope resolution. Do it again:
        cinfo.mainfileseen = 0;
        continue;
    }
    if (!cinfo.mainfileseen) {
        if (error) {
            char buf[512];
            snprintf(buf, sizeof(buf) - 1,
                "internal error, somehow failed to visit main file "
                "during pre-codegen resolution with main file uri: %s",
                AS_U8_TMP(mainfileuri, mainfileurilen)
            );
            *error = strdup(buf);
        }
        return 0;
    }
    int hadprojecterror = 0;
    if (cinfo.pr->resultmsg->message_count > 0) {
        int i = 0;
        while (i < cinfo.pr->resultmsg->message_count) {
            if (cinfo.pr->resultmsg->message[i].type == H64MSG_ERROR)
                hadprojecterror = 1;
            i++;
        }
    }
    if (hadprojecterror) {
        // Stop here, we can't safely codegen if there was an error.
        return 1;
    }
    if (!cinfo.pr->resultmsg->success) {
        // Probably out of memory
        if (error)
            *error = strdup("unexpectedly failed to get error message, "
                            "out of memory?");
        return 0;
    }

    // Do the final checks with more complicated graph analyses:
    if (!threadablechecker_IterateFinalGraph(cinfo.pr)) {
        assert(!cinfo.pr->resultmsg->success);
        return 1;
    }
    assert(cinfo.pr->resultmsg->success);

    // Now, do actual codegen:
    if (!hash_StringMapIterate(
            project->astfilemap, &_codegenallcb,
            &cinfo)) {
        project->resultmsg->success = 0;
        if (error)
            *error = strdup(
                "unexpected codegen callback failure, "
                "out of memory?"
            );
        return 0;
    }
    // Transform jump instructions to final offsets:
    if (!codegen_FinalBytecodeTransform(
            project
            )) {
        project->resultmsg->success = 0;
        char buf[256];
        snprintf(buf, sizeof(buf) - 1,
            "internal error: jump offset calculation "
            "failed, out of memory or codegen bug?"
        );
        if (error)
            *error = strdup(buf);
        return 0;
    }
    return 1;
}
