// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_COMPILER_MAIN_H_
#define HORSE64_COMPILER_MAIN_H_

#include "compileconfig.h"

#include "json.h"
#include "widechar.h"

typedef struct h64compilewarnconfig h64compilewarnconfig;
typedef struct h64misccompileroptions h64misccompileroptions;

jsonvalue *compiler_TokenizeToJSON(
    h64misccompileroptions *moptions,
    const h64wchar *fileuri, int64_t fileurilen,
    h64compilewarnconfig *wconfig
);

jsonvalue *compiler_ParseASTToJSON(
    h64misccompileroptions *moptions,
    const h64wchar *fileuri, int64_t fileurilen,
    h64compilewarnconfig *wconfig,
    int resolve_references
);

int compiler_command_Compile(
    const h64wchar **argv, int64_t *argvlen, int argc, int argoffset
);

int compiler_command_GetAST(
    const h64wchar **argv, int64_t *argvlen, int argc, int argoffset
);

int compiler_command_GetResolvedAST(
    const h64wchar **argv, int64_t *argvlen, int argc, int argoffset
);

int compiler_command_GetTokens(
    const h64wchar **argv, int64_t *argvlen, int argc, int argoffset
);

int compiler_command_Run(
    const h64wchar **argv, int64_t *argvlen, int argc,
    int argoffset, int *return_int
);

int compiler_command_CodeInfo(
    const h64wchar **argv, int64_t *argvlen, int argc,
    int argoffset
);

int compiler_command_ToASM(
    const h64wchar **argv, int64_t *argvlen, int argc,
    int argoffset
);

int compiler_command_Exec(
    const h64wchar **argv, int64_t *argvlen, int argc,
    int argoffset, int *return_int
);

typedef struct h64misccompileroptions {
    int vmexec_debug;
    int compiler_stage_debug;
    int import_debug;
    int from_stdin;
    int vmscheduler_debug, vmscheduler_verbose_debug;
    int vmsockets_debug;
    int vmasyncjobs_debug;
    int compile_project_debug;
} h64misccompileroptions;

#endif  // HORSE64_COMPILER_MAIN_H_
