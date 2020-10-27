// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_VMSCHEDULE_H_
#define HORSE64_VMSCHEDULE_H_

#include <stdint.h>

typedef struct h64vmthread h64vmthread;

typedef enum suspendtype {
    SUSPENDTYPE_FIXEDTIME = 0,
    SUSPENDTYPE_WAITFORSOCKET,
    SUSPENDTYPE_WAITFORPROCESSTERMINATION,
    SUSPENDTYPE_TOTALCOUNT
} suspendtype;

typedef struct vmsuspendoverview {
    uint8_t *waittypes_currently_active;
} vmsuspendoverview;

typedef struct h64program h64program;
typedef struct h64misccompileroptions h64misccompileroptions;
typedef struct h64vmthread h64vmthread;
typedef struct h64vmexec h64vmexec;


int vmschedule_AsyncScheduleFunc(
    h64vmexec *vmexec, h64vmthread *vmthread,
    int64_t new_func_floor, int64_t func_id
);

int vmschedule_SuspendFunc(
    h64vmthread *vmthread, suspendtype suspend_type,
    int64_t suspend_intarg
);

int vmschedule_ExecuteProgram(
    h64program *pr, h64misccompileroptions *moptions
);


#endif  // HORSE64_VMSCHEDULE_H_
