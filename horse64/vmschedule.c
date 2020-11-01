// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "bytecode.h"
#include "debugsymbols.h"
#include "osinfo.h"
#include "pipe.h"
#include "stack.h"
#include "valuecontentstruct.h"
#include "vmexec.h"
#include "vmschedule.h"


static char _unexpectedlookupfail[] = "<unexpected lookup fail>";

static const char *_classnamelookup(h64program *pr, int64_t classid) {
    h64classsymbol *csymbol = h64debugsymbols_GetClassSymbolById(
        pr->symbols, classid
    );
    if (!csymbol)
        return _unexpectedlookupfail;
    return csymbol->name;
}

static void _printuncaughterror(
        h64program *pr, h64errorinfo *einfo
        ) {
    fprintf(stderr, "Uncaught %s: ",
        (pr->symbols ?
         _classnamelookup(pr, einfo->error_class_id) :
         "Error"));
    if (einfo->msg) {
        char *buf = malloc(einfo->msglen * 5 + 2);
        if (!buf) {
            fprintf(stderr, "<utf8 buf alloc failure>");
        } else {
            int64_t outlen = 0;
            int result = utf32_to_utf8(
                einfo->msg, einfo->msglen,
                buf, einfo->msglen * 5 + 1,
                &outlen, 1
            );
            if (!result) {
                fprintf(stderr, "<utf8 conversion failure>");
            } else {
                buf[outlen] = '\0';
                fprintf(stderr, "%s", buf);
            }
            free(buf);
        }
    } else {
        fprintf(stderr, "<no message>");
    }
    fprintf(stderr, "\n");
}

int vmschedule_AsyncScheduleFunc(
        h64vmexec *vmexec, h64vmthread *vmthread,
        int64_t new_func_floor, int64_t func_id
        ) {
    mutex *access_mutex = (
        vmexec->worker_overview->worker_mutex
    );
    mutex_Lock(access_mutex);
    h64vmthread *newthread = vmthread_New(vmexec);
    mutex_Release(access_mutex);
    if (!newthread) {
        return 0;
    }
    int64_t func_slots = STACK_TOTALSIZE(vmthread->stack) - new_func_floor;
    if (!stack_ToSize(
            newthread->stack, func_slots, 0)) {
        mutex_Lock(access_mutex);
        vmthread_Free(newthread);
        mutex_Release(access_mutex);
        return 0;
    }

    // Pipe through the stack contents:
    h64gcvalue _stackbuf[64];
    h64gcvalue *object_instances_transferlist = _stackbuf;
    int object_instances_transferlist_count = 0;
    int object_instances_transferlist_alloc = 64;
    int object_instances_transferlist_onheap = 0;
    int64_t i = 0;
    while (i < func_slots) {
        int result = _pipe_DoPipeObject(
            vmthread, newthread,
            i + new_func_floor, i,
            &object_instances_transferlist,
            &object_instances_transferlist_count,
            &object_instances_transferlist_alloc,
            &object_instances_transferlist_onheap
        );
        if (!result) {
            if (object_instances_transferlist_onheap)
                free(object_instances_transferlist);
            mutex_Lock(access_mutex);
            vmthread_Free(newthread);
            mutex_Release(access_mutex);
            return 0;
        }
        i++;
    }

    // Set up calls to .on_piped() later, for all piped object instances:
    assert(
        object_instances_transferlist_count == 0
    ); // ^ FIXME, implement this

    // Set suspend state to be resumed once we get to run this:
    vmthread->suspend_info->suspendtype = (
        SUSPENDTYPE_ASYNCCALLSCHEDULED
    );
    vmthread->suspend_info->suspendarg = (
        func_id
    );
    mutex_Lock(access_mutex);
    vmexec->suspend_overview->waittypes_currently_active[
        SUSPENDTYPE_ASYNCCALLSCHEDULED
    ]++;
    mutex_Release(access_mutex);
    return 1;
}

void vmschedule_FreeWorkerSet(
        h64vmworkerset *wset
        ) {
    if (!wset)
        return;

    int i = 0;
    while (i < wset->worker_count) {
        if (wset->worker[i] && wset->worker[i]->worker_thread) {
            thread_Join(wset->worker[i]->worker_thread);
            wset->worker[i]->worker_thread = NULL;
        }
        i++;
    }
    free(wset->worker);
    if (wset->worker_mutex) {
        mutex_Destroy(wset->worker_mutex);
        wset->worker_mutex = NULL;
    }

    free(wset);
}

