#ifndef HORSE64_COMPILEPROJECT_H_
#define HORSE64_COMPILEPROJECT_H_

typedef struct hashmap hashmap;

typedef struct h64compileproject {
    char hashsecret[16];

    char *basefolder;
    hashmap *astfilemap; 
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
    h64ast *out_ast, char **error
);

void compileproject_Free(h64compileproject *pr);

char *compileproject_FolderGuess(
    const char *fileuri, int cwd_fallback_if_appropriate,
    char **error
);

#endif  // HORSE64_COMPILEPROJECT_H_
