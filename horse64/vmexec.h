// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_VMEXEC_H_
#define HORSE64_VMEXEC_H_

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


typedef struct h64vmfunctionframe {
    int stack_bottom;
    int func_id;
    int return_slot;
    int return_to_func_id;
    ptrdiff_t return_to_execution_offset;
} h64vmfunctionframe;

typedef struct h64vmexceptioncatchframe {
    int func_frame_no;
    int64_t catch_instruction_offset;
    int64_t finally_instruction_offset;
    int exception_obj_temporary_id;
    int triggered_catch, triggered_finally;
    h64exceptioninfo storeddelayedexception;

    int caught_types_count;
    int64_t caught_types_firstfive[5];
    int64_t *caught_types_more;
} h64vmexceptioncatchframe;


typedef struct h64vmthread {
    h64misccompileroptions moptions;
    h64program *program;
    int can_access_globals;
    int can_call_unthreadable;

    int kwarg_index_track_count;
    int64_t *kwarg_index_track_map;
    int arg_reorder_space_count;
    valuecontent *arg_reorder_space;

    h64stack *stack;
    poolalloc *heap, *str_pile;

    int funcframe_count, funcframe_alloc;
    h64vmfunctionframe *funcframe;
    int exceptionframe_count, exceptionframe_alloc;
    h64vmexceptioncatchframe *exceptionframe;

    int execution_func_id;
    int execution_instruction_id;
} h64vmthread;


static inline int VMTHREAD_FUNCSTACKBOTTOM(h64vmthread *vmthread) {
    if (vmthread->funcframe_count > 0)
        return vmthread->funcframe[vmthread->funcframe_count - 1].
            stack_bottom;
    return 0;
}

void vmthread_WipeFuncStack(h64vmthread *vmthread);

h64vmthread *vmthread_New();

int vmthread_RunFunctionWithReturnInt(
    h64vmthread *vmthread, int64_t func_id,
    int *returneduncaughtexception,
    h64exceptioninfo *einfo,
    int *out_returnint
);

void vmthread_Free(h64vmthread *vmthread);

int vmexec_ExecuteProgram(
    h64program *pr, h64misccompileroptions *moptions
);

int vmexec_ReturnFuncError(
    h64vmthread *vmthread, int64_t error_id,
    const char *msg, ...
);

#endif  // HORSE64_VMEXEC_H_
