// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_VMEXEC_H_
#define HORSE64_VMEXEC_H_

#include "compileconfig.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_STACK_FRAMES 10

#include "bytecode.h"
#include "compiler/main.h"

typedef struct h64program h64program;
typedef struct h64instruction h64instruction;
typedef struct poolalloc poolalloc;
typedef struct h64stack h64stack;
typedef struct h64refvalue h64refvalue;
typedef struct h64vmexec h64vmexec;
typedef struct h64vmworkerset h64vmworkerset;


typedef struct h64vmfunctionframe {
    int64_t stack_func_floor;
    int64_t stack_space_for_this_func;
    int64_t restore_stack_size;
    int func_id;
    int return_slot;
    int return_to_func_id;
    int rescueframe_count_on_enter;
    ptrdiff_t return_to_execution_offset;
} h64vmfunctionframe;

typedef struct h64vmrescueframe {
    int32_t func_frame_no, id;
    int64_t catch_instruction_offset;
    int64_t finally_instruction_offset;
    int error_obj_temporary_id;
    uint8_t triggered_catch, triggered_finally;
    h64errorinfo storeddelayederror;

    int caught_types_count;
    int64_t caught_types_firstfive[5];
    int64_t *caught_types_more;
} h64vmrescueframe;



typedef struct vmsuspendoverview vmsuspendoverview;
typedef struct vmthreadsuspendinfo vmthreadsuspendinfo;
typedef struct h64vmworker h64vmworker;

typedef struct h64vmthread {
    h64vmexec *vmexec_owner;
    h64vmworker *_Atomic volatile run_by_worker;
    uint8_t is_main_thread;

    int kwarg_index_track_count;
    int32_t *kwarg_index_track_map;
    int arg_reorder_space_count;
    valuecontent *arg_reorder_space;

    int64_t call_settop_reverse;
    h64stack *stack;
    poolalloc *heap, *str_pile;

    int funcframe_count, funcframe_alloc;
    h64vmfunctionframe *funcframe;
    int errorframe_count, errorframe_alloc;
    h64vmrescueframe *errorframe;

    int execution_func_id;
    int execution_instruction_id;
    vmthreadsuspendinfo *suspend_info;
} h64vmthread;

typedef struct h64vmexec {
    h64misccompileroptions moptions;
    h64program *program;

    vmsuspendoverview *suspend_overview;
    h64vmworkerset *worker_overview;

    h64vmthread **thread;
    int thread_count;
    h64vmthread *active_thread;

    int program_return_value;
} h64vmexec;

ATTR_UNUSED static inline int VMTHREAD_FUNCSTACKBOTTOM(
        h64vmthread *vmthread
        ) {
    if (vmthread->funcframe_count > 0)
        return vmthread->funcframe[vmthread->funcframe_count - 1].
            stack_func_floor;
    return 0;
}

void vmthread_WipeFuncStack(h64vmthread *vmthread);

h64vmthread *vmthread_New(h64vmexec *owner);

h64vmexec *vmexec_New();

int vmthread_RunFunctionWithReturnInt(
    h64vmworker *worker,
    h64vmthread *start_thread,
    int already_locked_in,
    int64_t func_id,
    int *returnedsuspend,
    vmthreadsuspendinfo *suspendinfo,
    int *returneduncaughterror,
    h64errorinfo *einfo,
    int *out_returnint
);

void vmthread_Free(h64vmthread *vmthread);

void vmexec_Free(h64vmexec *vmexec);

int vmexec_ReturnFuncError(
    h64vmthread *vmthread, int64_t error_id,
    const char *msg, ...
);

int vmexec_SuspendFunc(
    h64vmthread *vmthread, suspendtype suspend_type,
    int64_t suspend_intarg
);

#endif  // HORSE64_VMEXEC_H_
