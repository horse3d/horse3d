// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_ASYNCSYSJOB_H_
#define HORSE64_ASYNCSYSJOB_H_

#include <stdint.h>

#include "widechar.h"

typedef struct h64vmthread h64vmthread;

typedef enum h64asyncsysjobtype {
    ASYNCSYSJOB_NONE = 0,
    ASYNCSYSJOB_HOSTLOOKUP = 1,
    ASYNCSYSJOB_RUNCMD
} h64asyncsysjobtype;

typedef struct h64asyncsysjob {
    h64vmthread *request_thread;
    int type;
    volatile uint8_t done, inprogress, failed_oomorinternal;
    volatile uint8_t failed_external, abandoned;
    union {
        struct hostlookup {
            h64wchar *host;
            int64_t hostlen;
            h64wchar *resultip;
            int64_t resultiplen;
        } hostlookup;
        struct runcmd {
            h64wchar *cmd;
            int cmdlen;
            h64wchar *arg;
            int *arglen;
            int argcount;
        } runcmd;
    };
} h64asyncsysjob;

h64asyncsysjob *asyncjob_CreateEmpty();

void asyncjob_Free(h64asyncsysjob *job);

int asyncjob_RequestAsync(
    h64vmthread *request_thread,
    h64asyncsysjob *job
);

int asyncjob_IsDone(h64asyncsysjob *job);

void asyncjob_AbandonJob(
    h64asyncsysjob *job
);

int _asyncjob_GetSupervisorWaitFD();

#endif  // HORSE64_HOSTLOOKUP_H_