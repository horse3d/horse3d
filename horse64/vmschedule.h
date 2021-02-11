// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_VMSCHEDULE_H_
#define HORSE64_VMSCHEDULE_H_

#include "compileconfig.h"

#include <stdint.h>

#include "compiler/globallimits.h"
#include "threading.h"
#include "widechar.h"


typedef struct h64vmexec h64vmexec;
typedef struct h64vmthread h64vmthread;
typedef struct h64misccompileroptions h64misccompileroptions;

#include "vmsuspendtypeenum.h"

typedef struct vminnercfuncresumeinfo vminnercfuncresumeinfo;

typedef struct vmsuspendoverview {
    uint8_t *waittypes_currently_active;
} vmsuspendoverview;

typedef struct vmthreadsuspendinfo {
    volatile suspendtype suspendtype;
    int64_t suspendarg;
    uint8_t suspenditemready;
} vmthreadsuspendinfo;

typedef struct vminnercfuncresumeinfo {
    uint8_t needs_cfunc_resume;
    int64_t target_func_id;
    int64_t old_floor, oldreverseto, new_func_floor;
} vminnercfuncresumeinfo;

typedef struct vmthreadresumeinfo {
    int64_t byteoffset;
    funcid_t func_id;
    int64_t precall_old_stack;
    int64_t precall_old_floor;
    int funcnestdepth;
    int precall_funcframesbefore;
    int precall_errorframesbefore;
    uint8_t run_from_start;
    vminnercfuncresumeinfo cfunc_resume;
} vmthreadresumeinfo;

typedef struct h64vmworker {
    int no;
    thread *worker_thread;
    h64vmthread *current_vmthread;
    h64vmexec *vmexec;
    threadevent *wakeupevent;
    h64misccompileroptions *moptions;
} h64vmworker;

typedef struct h64vmworkerset {
    h64vmworker **worker;
    int worker_count;
    mutex *worker_mutex;

    _Atomic volatile int workers_ran_globalinitsimple;
    _Atomic volatile int workers_ran_globalinit;
    _Atomic volatile int workers_ran_main;
    _Atomic volatile int fatalerror;
} h64vmworkerset;

typedef struct h64program h64program;
typedef struct h64misccompileroptions h64misccompileroptions;
typedef struct h64vmthread h64vmthread;
typedef struct h64vmexec h64vmexec;


void vmschedule_FreeWorkerSet(
    h64vmworkerset *wset
);

int vmschedule_AsyncScheduleFunc(
    h64vmexec *vmexec, h64vmthread *vmthread,
    int64_t new_func_floor, int64_t func_id,
    int parallel
);

int vmschedule_SuspendFunc(
    h64vmthread *vmthread, suspendtype suspend_type,
    int64_t suspend_intarg
);

int vmschedule_ExecuteProgram(
    h64program *pr, h64misccompileroptions *moptions,
    const h64wchar **argv, int64_t *argvlen, int argc
);

int vmschedule_CanThreadResume_UnguardedCheck(
    h64vmthread *vt, uint64_t now
);

int _vmschedule_RegisterSocketForWaiting(
    int fd, int waittypes
);

int _vmschedule_UnregisterSocketForWaiting(
    int fd, int waittypes
);

#ifndef NDEBUG
extern int _vmsockets_debug, _vmasyncjobs_debug;
#endif

#endif  // HORSE64_VMSCHEDULE_H_
