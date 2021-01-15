// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#if defined(__linux__) || defined(__LINUX)
pid_t vfork(void);
#endif

#include "nonlocale.h"
#include "poolalloc.h"
#include "processrun.h"
#include "threading.h"
#include "widechar.h"


typedef int64_t h64pid_t;

typedef struct processrun {
    h64wchar *path; int64_t path_len;
    int arg_count; h64wchar **arg_s; int64_t *arg_len;
    h64pid_t pid;
    volatile uint8_t known_exited;
    volatile int32_t exit_code;
    int refcount;
} processrun;

mutex *_processrun_mutex = NULL;
poolalloc *processrun_struct_alloc = NULL;


__attribute__((constructor)) static void _processrun_CreateMutex() {
    _processrun_mutex = mutex_Create();
    if (!_processrun_mutex) {
        h64fprintf(stderr, "horsevm: error: OOM creeating process mutex\n");
        exit(1);
    }
}

int launched_process_count = 0;
int launched_process_alloc = 0;
processrun **launched_process = NULL;

void _processrun_Free(processrun *pr) {
    if (!pr)
        return;
    poolalloc_free(processrun_struct_alloc, pr);
}

void processrun_Deref(processrun *run) {
    mutex_Lock(_processrun_mutex);
    run->refcount--;
    if (run->refcount <= 0 && run->known_exited) {
        int i = 0;
        while (i < launched_process_count) {
            if (launched_process[i] == run) {
                _processrun_Free(launched_process[i]);
                launched_process[i] = NULL;
                if (i + 1 < launched_process_count) {
                    memmove(
                        &launched_process[i],
                        &launched_process[i + 1],
                        sizeof(*launched_process) * (
                            launched_process_count - 1 - i
                        )
                    );
                }
                launched_process_count--;
                mutex_Release(_processrun_mutex);
                return;
            }
            i++;
        }
    }
    mutex_Release(_processrun_mutex);
}

processrun *processrun_Launch(
        const h64wchar *path, int64_t path_len,
        int arg_count, const h64wchar **arg_s, const int64_t *arg_len,
        int search_in_path
        ) {
    if (!path || path_len <= 0)
        return NULL;
    assert(_processrun_mutex != NULL);
    #if defined(_WIN32) || defined(Win64)
    wchar_t *process_converted_path = NULL;
    wchar_t *process_converted_cmdline = NULL;
    #else
    char *process_converted_path = NULL;
    char **process_converted_args = NULL;
    process_converted_args = malloc(
        sizeof(char*) * (arg_count + 1)
    );
    if (!process_converted_args)
        return NULL;
    memset(
        process_converted_args, 0, sizeof(char*) * (arg_count + 1)
    );
    #endif

    if (launched_process_alloc <= launched_process_count) {
        int new_alloc = launched_process_alloc + 1;
        processrun **newlaunched = realloc(
            launched_process, new_alloc * sizeof(*newlaunched)
        );
        if (!newlaunched)
            return NULL;
        launched_process = newlaunched;
        launched_process_count = new_alloc;
    }

    // Get mutex, and ensure the pool allocator exists:
    mutex_Lock(_processrun_mutex);
    processrun *result = NULL;
    if (!processrun_struct_alloc) {
        processrun_struct_alloc = (
            poolalloc_New(sizeof(processrun))
        );
        if (!processrun_struct_alloc) {
            oom:
            // Out of memory exit branch.
            if (result)
                _processrun_Free(result);
            mutex_Release(_processrun_mutex);
            #if defined(_WIN32) || defined(Win64)
            free(process_converted_path);
            free(process_converted_cmdline);
            #else
            free(process_converted_path);
            if (process_converted_args) {
                int i = 0;
                while (i < arg_count) {
                    free(process_converted_args[i]);
                    i++;
                }
            }
            #endif
            return NULL;
        }
    }

    // Make struct and copy all arguments to it:
    result = poolalloc_malloc(processrun_struct_alloc, 0);
    if (!result)
        goto oom;
    memset(result, 0, sizeof(*result));
    result->path = malloc(sizeof(h64wchar) * path_len);
    if (!result->path)
        goto oom;
    memcpy(result->path, path, path_len);
    result->path_len = path_len;
    result->arg_count = arg_count;
    if (arg_count > 0) {
        result->arg_s = malloc(sizeof(*arg_s) * arg_count);
        if (!result->arg_s)
            goto oom;
        memset(result->arg_s, 0, sizeof(*arg_s) * arg_count);
    }
    int i = 0;
    while (i < arg_count) {
        result->arg_s[i] = malloc(arg_len[i] * sizeof(h64wchar));
        if (!result->arg_s[i])
            goto oom;
        memcpy(result->arg_s[i], arg_s[i],
               arg_len[i] * sizeof(h64wchar));
        i++;
    }

    // Prepare arguments for launch:
    #if defined(_WIN32) || defined(_WIN64)
    assert(0);  // FIXME
    #else
    process_converted_path = malloc(path_len * 5 + 1);
    if (!process_converted_path)
        goto oom;
    int64_t out_len = 0;
    int convertresult = utf32_to_utf8(
        path, path_len, process_converted_path, path_len * 5 + 1,
        &out_len, 1, 1
    );
    if (!convertresult || out_len >= path_len * 5 + 1)
        goto oom;
    process_converted_path[out_len] = '\0';
    {
        i = 0;
        while (i < arg_count) {
            process_converted_args[i] = malloc(
                arg_len[i] * 5 + 1
            );
            if (!process_converted_args[i])
                goto oom;
            out_len = 0;
            int convertresult = utf32_to_utf8(
                arg_s[i], arg_len[i], process_converted_args[i],
                arg_len[i] * 5 + 1, &out_len, 1, 1
            );
            if (!convertresult || out_len >= arg_len[i] * 5 + 1)
                goto oom;
            process_converted_args[i][out_len] = '\0';
            i++;
        }
    }
    #endif

    // Launch process:
    #if defined(_WIN32) || defined(_WIN64)

    #else
    pid_t vpid = vfork();
    if (vpid < 0) {
        goto oom;
    }

    if (vpid == 0) {
        // vfork() -> child
        if (search_in_path) {
            if (execvp(process_converted_path,
                       process_converted_args) < 0) {
                _exit(1);
            }
        } else {
            if (execv(process_converted_path,
                      process_converted_args) < 0) {
                _exit(1);
            }
        }
        // this is unreachable, child is running other process here
    }
    // vfork() -> parent
    free(process_converted_path);
    i = 0;
    while (i < arg_count) {
        free(process_converted_args[i]);
        i++;
    }
    free(process_converted_args);
    result->pid = vpid;
    #endif

    result->refcount++;
    launched_process[launched_process_count] = result;
    launched_process_count++;
    mutex_Release(_processrun_mutex);

    return result;
}

void processrun_AddRef(processrun *run) {
    mutex_Lock(_processrun_mutex);
    run->refcount++;
    mutex_Release(_processrun_mutex);
}