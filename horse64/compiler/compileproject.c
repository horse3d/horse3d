
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/ast.h"
#include "compiler/astparser.h"
#include "compiler/codemodule.h"
#include "compiler/compileproject.h"
#include "filesys.h"
#include "hash.h"
#include "secrandom.h"
#include "uri.h"


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

    uriinfo *uinfo = uri_ParseEx(basefolderuri, "https");
    if (!uinfo || !uinfo->path || !uinfo->protocol ||
            strcasecmp(uinfo->protocol, "file") != 0) {
        uri_Free(uinfo);
        free(pr);
        return NULL;
    }

    char *s = filesys_ToAbsolutePath(uinfo->path);
    uri_Free(uinfo); uinfo = NULL;
    if (!s) {
        free(pr);
        return NULL;
    }
    pr->basefolder = filesys_Normalize(s);
    free(s);
    if (!pr->basefolder) {
        free(pr);
        return NULL;
    }

    if (!secrandom_GetBytes(
            pr->hashsecret, sizeof(*pr->hashsecret)
            )) {
        free(pr->basefolder);
        free(pr);
        return NULL;
    }
    pr->astfilemap = hash_NewStringMap(32);
    if (!pr->astfilemap) {
        free(pr->basefolder);
        free(pr);
        return NULL;
    }

    return pr;
}

char *compileproject_ToProjectRelPath(
        h64compileproject *pr, const char *fileuri
        ) {
    if (!fileuri || !pr || !pr->basefolder)
        return NULL;

    uriinfo *uinfo = uri_ParseEx(fileuri, "https");
    if (!uinfo || !uinfo->path || !uinfo->protocol ||
            strcasecmp(uinfo->protocol, "file") != 0) {
        uri_Free(uinfo);
        return NULL;
    }

    char *s = filesys_ToAbsolutePath(uinfo->path);
    uri_Free(uinfo); uinfo = NULL;
    if (!s) return NULL;

    char *result = filesys_TurnIntoPathRelativeTo(
        s, pr->basefolder
    );
    free(s); s = NULL;
    if (!result) return NULL;

    char *resultnormalized = filesys_Normalize(result);
    free(result);
    if (!resultnormalized) return NULL;

    return resultnormalized;
}

int compileproject_GetAST(
        h64compileproject *pr, const char *fileuri,
        h64ast *out_ast, char **error
        ) {
    char *relfilepath = compileproject_ToProjectRelPath(
        pr, fileuri
    );
    if (!relfilepath) {
        *error = strdup(
            "cannot get AST of file outside of project root"
        );
        return 0;
    }
 
    uint64_t entry;
    if (hash_StringMapGet(
            pr->astfilemap, relfilepath, &entry
            ) && entry > 0) {
        h64ast *resultptr = (h64ast*)(uintptr_t)entry;
        if (resultptr->stmt_count > 0) {
            free(relfilepath);
            memcpy(out_ast, resultptr, sizeof(*resultptr));
            return 1;
        }
        hash_StringMapUnset(
            pr->astfilemap, relfilepath
        );
        result_FreeContents(&resultptr->resultmsg);
        ast_FreeContents(resultptr);
        free(resultptr);
    }

    char *absfilepath = filesys_Join(
        pr->basefolder, relfilepath
    );
    if (!absfilepath) {
        free(relfilepath);
        *error = strdup("alloc fail");
        return 0;
    }

    h64ast result = codemodule_GetASTUncached(
        pr, absfilepath, &pr->warnconfig
    );
    free(absfilepath); absfilepath = NULL;
    h64ast *resultalloc = malloc(sizeof(*resultalloc));
    if (!resultalloc) {
        ast_FreeContents(&result);
        free(relfilepath);
        *error = strdup("alloc fail");
        return 0;
    }
    memcpy(resultalloc, &result, sizeof(result));

    if (!hash_StringMapSet(
            pr->astfilemap, relfilepath, (uintptr_t)resultalloc
            )) {
        ast_FreeContents(&result);
        free(relfilepath);
        free(resultalloc);
        *error = strdup("alloc fail");
        return 0;
    }
    memcpy(out_ast, &result, sizeof(result));
    free(relfilepath);
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

void compileproject_Free(h64compileproject *pr) {
    if (!pr) return;

    free(pr->basefolder);

    if (pr->astfilemap) {
        hash_StringMapIterate(
            pr->astfilemap, &_compileproject_astfreecallback, NULL
        );
        hash_FreeMap(pr->astfilemap);
    }
    free(pr);
}

char *compileproject_FolderGuess(
        const char *fileuri, int cwd_fallback_if_appropriate,
        char **error
        ) {
    uriinfo *uinfo = uri_ParseEx(fileuri, "https");
    if (!uinfo || !uinfo->path || !uinfo->protocol ||
            strcasecmp(uinfo->protocol, "file") != 0) {
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
            while (strlen(snew) > 0 && (snew[strlen(snew) - 1] == '/'
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
        char *cwd = filesys_GetCurrentDirectory();
        if (!cwd) {
            free(full_path);
            *error = strdup("alloc failure");
            return NULL;
        }
        if (filesys_FolderContainsPath(cwd, full_path)) {    
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
