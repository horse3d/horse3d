// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include <stdio.h>
#include <string.h>

#include "horse64/compiler/main.h"
#include "horse64/packageversion.h"
#include "filesys.h"
#include "vfs.h"

#if defined(_WIN32) || defined(_WIN64)
int _actualmain(int argc, const char **argv) {
#else
int main(int argc, const char **argv) {
#endif
    vfs_Init(argv[0]);

    int doubledash_seen = 0;
    const char *action = NULL;
    int action_offset = -1;
    const char *action_file = NULL;
    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "--") == 0) {
            doubledash_seen = 1;
            i++;
            continue;
        }
        if (!doubledash_seen) {
            if (strcasecmp(argv[i], "-h") == 0 ||
                    strcmp(argv[i], "--help") == 0 ||
                    strcmp(argv[i], "-?") == 0 ||
                    strcmp(argv[i], "/?") == 0) {
                printf("Usage: horsec [action] "
                       "[...options + arguments...]\n");
                printf("\n");
                printf("Available actions:\n");
                printf("  - \"codeinfo\"          Compile .h64 code and show "
                       "describe resulting bytecode.\n");
                printf("  - \"compile\"           Compile .h64 code "
                       "and output executable.\n");
                printf("  - \"to_asm\"            Translate to .hasm\n");
                printf("  - \"get_ast\"           Get AST of code\n");
                printf("  - \"get_resolved_ast\"  "
                       "Get AST of code with resolved identifiers\n");
                printf("  - \"get_tokens\"        Get Tokenization of code\n");
                printf("  - \"run\"               Compile .h64 code, and "
                       "run it immediately.\n");
                return 0;
            }
            if (strcmp(argv[i], "--version") == 0 ||
                    strcmp(argv[i], "-V") == 0) {
                printf(
                    "org.horse64.core with horsec/horsevm\n"
                    "\n"
                    "   Corelib version:   %s\n"
                    "   Build time:        %s\n"
                    "   Compiler version:  %s%s\n",
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
                    strcmp(argv[i], "to_asm") == 0 ||
                    strcmp(argv[i], "get_ast") == 0 ||
                    strcmp(argv[i], "get_resolved_ast") == 0 ||
                    strcmp(argv[i], "get_tokens") == 0 ||
                    strcmp(argv[i], "run") == 0)) {
                action = argv[i];
                action_offset = i + 1;
                break;
            } else if (!action && argv[i][0] != '-' &&
                       argv[i][0] != '/') {
                fprintf(stderr, "horsecc: error: unknown action, "
                    "try --help: \"%s\"\n", argv[i]);
                return -1;
            }
        }
        i++;
    }
    if (!action) {
        fprintf(stderr, "horsecc: error: need action, "
            "like horsecc run. See horsecc --help\n");
        return -1;
    }

    if (strcmp(action, "codeinfo") == 0) {
        if (!compiler_command_CodeInfo(argv, argc, action_offset))
            return -1;
    } else if (strcmp(action, "compile") == 0) {
        if (!compiler_command_Compile(argv, argc, action_offset))
            return -1;
    } else if (strcmp(action, "to_asm") == 0) {
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
    } else if (strcmp(action, "run") == 0) {
        if (!compiler_command_Run(argv, argc, action_offset))
            return -1;
    } else {
        return -1;
    }
    return 0;
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


int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev,
                   LPSTR szCmdLine, int sw) {
    char **argv = malloc(sizeof(*argv));
    if (!argv)
        return 1;
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
        return 1;
    char *argline = NULL;
    if (szCmdLine)
        argline = strdup(szCmdLine);
    if (!argline)
        return 1;
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
                return 1;
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
    int result = _actualmain(argc, argv);
    int k = 0;
    while (k < argc) {
        free(argv[k]);
        k++;
    }
    free(argv);
    return result;
}
#endif

