// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_COMPILER_COMPILEPROJECT_H_
#define HORSE64_COMPILER_COMPILEPROJECT_H_

#include "compiler/warningconfig.h"
#include "debugsymbols.h"
#include "widechar.h"

typedef struct hashmap hashmap;
typedef struct h64program h64program;
typedef struct h64result h64result;
typedef struct h64misccompileroptions h64misccompileroptions;
typedef struct h64expression h64expression;
typedef struct h64threadablecheck_graph h64threadablecheck_graph;

typedef struct h64compileproject {
    h64compilewarnconfig warnconfig;

    h64wchar *basefolder;
    int64_t basefolderlen;
    hashmap *astfilemap;
    int astfilemap_count;
    h64program *program;

    // Temporarily used by codegen:
    h64expression *_tempglobalfakeinitfunc;
    hashmap *_tempclassesfakeinitfunc_map;

    // Temporarily used by scoperesolver:
    int *_class_was_propagated;

    // Temporarily used by threadablechecker:
    h64threadablecheck_graph *threadable_graph;

    h64result *resultmsg;
} h64compileproject;

typedef struct uri32info uri32info;

typedef struct h64ast h64ast;

h64compileproject *compileproject_New(
    const h64wchar *basefolderuri, int64_t basefolderurilen,
    h64misccompileroptions *moptions
);

uri32info *compileproject_URIRelPathToBase(
    const h64wchar *basepath, int64_t basepathlen,
    const h64wchar *fileuri, int64_t fileurilen,
    int *outofmemory
);

char *compileproject_ToProjectRelPath(
    h64compileproject *pr, const char *fileuri,
    int *outofmemory
);

int compileproject_GetAST(
    h64compileproject *pr,
    const h64wchar *fileuri, int64_t fileurilen,
    h64misccompileroptions *moptions,
    h64ast **out_ast, char **error
);

void compileproject_Free(h64compileproject *pr);

h64wchar *compileproject_FolderGuess(
    const h64wchar *fileuri, int64_t fileurilen,
    int cwd_fallback_if_appropriate,
    h64misccompileroptions *moptions,
    int64_t *output_len, char **error
);

uri32info *compileproject_GetFileSubProjectURI(
    h64compileproject *pr,
    const h64wchar *sourcefileuri, int64_t sourcefileurilen,
    char **subproject_name, int *outofmemory
);

int compileproject_DoesImportMapToCFuncs(
    h64compileproject *pr,
    const char **import_elements, int import_elements_count,
    const char *library_source, int print_debug_info,
    int *outofmemory
);

h64wchar *compileproject_ResolveImportToFile(
    h64compileproject *pr,
    const h64wchar *sourcefileuri, int64_t sourcefileurilen,
    const char **import_elements, int import_elements_count,
    const char *library_source,
    int print_debug_info,
    int64_t *out_len,
    int *outofmemory
);

int compileproject_CompileAllToBytecode(
    h64compileproject *project,
    h64misccompileroptions *moptions,
    const h64wchar *mainfileuri, int64_t mainfileurilen,
    char **error
);

#endif  // HORSE64_COMPILER_COMPILEPROJECT_H_