int vmschedule_WorkerCount() {
    int thread_count = osinfo_CpuThreads();
    if (thread_count < 4)
        return 4;
    return thread_count;
}

void vmschedule_WorkerRun(void *userdata) {
    h64vmworker *worker = (h64vmworker *)userdata;

    if (worker->no == 0) {
        #ifndef NDEBUG
        if (worker->moptions->vmscheduler_debug)
            fprintf(stderr, "horsevm: debug: vmschedule.c: "
                "[w%d] WAIT finding main thread...\n", worker->no);
        #endif
        // Main thread worker. Find the main thread:
        h64vmthread *mainthread = NULL;
        mutex *access_mutex = (
            worker->vmexec->worker_overview->worker_mutex
        );
        mutex_Lock(access_mutex);
        h64program *pr = worker->vmexec->program;
        int i = 0;
        while (i < worker->vmexec->thread_count) {
            if (worker->vmexec->thread[i]->is_main_thread) {
                mainthread = worker->vmexec->thread[i];
                assert(mainthread->run_by_worker == NULL);
                mainthread->run_by_worker = worker;
                break;
            }
            i++;
        }
        mutex_Release(access_mutex);
        if (!mainthread) {
            fprintf(
                stderr, "horsevm: error: "
                "vmschedule.c: main worker failed to find "
                "main thread...?\n"
            );
            return;
        }
        // Run the global var init and main func on this thread:
        h64errorinfo einfo = {0};
        int haduncaughterror = 0;
        int rval = 0;
        if (pr->globalinit_func_index >= 0) {
            #ifndef NDEBUG
            if (worker->moptions->vmscheduler_debug)
                fprintf(stderr, "horsevm: debug: vmschedule.c: "
                    "[w%d] RUN f%" PRId64
                    " ($$globalinit func)...\n", worker->no,
                    (int64_t)pr->globalinit_func_index);
            #endif
            if (!vmthread_RunFunctionWithReturnInt(
                    worker->vmexec, mainthread, pr->globalinit_func_index,
                    &haduncaughterror, &einfo, &rval
                    )) {
                fprintf(stderr, "horsevm: error: vmschedule.c: "
                    " fatal error in $$globalinit, "
                    "out of memory?\n");
                mutex_Lock(access_mutex);
                vmthread_Free(mainthread);
                worker->vmexec->program_return_value = -1;
                mutex_Release(access_mutex);
                return;
            }
            if (haduncaughterror) {
                assert(einfo.error_class_id >= 0);
                mutex_Lock(access_mutex);
                _printuncaughterror(pr, &einfo);
                vmthread_Free(mainthread);
                worker->vmexec->program_return_value = -1;
                mutex_Release(access_mutex);
                return;
            }
            int result = stack_ToSize(mainthread->stack, 0, 0);
            assert(result != 0);
        }
        #ifndef NDEBUG
        if (worker->moptions->vmscheduler_debug)
            fprintf(stderr, "horsevm: debug: vmschedule.c: "
                "[w%d] RUN f%" PRId64 " (main func)...\n",
                worker->no, (int64_t)pr->main_func_index);
        #endif
        haduncaughterror = 0;
        rval = 0;
        if (!vmthread_RunFunctionWithReturnInt(
                worker->vmexec, mainthread, pr->main_func_index,
                &haduncaughterror, &einfo, &rval
                )) {
            fprintf(stderr, "horsevm: error: vmschedule.c: "
                "fatal error in main, "
                "out of memory?\n");
            mutex_Lock(access_mutex);
            vmthread_Free(mainthread);
            worker->vmexec->program_return_value = -1;
            mutex_Release(access_mutex);
            return;
        }
        if (haduncaughterror) {
            assert(einfo.error_class_id >= 0);
            mutex_Lock(access_mutex);
            _printuncaughterror(pr, &einfo);
            vmthread_Free(mainthread);
            worker->vmexec->program_return_value = -1;
            mutex_Release(access_mutex);
            return;
        }
        mutex_Lock(access_mutex);
        vmthread_Free(mainthread);
        worker->vmexec->program_return_value = rval;
        mutex_Release(access_mutex);
        return;
    }
}

