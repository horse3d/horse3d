// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_CODEMODULE_H_
#define HORSE64_CODEMODULE_H_

#include "compiler/astparser.h"
#include "compiler/warningconfig.h"

typedef struct h64compileproject h64compileproject;
typedef struct uri32info uri32info;

h64ast *codemodule_GetASTUncached(
    h64compileproject *pr, uri32info *fileuri,
    h64compilewarnconfig *wconfig
);

#endif  // HORSE64_CODEMODULE_H_
