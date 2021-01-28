// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#endif

#include "asyncsysjob.h"
#include "nonlocale.h"
#include "poolalloc.h"
#include "processrun.h"
#include "sockets.h"
#include "threading.h"
#include "vmexec.h"
#include "widechar.h"


mutex *asyncsysjob_schedule_lock = NULL;
poolalloc *asyncsysjob_allocator = NULL;


static h64asyncsysjob **jobs = NULL;
static int jobs_alloc = 0;
static int jobs_count = 0;
static threadevent *job_done_supervisor_waitevent = NULL;

static thread **async_worker = NULL;
static threadevent **async_worker_event = NULL;
static int async_worker_count = 0;

static void __attribute__((constructor)) _asyncsysjob_InitLock() {
    assert(asyncsysjob_schedule_lock == NULL);
    asyncsysjob_schedule_lock = mutex_Create();
    if (!asyncsysjob_schedule_lock) {
        h64fprintf(stderr, "horsevm: error: failed to "
            "allocate asyncsysjob_schedule_lock\n");
        _exit(1);
    }
}

h64asyncsysjob *asyncjob_CreateEmpty() {
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
    if (!result)
        return NULL;
    memset(result, 0, sizeof(*result));
    return result;
}

void _asyncjob_FreeNoLock(h64asyncsysjob *job) {
    if (!job)
        return;
    if (job->type == ASYNCSYSJOB_HOSTLOOKUP) {
        free(job->hostlookup.host);
        job->hostlookup.host = NULL;
        free(job->hostlookup.resultip4);
        job->hostlookup.resultip4 = NULL;
        free(job->hostlookup.resultip6);
        job->hostlookup.resultip6 = NULL;
    } else if (job->type == ASYNCSYSJOB_RUNCMD) {
        if (job->runcmd.arg) {
            int i = 0;
            while (i < job->runcmd.argcount) {
                free(job->runcmd.arg[i]);
                i++;
            }
            free(job->runcmd.arg);
            job->runcmd.arg = NULL;
            job->runcmd.argcount = 0;
        }
        if (job->runcmd.cmd) {
            free(job->runcmd.cmd);
            job->runcmd.cmd = NULL;
        }
    }
    poolalloc_free(asyncsysjob_allocator, job);
}

void asyncjob_Free(h64asyncsysjob *job) {
    if (!job)
        return;
    mutex_Lock(asyncsysjob_schedule_lock);
    _asyncjob_FreeNoLock(job);
    mutex_Release(asyncsysjob_schedule_lock);
}

int _asyncjob_GetSupervisorWaitFD() {
    mutex_Lock(asyncsysjob_schedule_lock);
    if (!job_done_supervisor_waitevent) {
        job_done_supervisor_waitevent = (
            threadevent_Create()
        );
        if (!job_done_supervisor_waitevent) {
            mutex_Release(asyncsysjob_schedule_lock);
            return -1;
        }
    }
    h64socket *sock = threadevent_WaitForSocket(
        job_done_supervisor_waitevent
    );
    if (!sock) {
        mutex_Release(asyncsysjob_schedule_lock);
        return -1;
    }
    int fd = sock->fd;
    mutex_Release(asyncsysjob_schedule_lock);
    return fd;
}

