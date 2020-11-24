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
#include "datetime.h"
#include "debugsymbols.h"
#include "nonlocale.h"
#include "osinfo.h"
#include "pipe.h"
#include "stack.h"
#include "threading.h"
#include "valuecontentstruct.h"
#include "vmexec.h"
#include "vmschedule.h"
#include "vmsuspendtypeenum.h"


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
    h64fprintf(stderr, "Uncaught %s: ",
        (pr->symbols ?
         _classnamelookup(pr, einfo->error_class_id) :
         "Error"));
    if (einfo->msg) {
        char *buf = malloc(einfo->msglen * 5 + 2);
        if (!buf) {
            h64fprintf(stderr, "<utf8 buf alloc failure>");
        } else {
            int64_t outlen = 0;
            int result = utf32_to_utf8(
                einfo->msg, einfo->msglen,
                buf, einfo->msglen * 5 + 1,
                &outlen, 1
            );
            if (!result) {
                h64fprintf(stderr, "<utf8 conversion failure>");
            } else {
                buf[outlen] = '\0';
                h64fprintf(stderr, "%s", buf);
            }
            free(buf);
        }
    } else {
        h64fprintf(stderr, "<no message>");
    }
    h64fprintf(stderr, "\n");
}

int vmschedule_AsyncScheduleFunc(
        h64vmexec *vmexec, h64vmthread *vmthread,
        int64_t new_func_floor, int64_t func_id,
        int parallel
        ) {
    mutex *access_mutex = (
        vmexec->worker_overview->worker_mutex
    );
    mutex_Lock(access_mutex);
    h64vmthread *newthread = vmthread_New(vmexec, !parallel);
    mutex_Release(access_mutex);
    if (!newthread) {
        return 0;
    }
    assert(
        func_id >= 0 &&
        func_id < vmexec->program->func_count
    );
    assert(
        !parallel || vmexec->program->func[func_id].is_threadable
    );
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
    mutex_Lock(access_mutex);
    #ifndef NDEBUG
    if (newthread->vmexec_owner->moptions.vmscheduler_debug)
        h64fprintf(
            stderr, "horsevm: debug: vmschedule.c: "
            "[t%p] SCHEDASYNC doing async call of func %" PRId64
            " on t%p\n",
            vmthread, func_id, newthread
        );
    #endif
    vmthread_SetSuspendState(
        newthread, SUSPENDTYPE_ASYNCCALLSCHEDULED, -1
    );
    newthread->upcoming_resume_info->func_id = func_id;
    newthread->upcoming_resume_info->run_from_start = 1;
    assert(newthread->upcoming_resume_info->func_id >= 0);
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
        if (wset->worker[i]) {
            if (wset->worker[i]->worker_thread) {
                thread_Join(wset->worker[i]->worker_thread);
                wset->worker[i]->worker_thread = NULL;
            }
            free(wset->worker[i]);
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

int vmschedule_CanThreadResume_UnguardedCheck(
        h64vmthread *vt, uint64_t now
        ) {
    if (vt->suspend_info->suspendtype !=
            SUSPENDTYPE_ASYNCCALLSCHEDULED) {
        if (vt->suspend_info->suspendtype ==
                SUSPENDTYPE_FIXEDTIME) {
            if (unlikely(vt->suspend_info->suspenditemready))
                return 1;
            if ((int64_t)now >= vt->suspend_info->suspendarg) {
                vt->suspend_info->suspenditemready = 1;
                return 1;
            }
            return 0;
        }
        return 0;
    }
    return 1;
}

int vmschedule_CanThreadResume_LockIfYes(
        h64vmthread *vt, uint64_t now
        ) {
    mutex_Lock(vt->vmexec_owner->worker_overview->worker_mutex);
    int result = vmschedule_CanThreadResume_UnguardedCheck(
        vt, now
    );
    if (result) {
        // Leave locked if result is positive.
        return 1;
    }
    mutex_Release(vt->vmexec_owner->worker_overview->worker_mutex);
    return result;
}

static int vmschedule_RunMainThreadLaunchFunc(
        h64vmworker *worker, h64vmthread *mainthread,
        funcid_t func_id, const char *debug_func_name
        ) {
    // IMPORTANT: access mutex must be already LOCKED entering this.
    // It will be LOCKED on return, too.
    h64program *pr = worker->vmexec->program;
    h64errorinfo einfo = {0};
    vmthreadsuspendinfo sinfo = {0};
    int hadsuspendevent = 0;
    int haduncaughterror = 0;
    int rval = 0;
    if (func_id >= 0) {
        mainthread->run_by_worker = worker;
        #ifndef NDEBUG
        if (worker->moptions->vmscheduler_debug)
            h64fprintf(stderr, "horsevm: debug: vmschedule.c: "
                "[w%d] RUN f%" PRId64
                " (%s func)...\n", worker->no,
                (int64_t)func_id, debug_func_name);
        #endif
        if (!vmthread_RunFunctionWithReturnInt(
                worker, mainthread,
                1,  // assume locked mutex, will return LOCKED, too
                func_id,
                &hadsuspendevent, &sinfo,
                &haduncaughterror, &einfo, &rval
                )) {
            h64fprintf(stderr, "horsevm: error: vmschedule.c: "
                " fatal error in %s, "
                "out of memory?\n", debug_func_name);
            vmthread_Free(mainthread);
            worker->vmexec->program_return_value = -1;
            return 0;
        } else if (!hadsuspendevent && !haduncaughterror) {
            worker->vmexec->program_return_value = rval;
        }
        if (haduncaughterror) {
            assert(einfo.error_class_id >= 0);
            _printuncaughterror(pr, &einfo);
            vmthread_Free(mainthread);
            worker->vmexec->program_return_value = -1;
            return 0;
        }
        if (!hadsuspendevent) {
            int result = stack_ToSize(mainthread->stack, 0, 0);
            assert(result != 0);
        }
    }
    return 1;
}

void vmschedule_WorkerRun(void *userdata) {
    h64vmworker *worker = (h64vmworker *)userdata;
    h64program *pr = worker->vmexec->program;
    mutex *access_mutex = (
        worker->vmexec->worker_overview->worker_mutex
    );

    h64vmthread *mainthread = NULL;
    while (!worker->vmexec->worker_overview->fatalerror) {
        if (worker->no == 0 && !mainthread) {
            // Main thread worker needs to know the main
            // thread:
            #ifndef NDEBUG
            if (worker->moptions->vmscheduler_debug)
                h64fprintf(
                    stderr, "horsevm: debug: vmschedule.c: "
                    "[w%d] WAIT finding main thread...\n",
                    worker->no
                );
            #endif
            mutex_Lock(access_mutex);
            int i = 0;
            while (i < worker->vmexec->thread_count) {
                if (worker->vmexec->thread[i]->is_original_main) {
                    mainthread = worker->vmexec->thread[i];
                    assert(mainthread->run_by_worker == NULL ||
                           mainthread->run_by_worker == worker);
                    mainthread->run_by_worker = worker;
                    break;
                }
                i++;
            }
            mutex_Release(access_mutex);
            if (!mainthread) {
                h64fprintf(
                    stderr, "horsevm: error: "
                    "vmschedule.c: main worker failed to find "
                    "main thread...?\n"
                );
                return;
            }
        }

        uint64_t now = datetime_Ticks();

        // Special case: allow mainthread to re-run if main not run yet.
        if (worker->no == 0 && mainthread &&
                !worker->vmexec->worker_overview->
                    workers_ran_main) {
            mutex_Lock(access_mutex);
            if (!worker->vmexec->worker_overview->
                    workers_ran_main &&
                    mainthread->suspend_info->suspendtype ==
                        SUSPENDTYPE_DONE) {
                vmthread_SetSuspendState(
                    mainthread, SUSPENDTYPE_ASYNCCALLSCHEDULED, -1
                );
                mainthread->upcoming_resume_info->func_id = -1;
                mainthread->upcoming_resume_info->run_from_start = 1;
            }
            mutex_Release(access_mutex);
        }
        // Special case: $$globalinitsimple run
        if (worker->no == 0 && mainthread &&
                !worker->vmexec->worker_overview->
                    workers_ran_globalinitsimple &&
                vmschedule_CanThreadResume_LockIfYes(mainthread, now)
                ) {
            // NOTE: access_lock is NOW LOCKED.
            int result = vmschedule_RunMainThreadLaunchFunc(
                worker, mainthread, pr->globalinitsimple_func_index,
                "$$globalinitsimple"
            );  // NOTE: assumes locked mutex, and LOCKS it again.
            if (result) {
                worker->vmexec->worker_overview->
                    workers_ran_globalinitsimple = 1;
            } else {
                worker->vmexec->worker_overview->fatalerror = 1;
                mutex_Release(access_mutex);
                return;
            }
            mutex_Release(access_mutex);
            continue;
        }
        // Special case: $$globalinit run
        if (worker->no == 0 && mainthread &&
                worker->vmexec->worker_overview->
                    workers_ran_globalinitsimple &&
                !worker->vmexec->worker_overview->
                    workers_ran_globalinit &&
                vmschedule_CanThreadResume_LockIfYes(mainthread, now)
                ) {
            // NOTE: access_lock is NOW LOCKED.
            int result = vmschedule_RunMainThreadLaunchFunc(
                worker, mainthread, pr->globalinit_func_index,
                "$$globalinit"
            );  // NOTE: assumes locked mutex, and LOCKS it again.
            if (result) {
                worker->vmexec->worker_overview->
                    workers_ran_globalinit = 1;
            } else {
                worker->vmexec->worker_overview->fatalerror = 1;
                mutex_Release(access_mutex);
                return;
            }
            mutex_Release(access_mutex);
            continue;
        }
        // Special case: main run
        if (worker->no == 0 && mainthread &&\
                worker->vmexec->worker_overview->
                    workers_ran_globalinitsimple &&
                worker->vmexec->worker_overview->
                    workers_ran_globalinit &&
                !worker->vmexec->worker_overview->
                    workers_ran_main &&
                vmschedule_CanThreadResume_LockIfYes(mainthread, now)
                ) {
            // NOTE: access_lock is NOW LOCKED.
            int result = vmschedule_RunMainThreadLaunchFunc(
                worker, mainthread, pr->main_func_index,
                "main"
            );  // NOTE: assumes locked mutex, and LOCKS it again.
            if (result) {
                worker->vmexec->worker_overview->
                    workers_ran_main = 1;
            } else {
                worker->vmexec->worker_overview->fatalerror = 1;
                mutex_Release(access_mutex);
                return;
            }
            mutex_Release(access_mutex);
            continue;
        }

        // See if we can run anything:
        #ifndef NDEBUG
        if (worker->moptions->vmscheduler_verbose_debug)
            h64fprintf(
                stderr, "horsevm: debug: vmschedule.c: "
                "[w%d] CHECK looking for work...\n",
                worker->no
            );
        #endif
        mutex_Lock(access_mutex);
        threadevent_Unset(worker->wakeupevent);
        int have_notdone_thread = 0;
        int tc = worker->vmexec->thread_count;
        int i = 0;
        while (i < tc) {
            h64vmthread *vt = worker->vmexec->thread[i];
            if (likely(vt->suspend_info->suspendtype !=
                       SUSPENDTYPE_DONE))
                have_notdone_thread = 1;
            if (worker->no != 0 && vt->is_on_main_thread) {
                i++;
                continue;
            }
            if (vmschedule_CanThreadResume_UnguardedCheck(
                    vt, now
                    )) {
                #ifndef NDEBUG
                if (worker->moptions->vmscheduler_debug)
                    h64fprintf(
                        stderr, "horsevm: debug: vmschedule.c: "
                        "[w%d] RESUME picking up vm thread %p\n",
                        worker->no, vt
                    );
                #endif
                h64program *pr = worker->vmexec->program;
                h64errorinfo einfo = {0};
                vmthreadsuspendinfo sinfo = {0};
                int hadsuspendevent = 0;
                int haduncaughterror = 0;
                int rval = 0;
                vt->run_by_worker = worker;
                if (!vmthread_RunFunctionWithReturnInt(
                        worker, vt,
                        1,  // assume locked mutex & will return LOCKED
                        -1,  // func_id = -1 since we resume
                        &hadsuspendevent, &sinfo,
                        &haduncaughterror, &einfo, &rval
                        ) || haduncaughterror) {
                    // Mutex will be locked again, here.
                    if (!haduncaughterror) {
                        h64fprintf(stderr,
                            "horsevm: error: vmschedule.c: "
                            " fatal error in function, "
                            "out of memory?\n"
                        );
                    } else {
                        assert(einfo.error_class_id >= 0);
                        _printuncaughterror(pr, &einfo);
                    }
                    vt->suspend_info->suspendtype = (
                        SUSPENDTYPE_DONE
                    );
                    vmthread_Free(vt);
                    worker->vmexec->program_return_value = -1;
                } else if (!hadsuspendevent && !haduncaughterror) {
                    worker->vmexec->program_return_value = rval;
                }
                break;
            }
            i++;
        }
        mutex_Release(access_mutex);
        // Nothing we can run -> sleep, or exit if program is done:
        if (!have_notdone_thread) {
            // We reached the end of the program.
            #ifndef NDEBUG
            if (worker->moptions->vmscheduler_debug)
                h64fprintf(
                    stderr, "horsevm: debug: vmschedule.c: "
                    "[w%d] END no jobs left, assuming program end\n",
                    worker->no
                );
            #endif
            return;
        }
        if (worker->vmexec->worker_overview->fatalerror)
            break;  // could have changed right before threadevent_Unset()
        #ifndef NDEBUG
        if (worker->moptions->vmscheduler_verbose_debug)
            h64fprintf(
                stderr, "horsevm: debug: vmschedule.c: "
                "[w%d] WAIT sleeping until wakeup event...\n",
                worker->no
            );
        #endif
        int result = threadevent_WaitUntilSet(
            worker->wakeupevent, 10000, 1
        );
        if (result) {
            #ifndef NDEBUG
            if (worker->moptions->vmscheduler_debug)
                h64fprintf(
                    stderr, "horsevm: debug: vmschedule.c: "
                    "[w%d] AWOKEN by thread event set\n",
                    worker->no
                );
            #endif
        }
    }
}

void _wakeup_threads(h64vmexec *vmexec) {
    mutex *access_mutex = vmexec->worker_overview->worker_mutex;
    mutex_Lock(access_mutex);
    int wc = vmexec->worker_overview->worker_count;
    int i = 0;
    while (i < wc) {
        threadevent_Set(vmexec->worker_overview->worker[i]->wakeupevent);
        i++;
    }
    mutex_Release(access_mutex);
}

void vmschedule_WorkerSupervisorRun(void *userdata) {
    h64vmexec *vmexec = (h64vmexec*) userdata;
}

int vmschedule_ExecuteProgram(
        h64program *pr, h64misccompileroptions *moptions
        ) {
    h64vmexec *mainexec = vmexec_New();
    if (!mainexec) {
        h64fprintf(stderr, "horsevm: error: vmschedule.c: "
            " out of memory in vmexec_New() during setup\n");
        return -1;
    }
    assert(mainexec->worker_overview != NULL);
    if (!mainexec->worker_overview->worker_mutex) {
        mainexec->worker_overview->worker_mutex = (
            mutex_Create()
        );
        if (!mainexec->worker_overview->worker_mutex) {
             h64fprintf(stderr, "horsevm: error: vmschedule.c: "
                " out of memory in mutex_Create() during setup\n");
            return -1;
        }
    }

    h64vmthread *mainthread = vmthread_New(mainexec, 0);
    if (!mainthread) {
        h64fprintf(stderr, "horsevm: error: vmschedule.c: "
            "out of memory in vmthread_New() during setup\n");
        return -1;
    }
    mainexec->program = pr;
    mainthread->is_on_main_thread = 1;
    mainthread->is_original_main = 1;
    vmthread_SetSuspendState(
        mainthread, SUSPENDTYPE_ASYNCCALLSCHEDULED, -1
    );

    assert(pr->main_func_index >= 0);
    memcpy(&mainexec->moptions, moptions, sizeof(*moptions));

    int worker_count = vmschedule_WorkerCount();
    #ifndef NDEBUG
    if (mainexec->moptions.vmscheduler_debug)
        h64fprintf(stderr, "horsevm: debug: vmschedule.c: "
            "using %d workers\n", worker_count);
    #endif
    if (mainexec->worker_overview->worker_count < worker_count) {
        h64vmworker **new_workers = realloc(
            mainexec->worker_overview->worker,
            sizeof(*new_workers) * (worker_count)
        );
        if (!new_workers) {
            h64fprintf(stderr, "horsevm: error: vmschedule.c: "
                "out of memory of new worker array "
                "alloc during setup\n");
            return -1;
        }
        mainexec->worker_overview->worker = new_workers;
        int k = mainexec->worker_overview->worker_count;
        while (k < worker_count) {
            mainexec->worker_overview->worker[k] = malloc(
                sizeof(**new_workers)
            );
            if (!mainexec->worker_overview->worker[k]) {
                h64fprintf(
                    stderr, "horsevm: error: vmschedule.c: out of memory "
                    "of worker %d/%d info during setup\n", k, worker_count
                );
                return -1;
            }
            memset(
                mainexec->worker_overview->worker[k], 0,
                sizeof(*mainexec->worker_overview->worker[k])
            );
            mainexec->worker_overview->worker[k]->vmexec = mainexec;
            mainexec->worker_overview->worker[k]->wakeupevent = (
                threadevent_Create()
            );
            if (!mainexec->worker_overview->worker[k]->wakeupevent) {
                h64fprintf(
                    stderr, "horsevm: error: vmschedule.c: out of "
                    "memory when creating worker %d/%d's threadevent "
                    "during setup\n",
                    k, worker_count
                );
                return -1;
            }
            mainexec->worker_overview->worker_count++;
            k++;
        }
    }
    // Spawn all threads other than main thread (that will be us!):
    worker_count = mainexec->worker_overview->worker_count;
    int threaderror = 0;
    int i = 0;
    while (i < worker_count) {
        mainexec->worker_overview->worker[i]->no = i;
        mainexec->worker_overview->worker[i]->moptions = moptions;
        if (i >= 1) {
            mainexec->worker_overview->worker[i]->worker_thread = (
                thread_Spawn(
                    vmschedule_WorkerRun,
                    mainexec->worker_overview->worker[i]
                )
            );
            if (!mainexec->worker_overview->worker[i]->worker_thread)
                threaderror = 1;
        }
        i++;
    }
    if (threaderror) {
        mainexec->worker_overview->fatalerror = 1;
        h64fprintf(
            stderr, "horsevm: error: vmschedule.c: out of memory "
            "spawning workers\n"
        );
    }

    // Launch supervisor thread:
    struct threadinfo *supervisor_thread = NULL;
    if (!threaderror)
        supervisor_thread = thread_Spawn(
            vmschedule_WorkerSupervisorRun, mainexec
        );
    if (!threaderror && !supervisor_thread) {
        threaderror = 1;
        h64fprintf(
            stderr, "horsevm: error: vmschedule.c: out of memory "
            "spawning supervisor\n"
        );
    }

    // If we had a thread error, then signal to stop early:
    if (threaderror) {
        mainexec->worker_overview->fatalerror = 1;
    }

    // Run main thread:
    if (!threaderror)
        vmschedule_WorkerRun(mainexec->worker_overview->worker[0]);

    // Wait until other threads are done:
    #ifndef NDEBUG
    if (moptions->vmscheduler_debug)
        h64fprintf(
            stderr, "horsevm: debug: vmschedule.c: "
            "[w0] END sending termination to all threads...\n"
        );
    #endif
    i = 1;
    while (i < worker_count) {
        if (mainexec->worker_overview->worker[i]->worker_thread) {
            threadevent_Set(
                mainexec->worker_overview->worker[i]->wakeupevent
            );
            #ifndef NDEBUG
            if (moptions->vmscheduler_debug)
                h64fprintf(
                    stderr, "horsevm: debug: vmschedule.c: "
                    "[w0] END waiting for shutdown of w%d\n", i
                );
            #endif
            thread_Join(
                mainexec->worker_overview->worker[i]->worker_thread
            );
            mainexec->worker_overview->worker[i]->worker_thread = NULL;
        }
        i++;
    }
    thread_Join(
        supervisor_thread
    );
    if (threaderror && mainexec->program_return_value == 0)
        mainexec->program_return_value = -1;
    int retval = mainexec->program_return_value;
    while (mainexec->thread_count > 0) {
        vmthread_Free(mainexec->thread[0]);
    }
    vmexec_Free(mainexec);
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
    vc->type = H64VALTYPE_SUSPENDINFO;
    vc->suspend_type = suspend_type;
    vc->suspend_intarg = suspend_intarg;
    return 0;
}
