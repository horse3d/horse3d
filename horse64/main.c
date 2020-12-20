// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
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

#include "horse64/compiler/main.h"
#include "horse64/packageversion.h"
#include "filesys.h"
#include "nonlocale.h"
#include "vfs.h"
#include "widechar.h"


#define NOBETTERARGPARSE 1

int main_TryRunAttachedProgram(
        int *wasrun,
        int argc, const char **argv
        ) {
    *wasrun = 0;
    return 0;
}

#if defined(_WIN32) || defined(_WIN64)
int _actualmain(int argc, const char **argv) {
#else
int main(int argc, const char **argv) {
#endif
    #if defined(_WIN32) || defined(_WIN64)
    _setmode(_fileno(stdin), O_BINARY);
    #else
    signal(SIGPIPE, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    #endif
    char *exepath = filesys_GetOwnExecutable();
    vfs_Init(exepath ? exepath : argv[0]);
    free(exepath);

    // See if we have a program attached to run:
    int wasrun = 0;
    int result = main_TryRunAttachedProgram(
        &wasrun, argc, argv
    );
    if (wasrun)
        return result;

    // Ok, run as standalone horsec instead:
    _windows_ForceTerminalMode();
    int doubledash_seen = 0;
    const char *action = NULL;
    int action_offset = -1;
    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "--") == 0) {
            doubledash_seen = 1;
            i++;
            continue;
        }
        if (!doubledash_seen) {
            if (h64casecmp(argv[i], "-h") == 0 ||
                    strcmp(argv[i], "--help") == 0 ||
                    strcmp(argv[i], "-?") == 0 ||
                    strcmp(argv[i], "/?") == 0) {
                h64printf("Usage: horsec [action] "
                          "[...options + arguments...]\n");
                h64printf("\n");
                h64printf("Available actions:\n");
                h64printf("  - \"codeinfo\"          "
                          "Compile .h64 code and "
                          "show overview info of bytecode.\n");
                h64printf("  - \"compile\"           "
                          "Compile .h64 code "
                          "and output executable.\n");
                h64printf("  - \"get_asm\"           "
                          "Translate to .hasm\n");
                h64printf("  - \"get_ast\"           "
                          "Get AST of code\n");
                h64printf("  - \"get_resolved_ast\"  "
                          "Get AST of code with resolved identifiers\n");
                h64printf("  - \"get_tokens\"        "
                          "Get Tokenization of code\n");
                h64printf("  - \"run\"               "
                          "Compile .h64 code, and "
                          "run it immediately.\n");
                return 0;
            }
            if (strcmp(argv[i], "--version") == 0 ||
                    strcmp(argv[i], "-V") == 0) {
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
            if (!action && (strcmp(argv[i], "codeinfo") == 0 ||
                    strcmp(argv[i], "compile") == 0 ||
                    strcmp(argv[i], "exec") == 0 ||
                    strcmp(argv[i], "get_asm") == 0 ||
                    strcmp(argv[i], "get_ast") == 0 ||
                    strcmp(argv[i], "get_resolved_ast") == 0 ||
                    strcmp(argv[i], "get_tokens") == 0 ||
                    strcmp(argv[i], "run") == 0)) {
                action = argv[i];
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

    int return_value = 0;
    if (strcmp(action, "codeinfo") == 0) {
        if (!compiler_command_CodeInfo(argv, argc, action_offset))
            return -1;
    } else if (strcmp(action, "compile") == 0) {
        if (!compiler_command_Compile(argv, argc, action_offset))
            return -1;
    } else if (strcmp(action, "get_asm") == 0) {
        if (!compiler_command_ToASM(argv, argc, action_offset))
            return -1;
    } else if (strcmp(action, "get_ast") == 0) {
        if (!compiler_command_GetAST(argv, argc, action_offset))
            return -1;
    } else if (strcmp(action, "get_resolved_ast") == 0) {
        if (!compiler_command_GetResolvedAST(argv, argc, action_offset))
            return -1;
    } else if (strcmp(action, "get_tokens") == 0) {
        if (!compiler_command_GetTokens(argv, argc, action_offset))
            return -1;
    } else if (strcmp(action, "exec") == 0) {
        if (!compiler_command_Exec(argv, argc, action_offset, &return_value))
            return -1;
    } else if (strcmp(action, "run") == 0) {
        if (!compiler_command_Run(argv, argc, action_offset, &return_value))
            return -1;
    } else {
        return -1;
    }
    return return_value;
}

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
    #if defined(NOBETTERARGPARSE) && NOBETTERARGPARSE != 0

    // Shoddy argument splitting based on GetCommandLineW()

    int argc = 0;
    LPWSTR *_winSplitList = NULL;
    int _winSplitCount = 0;
    char **argv = malloc(sizeof(*argv));
    if (!argv)
        return 1;
    argv[0] = NULL;

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
        h64fprintf(
            stderr, "horsevm: error: arg alloc or convert "
            "failure"
        );
        return 1;
    }
    if (_winSplitCount > 0) {
        char **argv_new = realloc(
            argv, sizeof(*argv) * (_winSplitCount)
        );
        if (!argv_new)
            goto oom;
        argv = argv_new;
        memset(&argv[0], 0, sizeof(*argv) * (_winSplitCount));
        argc += _winSplitCount;
        int k = 0;
        while (k < _winSplitCount) {
            int argbuflen = wcslen(_winSplitList[k]) * 10 + 1;
            char *argbuf = malloc(argbuflen);
            if (!argbuf)
                goto oom;
            int64_t out_len = 0;
            if (!utf16_to_utf8(
                    _winSplitList[k], wcslen(_winSplitList[k]),
                    argbuf, argbuflen - 1, &out_len, 1
                   ) || out_len >= argbuflen)
                goto oom;
            argbuf[argbuflen - 1] = '\0';
            assert(k < argc);
            argv[k] = argbuf;
            k++;
        }
    }
    LocalFree(_winSplitList);
    _winSplitList = NULL;
    int result = _actualmain(argc, (const char**) argv);
    int k = 0;
    while (k < argc) {
        free(argv[k]);
        k++;
    }
    free(argv);
    return result;
    #else

    // Unix-style arg parsing:

    char **argv = malloc(sizeof(*argv));
    if (!argv) {
        oom:
        h64fprintf(stderr, "horsevm: error: arg alloc or convert "
            "failure");
        return 1;
    }
    char *execfullpath = filesys_GetOwnExecutable();
    char *execname = NULL;
    if (execfullpath) {
        char *execname = filesys_Basename(execfullpath);
        free(execfullpath);
        execfullpath = NULL;
    }
    if (execname) {
        argv[0] = strdup(execname);
    } else {
        argv[0] = strdup("horsec.exe");
    }
    if (!argv[0])
        goto oom;
    char *argline = NULL;
    {
        wchar_t *wcharCmdLine = GetCommandLineW();
        if (!wcharCmdLine)
            goto oom;
        {
            int arglinebytes = wcslen(wcharCmdLine) * 10 + 1;
            argline = malloc(arglinelen);
            if (!argline)
                goto oom;
            int64_t out_len = 0;
            if (!utf16_to_utf8(
                    wcharCmdLine, wcslen(wcharCmdLine),
                    argline, arglinebytes - 1, &out_len, 1
                    ) || out_len >= arglinebytes)
                goto oom;
            argline[out_len] = '\0';
        }
    }
    char *p = argline;
    int len = strlen(argline);
    char in_quote = '\0';
    int backslash_escaped = 0;
    int argc = 1;
    int i = 0;
    while (i <= len) {
        if (i >= len || (
                argline[i] == ' ' && in_quote == '\0' &&
                !backslash_escaped
                )) {
            char **new_argv = realloc(argv, sizeof(*argv) * (argc + 1));
            if (!new_argv)
                goto oom;
            argline[i] = '\0';
            int added_it = 0;
            if (strlen(p) > 0 && !str_is_spaces(p)) {
                added_it = 1;
                new_argv[argc] = strdup(p);
                char quote_removed = '\0';
                if ((new_argv[argc][0] == '"' &&
                        new_argv[argc][strlen(new_argv[argc]) - 1] == '"') ||
                        (new_argv[argc][0] == '\'' &&
                        new_argv[argc][strlen(new_argv[argc]) - 1] == '\'')) {
                    quote_removed = new_argv[argc][0];
                    memmove(
                        new_argv[argc], new_argv[argc] + 1,
                        strlen(new_argv[argc])
                    );
                    new_argv[argc][strlen(new_argv[argc]) - 1] = '\0';
                }
                int k = 0;
                while (k < (int)strlen(new_argv[argc]) - 1) {
                    if (new_argv[argc][k] == '\\' &&
                            (new_argv[argc][k + 1] == '\\' ||
                             (quote_removed != '\0' &&
                              new_argv[argc][k + 1] == quote_removed))) {
                        memmove(
                            new_argv[argc] + k, new_argv[argc] + k + 1,
                            strlen(new_argv[argc]) - k
                        );
                    }
                    k++;
                }
            }
            argv = new_argv;
            p = (argline + i + 1);
            if (added_it)
                argc++;
        } else if (backslash_escaped) {
            backslash_escaped = 0;
        } else if (in_quote == '\0' && !backslash_escaped && (
                argline[i] == '"' || argline[i] == '\'')) {
            in_quote = argline[i];
        } else if (in_quote != '\0' && in_quote == argline[i]) {
            in_quote = '\0';
        } else if (argline[i] == '\\' && in_quote != '\'') {
            backslash_escaped = 1;
        }
        i++;
    }
    free(argline);
    int result = _actualmain(argc, (const char**) argv);
    int k = 0;
    while (k < argc) {
        free(argv[k]);
        k++;
    }
    free(argv);
    return result;
    #endif
}
#endif