void asyncsysjobworker_Do(void *userdata) {
    int worker_id = (uintptr_t)userdata;
    while (1) {
        mutex_Lock(asyncsysjob_schedule_lock);
        h64asyncsysjob *ourjob = NULL;
        int i = 0;
        while (i < jobs_count) {
            if (!jobs[i]->inprogress &&
                    (!jobs[i]->done || jobs[i]->abandoned)) {
                if (jobs[i]->abandoned) {
                    _asyncjob_FreeNoLock(jobs[i]);
                    jobs[i] = NULL;
                    if (i + 1 < jobs_count)
                        memcpy(
                            &jobs[i], &jobs[i + 1],
                            sizeof(*jobs) * (jobs_count - i - 1)
                        );
                    jobs_count--;
                    continue;
                }
                jobs[i]->inprogress = 1;
                #ifndef NDEBUG
                if (_vmasyncjobs_debug)
                    h64fprintf(stderr, "horsevm: debug: "
                        "picking up job ptr=%p type=%d\n",
                        jobs[i], (int)jobs[i]->type);
                #endif
                ourjob = jobs[i];
                break;
            }
            i++;
        }
        mutex_Release(asyncsysjob_schedule_lock);
        if (ourjob != NULL &&
                ourjob->type == ASYNCSYSJOB_HOSTLOOKUP) {
            #ifndef NDEBUG
            if (_vmasyncjobs_debug)
                h64fprintf(stderr, "horsevm: debug: "
                    "processing host lookup (job ptr=%p)\n",
                    ourjob);
            #endif
            int hostutf8size = 0;
            char *hostutf8 = NULL;
            int64_t hostutf8len = 0;

            // Special case: it's already an ip:
            if (sockets_IsIPv4(
                    ourjob->hostlookup.host, ourjob->hostlookup.hostlen
                    )) {
                ourjob->hostlookup.resultip4 = malloc(
                    ourjob->hostlookup.hostlen * sizeof(h64wchar)
                );
                if (!ourjob->hostlookup.resultip4)
                    goto lookupoom;
                memcpy(
                    ourjob->hostlookup.resultip4, ourjob->hostlookup.host,
                    ourjob->hostlookup.hostlen * sizeof(h64wchar)
                );
                ourjob->hostlookup.resultip4len = (
                    ourjob->hostlookup.hostlen
                );
                mutex_Lock(asyncsysjob_schedule_lock);
                ourjob->inprogress = 0;
                ourjob->done = 1;
                mutex_Release(asyncsysjob_schedule_lock);
                threadevent_Set(job_done_supervisor_waitevent);
                #ifndef NDEBUG
                if (_vmasyncjobs_debug)
                    h64fprintf(stderr, "horsevm: debug: "
                        "host lookup OK, ptr=%p -> host is already ip\n",
                        ourjob);
                #endif
                continue;
            } else if (sockets_IsIPv6(
                    ourjob->hostlookup.host, ourjob->hostlookup.hostlen
                    )) {
                ourjob->hostlookup.resultip6 = malloc(
                    ourjob->hostlookup.hostlen * sizeof(h64wchar)
                );
                if (!ourjob->hostlookup.resultip6)
                    goto lookupoom;
                memcpy(
                    ourjob->hostlookup.resultip6, ourjob->hostlookup.host,
                    ourjob->hostlookup.hostlen * sizeof(h64wchar)
                );
                ourjob->hostlookup.resultip6len = (
                    ourjob->hostlookup.hostlen
                );
                mutex_Lock(asyncsysjob_schedule_lock);
                ourjob->inprogress = 0;
                ourjob->done = 1;
                mutex_Release(asyncsysjob_schedule_lock);
                threadevent_Set(job_done_supervisor_waitevent);
                #ifndef NDEBUG
                if (_vmasyncjobs_debug)
                    h64fprintf(stderr, "horsevm: debug: "
                        "host lookup OK, ptr=%p -> host is already ip\n",
                        ourjob);
                #endif
                continue;
            }
            // Special case: obviously invalid host:
            int dots = 0;
            int i = 0;
            while (i < ourjob->hostlookup.hostlen) {
                if (ourjob->hostlookup.host[i] == '.') {
                    if (i == 0 || i == ourjob->hostlookup.hostlen - 1)
                        goto invalidhost;
                    dots++;
                } else if (ourjob->hostlookup.host[i] == '\0') {
                    goto invalidhost;
                } else if (ourjob->hostlookup.host[i] <= 127 &&
                        (ourjob->hostlookup.host[i] < 'a' ||
                        ourjob->hostlookup.host[i] > 'z') &&
                        (ourjob->hostlookup.host[i] < 'A' ||
                        ourjob->hostlookup.host[i] > 'Z') &&
                        (ourjob->hostlookup.host[i] < '0' ||
                        ourjob->hostlookup.host[i] > '9') &&
                        ourjob->hostlookup.host[i] != '-') {
                    goto invalidhost;
                }
                i++;
            }
            if (ourjob->hostlookup.hostlen == 0 || dots <= 0) {
                invalidhost:
                if (hostutf8)
                    free(hostutf8);
                mutex_Lock(asyncsysjob_schedule_lock);
                ourjob->failed_external = 1;
                ourjob->inprogress = 0;
                ourjob->done = 1;
                mutex_Release(asyncsysjob_schedule_lock);
                threadevent_Set(job_done_supervisor_waitevent);
                #ifndef NDEBUG
                if (_vmasyncjobs_debug)
                    h64fprintf(stderr, "horsevm: debug: "
                        "host lookup FAIL, ptr=%p -> "
                        "host has invalid chars\n",
                        ourjob);
                #endif
                continue;
            }
            // Convert to utf-8 for system APIs:
            hostutf8size = ourjob->hostlookup.hostlen * 5 + 2;
            hostutf8 = malloc(hostutf8size);
            hostutf8len = 0;
            int result = utf32_to_utf8(
                ourjob->hostlookup.host,
                ourjob->hostlookup.hostlen,
                hostutf8, hostutf8size,
                &hostutf8len, 1, 0
            );
            if (!result) {
                lookupoom:
                if (hostutf8)
                    free(hostutf8);
                mutex_Lock(asyncsysjob_schedule_lock);
                ourjob->failed_oomorinternal = 1;
                ourjob->inprogress = 0;
                ourjob->done = 1;
                mutex_Release(asyncsysjob_schedule_lock);
                threadevent_Set(job_done_supervisor_waitevent);
                #ifndef NDEBUG
                if (_vmasyncjobs_debug)
                    h64fprintf(stderr, "horsevm: debug: "
                        "host lookup FAIL, ptr=%p -> "
                        "out of memory / unspecified internal error\n",
                        ourjob);
                #endif
                continue;
            }

            // IPv4:
            struct addrinfo* lookupresult = NULL;
            struct addrinfo hints = {0};
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_family = AF_INET;
            result = getaddrinfo(hostutf8, NULL, &hints, &lookupresult);
            if (result == 0) {
                char ipresult[NI_MAXHOST] = "";
                struct addrinfo* resultiter = lookupresult;
                while (resultiter != NULL) {
                    result = getnameinfo(
                        resultiter->ai_addr,
                        resultiter->ai_addrlen,
                        ipresult, NI_MAXHOST, NULL, 0,
                        NI_NUMERICHOST
                    );
                    if (result != 0) {
                        freeaddrinfo(lookupresult);
                        break;
                    }
                    if (strlen(ipresult) > 0) {
                        break;
                    }
                    resultiter = resultiter->ai_next;
                }
                freeaddrinfo(lookupresult);
                if (strlen(ipresult) > 0) {
                    int wasinvalid = 0;
                    int wasoom = 0;
                    ourjob->hostlookup.resultip4 = utf8_to_utf32_ex(
                        ipresult, strlen(ipresult),
                        NULL, 0, NULL, NULL,
                        &ourjob->hostlookup.resultip4len,
                        0, 1, &wasinvalid, &wasoom
                    );
                    if (!ourjob->hostlookup.resultip4) {
                        if (wasoom)
                            goto lookupoom;
                    }
                }
            }
            // IPv6:
            lookupresult = NULL;
            hints.ai_family = AF_INET6;
            result = getaddrinfo(hostutf8, NULL, &hints, &lookupresult);
            if (result == 0) {
                char ipresult[NI_MAXHOST] = "";
                struct addrinfo* resultiter = lookupresult;
                while (resultiter != NULL) {
                    result = getnameinfo(
                        resultiter->ai_addr,
                        resultiter->ai_addrlen,
                        ipresult, NI_MAXHOST, NULL, 0,
                        NI_NUMERICHOST
                    );
                    if (result != 0) {
                        freeaddrinfo(lookupresult);
                        break;
                    }
                    if (strlen(ipresult) > 0) {
                        break;
                    }
                    resultiter = resultiter->ai_next;
                }
                freeaddrinfo(lookupresult);
                if (strlen(ipresult) > 0) {
                    int wasinvalid = 0;
                    int wasoom = 0;
                    ourjob->hostlookup.resultip6 = utf8_to_utf32_ex(
                        ipresult, strlen(ipresult),
                        NULL, 0, NULL, NULL,
                        &ourjob->hostlookup.resultip6len,
                        0, 1, &wasinvalid, &wasoom
                    );
                    if (!ourjob->hostlookup.resultip6) {
                        if (wasoom)
                            goto lookupoom;
                    }
                }
            }
            free(hostutf8);
            hostutf8 = NULL;
            // Bail out on failure (=> neither ipv4 nor ipv6 resolved):
            if (ourjob->hostlookup.resultip4len == 0 &&
                    ourjob->hostlookup.resultip6len == 0) {
                mutex_Lock(asyncsysjob_schedule_lock);
                ourjob->failed_oomorinternal = 0;
                ourjob->failed_external = 1;
                ourjob->inprogress = 0;
                ourjob->done = 1;
                mutex_Release(asyncsysjob_schedule_lock);
                threadevent_Set(job_done_supervisor_waitevent);
                #ifndef NDEBUG
                if (_vmasyncjobs_debug)
                    h64fprintf(stderr, "horsevm: debug: "
                        "host lookup FAIL, ptr=%p -> no such domain\n",
                        ourjob);
                #endif
                continue;
            }
            // Mark done on success:
            mutex_Lock(asyncsysjob_schedule_lock);
            ourjob->inprogress = 0;
            ourjob->done = 1;
            mutex_Release(asyncsysjob_schedule_lock);
            threadevent_Set(job_done_supervisor_waitevent);
            #ifndef NDEBUG
            if (_vmasyncjobs_debug)
                h64fprintf(stderr, "horsevm: debug: "
                    "host lookup OK, ptr=%p -> success\n",
                    ourjob);
            #endif
            continue;
        } else if (ourjob != NULL &&
                ourjob->type == ASYNCSYSJOB_RUNCMD) {
            #ifndef NDEBUG
            if (_vmasyncjobs_debug)
                h64fprintf(stderr, "horsevm: debug: "
                    "run cmd launch... (job ptr=%p)\n",
                    ourjob);
            #endif
            assert(0);
        }
        threadevent_WaitUntilSet(
            async_worker_event[worker_id],
            5000, 1
        );
    }
}

