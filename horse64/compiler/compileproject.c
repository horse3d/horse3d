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
#include "hash.h"
#include "nonlocale.h"
#include "secrandom.h"
#include "threadablechecker.h"
#include "uri.h"
#include "vfs.h"

// #define DEBUG_COMPILEPROJECT

h64compileproject *compileproject_New(
        const char *basefolderuri
        ) {
    if (!basefolderuri)
        return NULL;

    h64compileproject *pr = malloc(sizeof(*pr));
    if (!pr)
        return NULL;
    memset(pr, 0, sizeof(*pr));
    warningconfig_Init(&pr->warnconfig);

    uriinfo *uinfo = uri_ParseEx(basefolderuri, "file");
    if (!uinfo || !uinfo->path || !uinfo->protocol ||
            h64casecmp(uinfo->protocol, "file") != 0) {
        uri_Free(uinfo);
        free(pr);
        return NULL;
    }

    char *s = filesys_ToAbsolutePath(uinfo->path);
    uri_Free(uinfo); uinfo = NULL;
    if (!s) {
        compileproject_Free(pr);
        return NULL;
    }
    pr->basefolder = filesys_Normalize(s);
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

uriinfo *compileproject_FilePathToURI(
        const char *fileuri, int makeabs
        ) {
    int _couldbevfs = 0;
    if (!strstr(fileuri, "://"))
        _couldbevfs = 1;
    uriinfo *uinfo = uri_ParseEx(fileuri, "file");
    if (!uinfo || !uinfo->path || !uinfo->protocol ||
            (h64casecmp(uinfo->protocol, "file") != 0 &&
             h64casecmp(uinfo->protocol, "vfs") != 0)) {
        uri_Free(uinfo);
        return NULL;
    }
    if (_couldbevfs && h64casecmp(uinfo->protocol, "file") == 0 &&
            !filesys_IsAbsolutePath(uinfo->path)) {
        int exists = 0;
        if (!vfs_Exists(uinfo->path, &exists,
                VFSFLAG_NO_REALDISK_ACCESS)) {
            uri_Free(uinfo);
            return NULL;
        }
        if (exists) {
            free(uinfo->protocol);
            uinfo->protocol = strdup("vfs");
            if (!uinfo->protocol) {
                uri_Free(uinfo);
                return NULL;
            }
            char *npath = filesys_Normalize(uinfo->path);
            if (!npath) {
                uri_Free(uinfo);
                return NULL;
            }
            free(uinfo->path);
            uinfo->path = npath;
        }
    }
    if (h64casecmp(uinfo->protocol, "file") == 0 &&
            !filesys_IsAbsolutePath(uinfo->path) && makeabs) {
        char *npath = filesys_ToAbsolutePath(uinfo->path);
        if (npath) {
            char *npath2 = filesys_Normalize(npath);
            free(npath);
            npath = npath2;
        }
        if (!npath) {
            uri_Free(uinfo);
            return NULL;
        }
        free(uinfo->path);
        uinfo->path = npath;
    }
    return uinfo;
}

uriinfo *compileproject_URIRelPathToBase(
        const char *basepath, const char *fileuri,
        int *outofmemory
        ) {
    if (!fileuri || !basepath) {
        if (outofmemory) *outofmemory = 1;
        return NULL;
    }

    uriinfo *uinfo = compileproject_FilePathToURI(fileuri, 1);
    if (!uinfo) {
        if (outofmemory) *outofmemory = 1;
        return NULL;
    }

    if (h64casecmp(uinfo->protocol, "file") == 0 ||
            h64casecmp(uinfo->protocol, "vfs") == 0) {
        // Convert URIs to be relative to given base.
        #ifndef NDEBUG
        if (h64casecmp(uinfo->protocol, "vfs") == 0)
            assert(!filesys_IsAbsolutePath(basepath));
        else if (h64casecmp(uinfo->protocol, "file") == 0)
            assert(filesys_IsAbsolutePath(basepath));
        #endif
        char *newpath = filesys_TurnIntoPathRelativeTo(
            uinfo->path, basepath
        );
        if (newpath) {
            char *newpath2 = filesys_Normalize(newpath);
            free(newpath);
            newpath = newpath2;
        }
        if (!newpath) {
            uri_Free(uinfo);
            uinfo = NULL;
            if (outofmemory) *outofmemory = 1;
            return NULL;
        }
        if (strlen(newpath) > strlen("../") &&
                (memcmp(newpath, "../", 3) == 0
                #if defined(_WIN32) || defined(_WIN64)
                || memcmp(newpath, "..\\", 3) == 0
                #endif
                )) {
            // A path escaping out of base is not allowed.
            uri_Free(uinfo);
            uinfo = NULL;
            if (outofmemory) *outofmemory = 0;
            return NULL;
        }
        free(uinfo->path);
        uinfo->path = newpath;
    }
    if (!uinfo) {
        if (outofmemory) *outofmemory = 1;
        return NULL;
    }
    return uinfo;
}

uriinfo *compileproject_ToProjectRelPathURI(
        h64compileproject *pr, const char *fileuri,
        int *outofmemory
        ) {
    if (!fileuri || !pr || !pr->basefolder) {
        if (outofmemory) *outofmemory = 0;
        return NULL;
    }
    int isvfs = 0;
    {
        uriinfo *fileuri_info = uri_ParseEx(fileuri, "file");
        if (!fileuri_info) {
            if (outofmemory) *outofmemory = 1;
            return NULL;
        }
        isvfs = (
            fileuri_info->protocol != NULL &&
            h64casecmp(fileuri_info->protocol, "vfs") == 0
        );
        uri_Free(fileuri_info);
    }
    return compileproject_URIRelPathToBase(
        (isvfs ? "" : pr->basefolder), fileuri, outofmemory
    );
}

int compileproject_GetAST(
        h64compileproject *pr, const char *fileuri,
        h64ast **out_ast, char **error
        ) {
    int relfileoom = 0;
    uriinfo *relfileuri = compileproject_ToProjectRelPathURI(
        pr, fileuri, &relfileoom
    );
    if (!relfileuri) {
        if (relfileoom) {
            *error = strdup("out of memory");
            *out_ast = NULL;
            return 0;
        }
        char msg[] = "cannot get AST of file outside of project root: ";
        *error = malloc(
            strlen(msg) + strlen(fileuri) + 1
        );
        if (*error) {
            memcpy(
                *error, msg, strlen(msg)
            );
            memcpy(
                (*error) + strlen(msg), fileuri, strlen(fileuri) + 1
            );
        }
        *out_ast = NULL;
        return 0;
    }
 
    char *hashmap_key = uri_Dump(relfileuri);
    if (!hashmap_key) {
        uri_Free(relfileuri);
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
        uri_Free(relfileuri);
        *out_ast = resultptr;
        *error = NULL;
        return 1;
    }

    int isvfs = (h64casecmp(relfileuri->protocol, "vfs") == 0);
    if (!isvfs) {
        char *trueabspath = filesys_Join(
            pr->basefolder, relfileuri->path
        );
        if (!trueabspath) {
            free(hashmap_key);
            uri_Free(relfileuri);
            *error = strdup("alloc fail (abs file path)");
            *out_ast = NULL;
            return 0;
        }
        free(relfileuri->path);
        relfileuri->path = trueabspath;
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
        uri_Free(relfileuri);
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
        uri_Free(relfileuri);
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
        uri_Free(relfileuri);
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
    uri_Free(relfileuri);
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

uriinfo *compileproject_GetFileSubProjectURI(
        h64compileproject *pr, const char *sourcefileuri,
        char **subproject_name, int *outofmemory
        ) {
    // Parse sourcefileuri given to us:
    uriinfo *uinfo = uri_ParseEx(sourcefileuri, "file");
    if (!uinfo || !uinfo->path || !uinfo->protocol ||
            (h64casecmp(uinfo->protocol, "file") != 0 &&
             h64casecmp(uinfo->protocol, "vfs") != 0)) {
        if (outofmemory && !uinfo) *outofmemory = 1;
        if (outofmemory && uinfo) *outofmemory = 0;
        uri_Free(uinfo);
        return NULL;
    }
    int isvfs = (h64casecmp(uinfo->protocol, "vfs") == 0);

    // Turn it into a relative URI, which is relative to our main project:
    int relfilepathoom = 0;
    if (!isvfs) {
        uriinfo *relfileuri = compileproject_ToProjectRelPathURI(
            pr, uinfo->path, &relfilepathoom
        );
        if (!relfileuri) {
            uri_Free(uinfo);
            if (outofmemory && relfilepathoom) *outofmemory = 1;
            if (outofmemory && !relfilepathoom) *outofmemory = 0;
            return NULL;
        }
        uri_Free(uinfo);
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
        if (strlen(uinfo->path) <= strlen(hmodules_path)) {
            // Too short to have horse_modules + sub_folder in path.
            i++;
            continue;
        }
        // Verify it starts with correct path segment:
        char buf[64];
        memcpy(buf, uinfo->path, strlen(hmodules_path));
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
            while (uinfo->path[k] != '/' && uinfo->path[k] != '\0'
                    #if defined(_WIN32) || defined(_WIN64)
                    && uinfo->path[k] != '\\'
                    #endif
                    )
                k++;
            if (uinfo->path[k] != '\0') {
                k++;  // go past dir separator
                // Extract actual horse_modules/<name>/(REST CUT OFF) path:
                char *relfilepath_shortened = strdup(uinfo->path);
                if (!relfilepath_shortened) {
                    uri_Free(uinfo);
                    if (outofmemory) *outofmemory = 0;
                    return NULL;
                }
                int firstslashindex = -1;
                int secondslashindex = -1;
                int slashcount = 0;
                int j = 0;
                while (j < (int)strlen(relfilepath_shortened)) {
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
                            relfilepath_shortened[j] = '\0';
                            break;
                        }
                    }
                    j++;
                }
                if (slashcount != 2 ||
                        secondslashindex <= firstslashindex + 1) {
                    free(relfilepath_shortened);
                    uri_Free(uinfo);
                    if (outofmemory) *outofmemory = 0;
                    return NULL;
                }

                // Extract project name from our cut off result path:
                char *project_name = malloc(
                    secondslashindex - firstslashindex
                );
                if (!project_name) {
                    free(relfilepath_shortened);
                    uri_Free(uinfo);
                    if (outofmemory) *outofmemory = 0;
                    return NULL;
                }
                memcpy(
                    project_name, uinfo->path + firstslashindex + 1,
                    secondslashindex - (firstslashindex + 1)
                );
                project_name[
                    secondslashindex - (firstslashindex + 1)
                ] = '\0';

                // Turn relative project path into absolute one:
                char *parent_abs = filesys_ToAbsolutePath(
                    pr->basefolder
                );  // needs to be relative to main project path
                if (!parent_abs) {
                    uri_Free(uinfo);
                    free(relfilepath_shortened);
                    if (outofmemory) *outofmemory = 0;
                    return NULL;
                }
                char *resultpath = NULL;
                if (!isvfs) {
                    resultpath = filesys_Join(
                        parent_abs, relfilepath_shortened
                    );
                } else {
                    resultpath = strdup(relfilepath_shortened);
                }
                free(parent_abs);
                parent_abs = NULL;
                free(relfilepath_shortened);
                relfilepath_shortened = NULL;
                if (resultpath) {
                    char *resultold = resultpath;
                    resultpath = filesys_Normalize(resultold);
                    free(resultold);
                }
                uri_Free(uinfo);
                if (resultpath) {
                    if (subproject_name) *subproject_name = project_name;
                    if (outofmemory) *outofmemory = 0;
                } else {
                    if (outofmemory) *outofmemory = 1;
                }
                // Ok, return as URI with protocol header stuff:
                uriinfo *resulturi = uri_ParseEx(
                    resultpath, (isvfs ? "vfs" : "file")
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
    uri_Free(uinfo);
    if (subproject_name) {
        *subproject_name = strdup("");
        if (!*subproject_name) {
            if (outofmemory) *outofmemory = 1;
            return NULL;
        }
    }
    char *resultpath = NULL;
    if (isvfs) {
        resultpath = strdup("");  // (cwd)
    } else {
        resultpath = filesys_ToAbsolutePath(pr->basefolder);
        if (resultpath) {
            char *result2 = uri_Normalize(
                resultpath, 1
            );
            free(resultpath);
            resultpath = result2;
        }
    }
    if (!resultpath) {
        if (outofmemory) *outofmemory = 1;
        return NULL;
    }
    // Ok, return as URI with protocol header stuff:
    uriinfo *resulturi = uri_ParseEx(
        resultpath, (isvfs ? "vfs" : "file")
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

char *compileproject_ResolveImportToFile(
        h64compileproject *pr,
        const char *sourcefileuri,
        const char **import_elements, int import_elements_count,
        const char *library_source,
        int print_debug_info,
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
        int library_sourced_path_len = (
            strlen("horse_modules_builtin") + 1 +
            strlen(library_source) + 1 +
            import_relpath_len + 1
        );
        char *library_sourced_path = malloc(
            library_sourced_path_len
        );
        if (!library_sourced_path) {
            free(import_relpath);
            if (outofmemory) *outofmemory = 1;
            return NULL;
        }

        // Copy in actual path contents:
        memcpy(library_sourced_path, "horse_modules_builtin",
               strlen("horse_modules_builtin"));
        #if defined(_WIN32) || defined(_WIN64)
        library_sourced_path[strlen("horse_modules_builtin")] = '\\';
        #else
        library_sourced_path[strlen("horse_modules_builtin")] = '/';
        #endif
        memcpy(library_sourced_path + strlen("horse_modules_builtin/"),
               library_source, strlen(library_source));
        #if defined(_WIN32) || defined(_WIN64)
        library_sourced_path[strlen("horse_modules_builtin") + 1 +
            strlen(library_source)] = '\\';
        #else
        library_sourced_path[strlen("horse_modules_builtin") + 1 +
            strlen(library_source)] = '/';
        #endif
        memcpy(library_sourced_path + strlen("horse_modules_builtin/") +
               strlen(library_source) + 1,
               import_relpath, import_relpath_len + 1);
        free(import_relpath); import_relpath = NULL;

        // Assemble horse_modules-VFS and absolute disk file path:
        char *library_sourced_path_external = strdup(library_sourced_path);
        if (!library_sourced_path_external) {
            free(library_sourced_path);
            if (outofmemory) *outofmemory = 1;
            return NULL;
        }
        memmove(
            library_sourced_path_external + strlen("horse_modules"),
            library_sourced_path_external + strlen("horse_modules_builtin"),
            strlen(library_sourced_path_external) + 1 -
            strlen("horse_modules_builtin")
        );
        char *fullpath = filesys_Join(
            pr->basefolder, library_sourced_path_external
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
            if (!vfs_Exists(
                    library_sourced_path, &_vfs_exists_internal,
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
                char *resulturi = NULL;
                {
                    uriinfo *result = uri_ParseEx(
                        library_sourced_path, "vfs"
                    );
                    if (result && result->protocol &&
                            strcmp(result->protocol, "file") == 0) {
                        free(result->protocol);
                        result->protocol = strdup("vfs");
                        if (!result->protocol) {
                            uri_Free(result);
                            result = NULL;
                        }
                    }
                    free(library_sourced_path);
                    if (result) {
                        resulturi = uri_Dump(result);
                        uri_Free(result);
                    }
                }
                if (!resulturi)
                    if (outofmemory) *outofmemory = 1;
                return resulturi;
            }
            free(library_sourced_path);
            library_sourced_path = NULL;
        }

        // Check if it exists in "horse_modules" and return result:
        int _vfs_exists = 0;
        if (!vfs_ExistsEx(
                fullpath, library_sourced_path_external,
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
        if (!vfs_Exists(
                library_sourced_path_external, &_vfs_exists_nodisk,
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
            char *resulturi = NULL;
            {
                uriinfo *result = uri_ParseEx(
                    library_sourced_path, "vfs"
                );
                if (result && result->protocol &&
                        strcmp(result->protocol, "file") == 0) {
                    free(result->protocol);
                    result->protocol = strdup("vfs");
                    if (!result->protocol) {
                        uri_Free(result);
                        result = NULL;
                    }
                }
                free(library_sourced_path_external);
                if (result) {
                    resulturi = uri_Dump(result);
                    uri_Free(result);
                }
            }
            if (!resulturi)
                if (outofmemory) *outofmemory = 1;
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
            char *resulturi = NULL;
            {
                uriinfo *result = uri_ParseEx(
                    fullpath, "file"
                );
                free(fullpath);
                if (result) {
                    resulturi = uri_Dump(result);
                    uri_Free(result);
                }
            }
            if (!resulturi)
                if (outofmemory) *outofmemory = 1;
            return resulturi;
        }
    }

    // Not a library, do local project folder search:
    int projectpathoom = 0;
    uriinfo *projecturi = compileproject_GetFileSubProjectURI(
        pr, sourcefileuri, NULL, &projectpathoom
    );
    if (!projecturi) {
        free(import_relpath);
        if (outofmemory) *outofmemory = projectpathoom;
        return NULL;
    }
    int relfilepathoom = 0;
    uriinfo *relfileuri = compileproject_ToProjectRelPathURI(
        pr, sourcefileuri, &relfilepathoom
    );
    if (!relfileuri) {
        uri_Free(projecturi);
        free(import_relpath);
        if (relfilepathoom && outofmemory) *outofmemory = 1;
        return NULL;
    }
    char *relfolderpath = filesys_Dirname(relfileuri->path);
    uri_Free(relfileuri);
    relfileuri = NULL;
    if (!relfolderpath) {
        uri_Free(projecturi);
        free(import_relpath);
        if (outofmemory) *outofmemory = 1;
        return NULL;
    }
    while (strlen(relfolderpath) && (
            relfolderpath[strlen(relfolderpath) - 1] == '/'
            #if defined(_WIN32) || defined(_WIN64)
            || relfolderpath[strlen(relfolderpath) - 1] == '\\'
            #endif
            )) {
        relfolderpath[strlen(relfolderpath) - 1] = '\0';
    }

    // Split up the relative path in our project into components:
    char **subdir_components = NULL;
    int subdir_components_count = 0;
    char currentcomponent[512] = {0};
    int currentcomponentlen = 0;
    i = 0;
    while (1) {
        if (relfolderpath[i] == '\0' || (
                relfolderpath[i] == '/'
                #if defined(_WIN32) || defined(_WIN64)
                || relfolderpath[i] == '\\'
                #endif
                )) {  // component separator in path
            if (currentcomponentlen > 0) {
                // Extract component:
                currentcomponent[currentcomponentlen] = '\0';
                char *s = strdup(currentcomponent);
                if (!s) {
                    subdircheckoom: ;
                    int k = 0;
                    while (k < subdir_components_count) {
                        free(subdir_components[k]);
                        k++;
                    }
                    free(subdir_components);
                    uri_Free(projecturi);
                    free(relfolderpath);
                    free(import_relpath);
                    if (outofmemory) *outofmemory = 1;
                    return NULL;
                }
                char **new_components = realloc(
                    subdir_components,
                    sizeof(*new_components) * (
                    subdir_components_count + 1
                    ));
                if (!new_components) {
                    free(s);
                    goto subdircheckoom;
                }
                subdir_components = new_components;
                subdir_components[subdir_components_count] = s;
                subdir_components_count++;
            }
            if (relfolderpath[i] == '\0')
                break;
            currentcomponentlen = 0;
            i++;
            continue;
        }
        // Put together component path:
        if (currentcomponentlen + 1 < (int)sizeof(currentcomponent)) {
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
    char *result = NULL;
    int k = subdir_components_count;
    while (k >= 0) {
        // Compute how long the file path will be at this level:
        int subdirspath_len = 0;
        i = 0;
        while (i < k) {
            if (i + 1 < k)
                subdirspath_len++;  // dir sep
            subdirspath_len += strlen(subdir_components[i]);
            i++;
        }
        char *checkpath_rel = malloc(
            subdirspath_len + 1 + import_relpath_len + 1
        );
        if (!checkpath_rel)
            goto subdircheckoom;

        // Assemble actual file path:
        char *p = checkpath_rel;
        i = 0;
        while (i < k) {
            memcpy(p, subdir_components[i],
                   strlen(subdir_components[i]));
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
        memcpy(p, import_relpath, import_relpath_len + 1);
        assert((int)(strlen(checkpath_rel) + 1) ==
               subdirspath_len + 1 + import_relpath_len + 1);

        // Get absolute path, and check if we can actually import this path:
        char *checkpath_abs = filesys_Join(
            projecturi->path, checkpath_rel
        );
        if (!checkpath_abs) {
            free(checkpath_rel);
            goto subdircheckoom;
        }
        int _exists_result = 0;
        if (!vfs_ExistsEx(checkpath_abs, checkpath_rel,
                &_exists_result, 0)) {
            free(checkpath_abs);
            free(checkpath_rel);
            goto subdircheckoom;
        }
        if (_exists_result) {
            int _directory_result = 0;
            if (!vfs_IsDirectoryEx(checkpath_abs, checkpath_rel,
                    &_directory_result, 0)) {
                free(checkpath_abs);
                free(checkpath_rel);
                goto subdircheckoom;
            }
            if (!_directory_result) {
                // Match! Import found something, return this.
                int _exists_result_nodisk = 0;
                if (!vfs_Exists(checkpath_rel, &_exists_result_nodisk,
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
    uri_Free(projecturi);
    free(relfolderpath);
    free(import_relpath);
    if (outofmemory) *outofmemory = 0;
    return result;
}

char *compileproject_FolderGuess(
        const char *fileuri, int cwd_fallback_if_appropriate,
        char **error
        ) {
    assert(fileuri != NULL);
    uriinfo *uinfo = uri_ParseEx(fileuri, "file");
    if (!uinfo || !uinfo->path || !uinfo->protocol ||
            h64casecmp(uinfo->protocol, "file") != 0) {
        uri_Free(uinfo);
        *error = strdup("failed to parse URI, invalid syntax "
            "or not file protocol");
        return NULL;
    }
    char *s = filesys_ToAbsolutePath(uinfo->path);
    char *full_path = (s ? strdup(s) : (char*)NULL);
    uri_Free(uinfo); uinfo = NULL;
    if (!s) {
        *error = strdup("allocation failure, out of memory?");
        return NULL;
    }
    if (!filesys_FileExists(s) || filesys_IsDirectory(s)) {
        free(s);
        free(full_path);
        *error = strdup("path not referring to an existing file, "
            "or lacking permission to access");
        return NULL;
    }

    while (1) {
        // Go up one folder:
        {
            char *snew = filesys_ParentdirOfItem(s);
            if (!snew) {
                free(s);
                free(full_path);
                *error = strdup("alloc failure");
                return NULL;
            }
            while (strlen(snew) > 1 && (snew[strlen(snew) - 1] == '/'
                    #if defined(_WIN32) || defined(_WIN64)
                    || snew[strlen(snew) - 1] == '\\'
                    #endif
                    ))
                snew[strlen(snew) - 1] = '\0';
            if (strcmp(s, snew) == 0) {
                free(s); free(snew);
                s = NULL;
                break;
            }
            free(s);
            s = snew;
        }

        // Check for .git:
        {
            char *git_path = filesys_Join(s, ".git");
            if (!git_path) {
                free(s);
                free(full_path);
                *error = strdup("alloc failure");
                return NULL;
            }
            if (filesys_FileExists(git_path)) {
                free(git_path);
                free(full_path);
                return s;
            }
            free(git_path);
        }

        // Check for horse_modules:
        {
            char *mods_path = filesys_Join(s, "horse_modules");
            if (!mods_path) {
                free(s);
                free(full_path);
                *error = strdup("alloc failure");
                return NULL;
            }
            if (filesys_FileExists(mods_path)) {
                free(mods_path);
                free(full_path);
                return s;
            }
            free(mods_path);
        }
    }

    // Check if we can fall back to the current directory:
    if (cwd_fallback_if_appropriate) {
        char *cwd_rel = filesys_GetCurrentDirectory();
        if (!cwd_rel) {
            free(full_path);
            *error = strdup("alloc failure");
            return NULL;
        }
        char *cwd = filesys_ToAbsolutePath(cwd_rel);
        free(cwd_rel);
        if (!cwd) {
            free(full_path);
            *error = strdup("alloc failure");
            return NULL;
        }
        int _contained = 0;
        if (!filesys_FolderContainsPath(cwd, full_path, &_contained)) {
            free(cwd);
            free(full_path);
            *error = strdup("alloc failure");
            return NULL;
        }
        if (_contained) {
            char *result = filesys_Normalize(cwd);
            free(full_path);
            free(cwd);
            if (!result)
                *error = strdup("alloc failure");
            return result;
        }
        free(cwd);
    }

    free(full_path);
    *error = strdup("failed to find project folder");
    return NULL;
}

typedef struct compileallinfo {
    const char *mainfileuri;
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
    uriinfo *relfileuri_main = compileproject_ToProjectRelPathURI(
        pr, cinfo->mainfileuri, &relfileoom
    );
    if (!relfileuri_main)
        return 0;
    relfileoom = 0;
    uriinfo *relfileuri_ast = compileproject_ToProjectRelPathURI(
        pr, ast->fileuri, &relfileoom
    );
    if (!relfileuri_ast) {
        uri_Free(relfileuri_main);
        return 0;
    }
    if (!uri_CompareEx(
            relfileuri_main, relfileuri_ast,
            0, -1, &fileismain
            )) {
        // Oom.
        uri_Free(relfileuri_main);
        uri_Free(relfileuri_ast);
        return 0;
    }
    uri_Free(relfileuri_main);
    uri_Free(relfileuri_ast);
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

int codegen_FinalBytecodeTransform(
    h64compileproject *prj
);

int compileproject_CompileAllToBytecode(
        h64compileproject *project,
        h64misccompileroptions *moptions,
        const char *mainfileuri,
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
                mainfileuri
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
