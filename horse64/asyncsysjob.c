// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "asyncsysjob.h"
#include "nonlocale.h"
#include "poolalloc.h"
#include "processrun.h"
#include "threading.h"
#include "vmexec.h"


mutex *asyncsysjob_schedule_lock = NULL;
poolalloc *asyncsysjob_allocator = NULL;


static h64asyncsysjob **jobs = NULL;
static int jobs_alloc = 0;
static int jobs_count = 0;

static void __attribute__((constructor)) _asyncsysjob_InitLock() {
    assert(asyncsysjob_schedule_lock == NULL);
    asyncsysjob_schedule_lock = mutex_Create();
    if (!asyncsysjob_schedule_lock) {
        h64fprintf(stderr, "horsevm: error: failed to "
            "allocate asyncsysjob_schedule_lock\n");
        _exit(1);
    }
}

h64asyncsysjob *asyncjob_Alloc() {
    mutex_Lock(asyncsysjob_schedule_lock);
    if (!asyncsysjob_allocator) {
        asyncsysjob_allocator = poolalloc_New(sizeof(h64asyncsysjob));
        if (!asyncsysjob_allocator) {
            mutex_Release(asyncsysjob_schedule_lock);
            return NULL;
        }
    }
    h64asyncsysjob *result = poolalloc_malloc(
        asyncsysjob_allocator, 0
    );
    mutex_Release(asyncsysjob_schedule_lock);
    return result;
}

void asyncjob_Free(h64asyncsysjob *job) {
    if (!job)
        return;
    mutex_Lock(asyncsysjob_schedule_lock);
    poolalloc_free(asyncsysjob_allocator, job);
    mutex_Release(asyncsysjob_schedule_lock);
}

int asyncjob_RequestAsync(
        h64vmthread *request_thread,
        h64asyncsysjob *job,
        int *failurewasoom
        ) {
    if (!request_thread || !job) {
        if (failurewasoom) *failurewasoom = 1;
        return 0;
    }
    assert(job->request_thread == NULL ||
           job->request_thread == request_thread);
    job->request_thread = request_thread;
    mutex_Lock(asyncsysjob_schedule_lock);

    mutex_Release(asyncsysjob_schedule_lock);
    return 1;
}

void asyncjob_AbandonJob(
        h64asyncsysjob *job
        ) {
    mutex_Lock(asyncsysjob_schedule_lock);
    job->abandoned = 1;
    mutex_Release(asyncsysjob_schedule_lock);
}