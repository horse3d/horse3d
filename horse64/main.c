
#include <SDL2/SDL.h>
#include <stdio.h>

#include "filesys.h"
#include "scriptcore.h"
#include "vfs.h"


int main(int argc, char **argv) {
    SDL_SetHintWithPriority(
        "SDL_MOUSE_FOCUS_CLICKTHROUGH", "1",
        SDL_HINT_OVERRIDE
    );

    // Initialize essentials:
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|
                 SDL_INIT_EVENTS|SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "horse3d/main.c: error: SDL "
                "initialization failed: %s\n", SDL_GetError());
        return 1;
    }
    // Initialize non-essentials:
    if (SDL_InitSubSystem(SDL_INIT_JOYSTICK) == 0)
        SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    SDL_InitSubSystem(SDL_INIT_HAPTIC);

    vfs_Init(argv[0]);

    return scriptcore_Run(argc, argv);
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
    SDL_SetMainReady();
    int result = SDL_main(argc, argv);
    int k = 0;
    while (k < argc) {
        free(argv[k]);
        k++;
    }
    free(argv);
    return result;
}
#endif