void asyncjob_TriggerSupervisorWakeupEvent() {
    threadevent_Set(job_done_supervisor_waitevent);
}

void asyncjob_FlushSupervisorWakeupEvents() {
    threadevent_FlushWakeUpEvents(
        job_done_supervisor_waitevent
    );
}

int asyncjob_IsDone(h64asyncsysjob *job) {
    mutex_Lock(asyncsysjob_schedule_lock);
    int result = (job->done != 0);
    mutex_Release(asyncsysjob_schedule_lock);
    return result;
}

int asyncjob_RequestAsync(
        h64vmthread *request_thread,
        h64asyncsysjob *job
        ) {
    if (!request_thread || !job) {
        return 0;
    }
    assert(job->request_thread == NULL ||
           job->request_thread == request_thread);
    job->request_thread = request_thread;
    mutex_Lock(asyncsysjob_schedule_lock);
    if (jobs_count + 1 > jobs_alloc) {
        int newc = (jobs_count + 64);
        if (newc < jobs_alloc * 2)
            newc = jobs_alloc * 2;
        h64asyncsysjob **new_jobs = realloc(
            jobs, sizeof(*new_jobs) * newc
        );
        if (!new_jobs) {
            mutex_Release(asyncsysjob_schedule_lock);
            return 0;
        }
        jobs = new_jobs;
        jobs_alloc = newc;
    }
    jobs[jobs_count] = job;
    jobs_count++;
    if (async_worker_count < ASYNCSYSJOB_WORKER_COUNT) {
        if (!async_worker) {
            async_worker = malloc(
                sizeof(*async_worker) * ASYNCSYSJOB_WORKER_COUNT
            );
            if (!async_worker)
                goto abortduetospawn;
            memset(
                async_worker, 0,
                sizeof(*async_worker) * ASYNCSYSJOB_WORKER_COUNT
            );
        }
        if (!async_worker_event) {
            async_worker_event = malloc(
                sizeof(*async_worker_event) * ASYNCSYSJOB_WORKER_COUNT
            );
            if (!async_worker_event)
                goto abortduetospawn;
            memset(
                async_worker_event, 0,
                sizeof(*async_worker_event) * ASYNCSYSJOB_WORKER_COUNT
            );
        }
        int i = 0;
        while (i < ASYNCSYSJOB_WORKER_COUNT) {
            if (!async_worker_event[i]) {
                async_worker_event[i] = (
                    threadevent_Create()
                );
                if (!async_worker_event[i]) {
                    if (async_worker_count <= 0)
                        goto abortduetospawn;
                }
            }
            i++;
        }
        while (async_worker_count < ASYNCSYSJOB_WORKER_COUNT) {
            if (!async_worker_event[async_worker_count])
                break;
            async_worker[async_worker_count] = (
                thread_SpawnWithPriority(
                    THREAD_PRIO_LOW, asyncsysjobworker_Do,
                    (void*)(uintptr_t)async_worker_count
                )
            );
            if (!async_worker[async_worker_count]) {
                if (async_worker_count <= 0) {
                    // Didn't manage to spawn even one worker, abort.
                    abortduetospawn:
                    jobs_count--;
                    jobs[jobs_count] = NULL;  // disown it again
                    mutex_Release(asyncsysjob_schedule_lock);
                    return 0;
                }
                // Got one worker running, so ignore for now.
            }
            async_worker_count++;
        }
    } else {
        int i = 0;
        while (i < ASYNCSYSJOB_WORKER_COUNT) {
            if (async_worker_event[i]) {
                threadevent_Set(async_worker_event[i]);
            }
            i++;
        }
    }
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