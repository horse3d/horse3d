#ifndef HORSE64_CODEMODULE_H_
#define HORSE64_CODEMODULE_H_

#include "compiler/astparser.h"
#include "compiler/warningconfig.h"

typedef struct h64compileproject h64compileproject;

h64ast *codemodule_GetASTUncached(
    h64compileproject *pr, const char *fileuri,
    h64compilewarnconfig *wconfig
);

#endif  // HORSE64_CODEMODULE_H_
