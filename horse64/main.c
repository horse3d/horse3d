// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause


#include "compileconfig.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#if defined(_WIN32) || defined(_WIN64)
#include <fcntl.h>
#include <windows.h>
#else
#include <signal.h>
#endif

#include "bytecode.h"
#include "bytecodeserialize.h"
#include "horse64/compiler/main.h"
#include "horse64/packageversion.h"
#include "filesys.h"
#include "mainpreinit.h"
#include "nonlocale.h"
#include "vfs.h"
#include "widechar.h"

void _load_unicode_data();  // widechar.c


#define NOBETTERARGPARSE 1

int main_TryRunAttachedProgram(
        int *wasrun,
        int argc, const h64wchar **argv, int64_t *argvlen
        ) {
    *wasrun = 0;
    int _progexists = 0;
    if (!vfs_ExistsEx(NULL, "__horse_program.hasm_raw",
            &_progexists, VFSFLAG_NO_REALDISK_ACCESS)) {
        fprintf(stderr,
            "main.c: error: failed to check VFS code bytecode - "
            "out of memory or disk I/O error?\n");
        *wasrun = 1;
        return -1;
    }
    if (!_progexists)
        return 0;

    char *programbytes = NULL;
    uint64_t programbyteslen = 0;
    if (!vfs_SizeEx(
            NULL, "__horse_program.hasm_raw", &programbyteslen,
            VFSFLAG_NO_REALDISK_ACCESS
            )) {
        fprintf(stderr,
            "main.c: error: failed to check VFS code bytecode - "
            "out of memory or disk I/O error?\n");
        *wasrun = 1;
        return -1;
    }
    programbytes = malloc(programbyteslen);
    if (!programbytes) {
        fprintf(stderr,
            "main.c: error: out of memory loading bytecode\n");
        *wasrun = 1;
        return -1;
    }
    if (!vfs_GetBytesEx(
            NULL, "__horse_program.hasm_raw",
            0, programbyteslen, programbytes,
            VFSFLAG_NO_REALDISK_ACCESS
            )) {
        fprintf(stderr,
            "main.c: error: failed to check VFS code bytecode - "
            "out of memory or disk I/O error?\n");
        *wasrun = 1;
        return -1;
    }
    h64program *p = NULL;
    if (!h64program_Restore(
            &p, programbytes, programbyteslen
            )) {
        fprintf(stderr,
            "main.c: error: failed to load VFS code bytecode - "
            "out of memory or disk I/O error?\n");
        *wasrun = 1;
        return -1;
    }

    *wasrun = 1;
    h64misccompileroptions moptions = {0};
    int resultcode = vmschedule_ExecuteProgram(
        p, &moptions
    );
    h64program_Free(p);
    return resultcode;
}