int vmschedule_ExecuteProgram(
        h64program *pr, h64misccompileroptions *moptions
        ) {
    h64vmexec *mainexec = vmexec_New();
    if (!mainexec) {
        fprintf(stderr, "horsevm: error: vmschedule.c: "
            " out of memory during setup\n");
        return -1;
    }
    assert(mainexec->worker_overview != NULL);
    if (!mainexec->worker_overview->worker_mutex) {
        mainexec->worker_overview->worker_mutex = (
            mutex_Create()
        );
        if (!mainexec->worker_overview->worker_mutex) {
             fprintf(stderr, "horsevm: error: vmschedule.c: "
                " out of memory during setup\n");
            return -1;
        }
    }

    h64vmthread *mainthread = vmthread_New(mainexec);
    if (!mainthread) {
        fprintf(stderr, "horsevm: error: vmschedule.c: "
            "out of memory during setup\n");
        return -1;
    }
    mainexec->program = pr;
    mainthread->is_main_thread = 1;

    assert(pr->main_func_index >= 0);
    memcpy(&mainexec->moptions, moptions, sizeof(*moptions));

    int worker_count = vmschedule_WorkerCount();
    if (mainexec->worker_overview->worker_count < worker_count) {
        h64vmworker **new_workers = realloc(
            mainexec->worker_overview->worker,
            sizeof(*new_workers) * (worker_count)
        );
        if (!new_workers) {
            fprintf(stderr, "horsevm: error: vmschedule.c: "
                "out of memory during setup\n");
            return -1;
        }
        mainexec->worker_overview->worker = new_workers;
        int k = mainexec->worker_overview->worker_count;
        while (k < worker_count) {
            mainexec->worker_overview->worker[k] = malloc(
                sizeof(**new_workers)
            );
            if (!mainexec->worker_overview->worker[k]) {
                fprintf(
                    stderr, "horsevm: error: vmschedule.c: out of memory "
                    "during setup\n"
                );
                return -1;
            }
            memset(
                mainexec->worker_overview->worker[k], 0,
                sizeof(*mainexec->worker_overview->worker[k])
            );
            mainexec->worker_overview->worker[k]->vmexec = mainexec;
            k++;
        }
    }
    // Spawn all threads:
    int threaderror = 0;
    int i = 0;
    while (i < worker_count) {
        mainexec->worker_overview->worker[i]->no = i;
        mainexec->worker_overview->worker[i]->moptions = moptions;
        mainexec->worker_overview->worker[i]->worker_thread = (
            thread_Spawn(
                vmschedule_WorkerRun,
                mainexec->worker_overview->worker[i]
            )
        );
        if (!mainexec->worker_overview->worker[i]->worker_thread)
            threaderror = 1;
        i++;
    }
    if (threaderror) {
        fprintf(
            stderr, "horsevm: error: vmschedule.c: out of memory "
            "spawning workers\n"
        );
    }
    // Wait until we're done:
    i = 0;
    while (i < worker_count) {
        if (mainexec->worker_overview->worker[i]->worker_thread) {
            thread_Join(
                mainexec->worker_overview->worker[i]->worker_thread
            );
            mainexec->worker_overview->worker[i]->worker_thread = NULL;
        }
        i++;
    }
    if (threaderror && mainexec->program_return_value == 0)
        mainexec->program_return_value = -1;
    int retval = mainexec->program_return_value;
    vmexec_Free(mainexec);
    while (mainexec->thread_count > 0) {
        vmthread_Free(mainexec->thread[0]);
    }
    return retval;
}

int vmschedule_SuspendFunc(
        h64vmthread *vmthread, suspendtype suspend_type,
        int64_t suspend_intarg
        ) {
    if (STACK_TOP(vmthread->stack) == 0) {
        if (!stack_ToSize(
                vmthread->stack,
                STACK_TOTALSIZE(vmthread->stack) + 1, 1
                ))
            return 0;
    }
    valuecontent *vc = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vc);
    valuecontent_Free(vc);
    memset(vc, 0, sizeof(*vc));
    vc->type = H64VALTYPE_THREADSUSPENDINFO;
    vc->suspend_type = suspend_type;
    vc->suspend_intarg = suspend_intarg;
    return 0;
}