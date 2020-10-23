// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "poolalloc.h"
#include "processrun.h"
#include "threading.h"


typedef int64_t h64pid_t;

typedef struct processrun {
    h64wchar *path; int64_t path_len;
    int arg_count; h64wchar **arg_s; int64_t *arg_len;
    h64pid_t pid;
 } processrun;

mutex *_processrun_mutex = NULL;
__attribute__((constructor)) static void _processrun_CreateMutex() {
    _processrun_mutex = mutex_Create();
    if (!_processrun_mutex) {
        fprintf(stderr, "horsevm: error: OOM creeating process mutex\n");
        exit(1);
    }
}

poolalloc *processrun_struct_alloc = NULL;

void _processrun_Free(processrun *pr) {
    if (!pr)
        return;
    poolalloc_free(processrun_struct_alloc, pr);
}

processrun *processrun_Launch(
        const h64wchar *path, int64_t path_len,
        int arg_count, const h64wchar **arg_s, const int64_t *arg_len
        ) {
    assert(_processrun_mutex != NULL);
    int mutexheld = 1;

    // Get mutex, and ensure the pool allocator exists:
    mutex_Lock(_processrun_mutex);
    processrun *result = NULL;
    if (!processrun_struct_alloc) {
        processrun_struct_alloc = poolalloc_New(sizeof(processrun));
        if (!processrun_struct_alloc) {
            oom:
            // Out of memory exit branch.
            if (!mutexheld)
                mutex_Lock(_processrun_mutex);
            if (result)
                _processrun_Free(result);
            mutex_Release(_processrun_mutex);
            return NULL;
        }
    }

    // Make struct and copy all arguments to it:
    result = poolalloc_malloc(processrun_struct_alloc, 0);
    if (!result)
        goto oom;
    mutexheld = 0;
    mutex_Release(_processrun_mutex);
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
        memcpy(result->arg_s[i], arg_s[i], arg_len[i] * sizeof(h64wchar));
        i++;
    }



    return result;
}