int mainu32(int argc, const h64wchar **argv, int64_t *argvlen) {
    main_PreInit();

    // See if we have a program attached to run:
    int wasrun = 0;
    int result = main_TryRunAttachedProgram(
        &wasrun, argc, argv, argvlen
    );
    if (wasrun)
        return result;

    // Ok, run as standalone horsec instead:
    _windows_ForceTerminalMode();
    int doubledash_seen = 0;
    int64_t actionlen = 0;
    const h64wchar *action = NULL;
    int action_offset = -1;
    int i = 1;
    while (i < argc) {
        if (h64cmp_u32u8(argv[i], argvlen[i], "--") == 0) {
            doubledash_seen = 1;
            i++;
            continue;
        }
        if (!doubledash_seen) {
            if (h64cmp_u32u8(argv[i], argvlen[i], "-h") == 0 ||
                    h64cmp_u32u8(argv[i], argvlen[i], "--help") == 0 ||
                    h64cmp_u32u8(argv[i], argvlen[i], "-?") == 0 ||
                    h64cmp_u32u8(argv[i], argvlen[i], "/?") == 0) {
                h64printf("Usage: horsec [action] "
                          "[...options + arguments...]\n");
                h64printf("\n");
                h64printf("Available actions:\n");
                h64printf("  - \"codeinfo\"          "
                          "Compile .h64 code and "
                          "show overview info of bytecode.\n");
                h64printf("  - \"compile\"           "
                          "Compile code from .h64 file "
                          "to standalone executable.\n");
                h64printf("  - \"exec\"              "
                          "Run the given argument as literal code "
                          "immediately.\n");
                h64printf("  - \"get_asm\"           "
                          "Translate to .hasm\n");
                h64printf("  - \"get_ast\"           "
                          "Get AST of code\n");
                h64printf("  - \"get_resolved_ast\"  "
                          "Get AST of code with resolved identifiers\n");
                h64printf("  - \"get_tokens\"        "
                          "Get Tokenization of code\n");
                h64printf("  - \"run\"               "
                          "Compile .h64 code from a file, and "
                          "run it immediately.\n");
                return 0;
            }
            if (h64cmp_u32u8(argv[i], argvlen[i], "--version") == 0 ||
                    h64cmp_u32u8(argv[i], argvlen[i], "-V") == 0 ||
                    h64cmp_u32u8(argv[i], argvlen[i], "-version") == 0 ||
                    h64cmp_u32u8(argv[i], argvlen[i], "-v") == 0) {
                h64printf(
                    "org.horse64.core horsec/horsevm v%s.\n"
                    "- Corelib version:   %s\n"
                    "- Build time:        %s\n"
                    "- Compiler version:  %s%s\n",
                    CORELIB_VERSION,
                    CORELIB_VERSION, BUILD_TIME,
                    #if defined(__clang__)
                    "clang-", __clang_version__
                    #elif defined(__GNUC__) && !defined(__clang__)
                    "gcc-", __VERSION__
                    #else
                    "unknown-", "unknown"
                    #endif
                );
                return 0;
            }
            if (!action && (
                    h64cmp_u32u8(argv[i], argvlen[i], "codeinfo") == 0 ||
                    h64cmp_u32u8(argv[i], argvlen[i], "compile") == 0 ||
                    h64cmp_u32u8(argv[i], argvlen[i], "exec") == 0 ||
                    h64cmp_u32u8(argv[i], argvlen[i], "get_asm") == 0 ||
                    h64cmp_u32u8(argv[i], argvlen[i], "get_ast") == 0 ||
                    h64cmp_u32u8(argv[i], argvlen[i],
                        "get_resolved_ast") == 0 ||
                    h64cmp_u32u8(argv[i], argvlen[i], "get_tokens") == 0 ||
                    h64cmp_u32u8(argv[i], argvlen[i], "run") == 0)) {
                action = argv[i];
                actionlen = argvlen[i];
                action_offset = i + 1;
                break;
            } else if (!action && argv[i][0] != '-' &&
                       argv[i][0] != '/') {
                h64fprintf(
                    stderr, "horsec: error: unknown action, "
                    "try --help: \"%s\"\n", argv[i]
                );
                return -1;
            }
        }
        i++;
    }
    if (!action) {
        h64fprintf(stderr, "horsec: error: need action, "
            "like horsec run. See horsec --help\n");
        return -1;
    }
    char *action_u8_alloc = AS_U8(action, actionlen);
    if (!action_u8_alloc) {
        h64fprintf(stderr, "horsec: error: alloc "
            "fail for action u8 conversion\n");
        return -1;
    }
    char action_u8[64];
    snprintf(
        action_u8, sizeof(action_u8),
        "%s", action_u8_alloc
    );
    action_u8[sizeof(action_u8) - 1] = '\0';
    free(action_u8_alloc);
    action_u8_alloc = NULL;

    int return_value = 0;
    if (strcmp(action_u8, "codeinfo") == 0) {
        if (!compiler_command_CodeInfo(
                argv, argvlen, argc, action_offset
                ))
            return -1;
    } else if (strcmp(action_u8, "compile") == 0) {
        if (!compiler_command_Compile(
                argv, argvlen, argc, action_offset
                ))
            return -1;
    } else if (strcmp(action_u8, "get_asm") == 0) {
        if (!compiler_command_ToASM(
                argv, argvlen, argc, action_offset
                ))
            return -1;
    } else if (strcmp(action_u8, "get_ast") == 0) {
        if (!compiler_command_GetAST(
                argv, argvlen, argc, action_offset
                ))
            return -1;
    } else if (strcmp(action_u8, "get_resolved_ast") == 0) {
        if (!compiler_command_GetResolvedAST(
                argv, argvlen, argc, action_offset
                ))
            return -1;
    } else if (strcmp(action_u8, "get_tokens") == 0) {
        if (!compiler_command_GetTokens(
                argv, argvlen, argc, action_offset
                ))
            return -1;
    } else if (strcmp(action_u8, "exec") == 0) {
        if (!compiler_command_Exec(
                argv, argvlen, argc, action_offset, &return_value
                ))
            return -1;
    } else if (strcmp(action_u8, "run") == 0) {
        if (!compiler_command_Run(
                argv, argvlen, argc, action_offset, &return_value
                ))
            return -1;
    } else {
        return -1;
    }
    return return_value;
}

