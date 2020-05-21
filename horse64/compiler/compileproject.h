#ifndef HORSE64_COMPILER_COMPILEPROJECT_H_
#define HORSE64_COMPILER_COMPILEPROJECT_H_

#include "compiler/warningconfig.h"
#include "debugsymbols.h"

typedef struct hashmap hashmap;
typedef struct h64program h64program;
typedef struct h64result h64result;


typedef struct h64compileproject {
    h64compilewarnconfig warnconfig;

    char hashsecret[16];

    char *basefolder;
    hashmap *astfilemap;
    h64program *program;

    h64result *resultmsg;
} h64compileproject;

typedef struct h64ast h64ast;

h64compileproject *compileproject_New(
    const char *basefolderuri
);

char *compileproject_ToProjectRelPath(
    h64compileproject *pr, const char *fileuri
);

int compileproject_GetAST(
    h64compileproject *pr, const char *fileuri,
    h64ast **out_ast, char **error
);

void compileproject_Free(h64compileproject *pr);

char *compileproject_FolderGuess(
    const char *fileuri, int cwd_fallback_if_appropriate,
    char **error
);

char *compileproject_ResolveImport(
    h64compileproject *pr,
    const char *sourcefileuri,
    const char **import_elements, int import_elements_count,
    const char *library_source,
    int *outofmemory
);

#endif  // HORSE64_COMPILER_COMPILEPROJECT_H_
