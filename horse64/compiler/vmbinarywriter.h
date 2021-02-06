// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause


#ifndef HORSE64_COMPILER_VMBINARYWRITER_H_
#define HORSE64_COMPILER_VMBINARYWRITER_H_

#include "compileconfig.h"

#include "widechar.h"

typedef struct h64program h64program;

int vmbinarywriter_WriteProgram(
    const h64wchar *targetfile, int64_t targetfilelen,
    h64program *program,
    char **error
);


#endif  // HORSE64_COMPILER_VMBINARYWRITER_H_