#if !defined(_WIN32) && !defined(_WIN64)
int main(int argc, const char **argv) {
    h64wchar **u32args = malloc(sizeof(*u32args) * (
        argc > 0 ? argc : 1
    ));
    if (!u32args) {
        h64fprintf(stderr, "main.c: error: alloc "
            "fail for u32 conversion\n");
        return -1;
    }
    int64_t *u32argslen = malloc(sizeof(*u32argslen) * (
        argc > 0 ? argc : 1
    ));
    if (!u32argslen) {
        free(u32args);
        h64fprintf(stderr, "main.c: error: alloc "
            "fail for u32 conversion\n");
        return -1;
    }
    int k = 0;
    while (k < argc) {
        u32args[k] = AS_U32(
            argv[k], &u32argslen[k]
        );
        if (!u32args[k]) {
            int k2 = 0;
            while (k2 < k)
                free(u32args[k]);
            free(u32args);
            free(u32argslen);
            h64fprintf(stderr, "main.c: error: alloc "
                "fail for u32 conversion\n");
            return -1;
        }
        k++;
    }
    int result = mainu32(
        argc, (const h64wchar **)u32args, u32argslen
    );
    k = 0;
    while (k < argc) {
        free(u32args[k]);
        k++;
    }
    free(u32args);
    free(u32argslen);
    return result;
}
#endif

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
static int str_is_spaces(const char *s) {
    if (!*s)
        return 0;
    while (*s) {
        if (*s == ' ') {
            s++;
            continue;
        }
        return 0;
    }
    return 1;
}

int WINAPI WinMain(
        ATTR_UNUSED HINSTANCE hInst, ATTR_UNUSED HINSTANCE hPrev,
        ATTR_UNUSED LPSTR szCmdLine, ATTR_UNUSED int sw
        ) {
    // Shoddy argument splitting based on GetCommandLineW(),
    // since Windows is weird.

    int argc = 0;
    LPWSTR *_winSplitList = NULL;
    int _winSplitCount = 0;
    h64wchar **argv = malloc(sizeof(*argv));
    int64_t *argvlen = malloc(sizeof(*argvlen));
    if (!argvlen || !argv) {
        free(argvlen);
        free(argv);
        h64fprintf(
            stderr, "main.c: error: arg alloc or convert "
            "failure"
        );
        return -1;
    }

    _winSplitList = CommandLineToArgvW(
        GetCommandLineW(), &_winSplitCount
    );
    if (!_winSplitList) {
        oom: ;
        if (_winSplitList) LocalFree(_winSplitList);
        int i = 0;
        while (i < argc) {
            free(argv[i]);
            i++;
        }
        free(argv);
        free(argvlen);
        h64fprintf(
            stderr, "main.c: error: arg alloc or convert "
            "failure"
        );
        return -1;
    }
    if (_winSplitCount > 0) {
        h64wchar **argv_new = realloc(
            argv, sizeof(*argv) * (_winSplitCount)
        );
        if (!argv_new)
            goto oom;
        argv = argv_new;
        memset(&argv[0], 0, sizeof(*argv) * (_winSplitCount));
        argc += _winSplitCount;
        int k = 0;
        while (k < _winSplitCount) {
            assert(sizeof(wchar_t) == sizeof(uint16_t));
            int64_t argbuflen = 0;
            h64wchar *argbuf = NULL;
            int64_t out_len = 0;
            ATTR_UNUSED int wasoom;
            argbuf = utf16_to_utf32(
                _winSplitList[k], wcslen(_winSplitList[k]),
                &argbuflen, 1, &wasoom
            );
            if (!argbuf)
                goto oom;
            assert(k < argc);
            argv[k] = argbuf;
            argvlen[k] = argbuflen;
            k++;
        }
    }
    LocalFree(_winSplitList);
    _winSplitList = NULL;
    int result = mainu32(argc, (const h64wchar **)argv, argvlen);
    int k = 0;
    while (k < argc) {
        free(argv[k]);
        k++;
    }
    free(argv);
    free(argvlen);
    return result;
}
#endif

