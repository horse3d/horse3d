#ifndef HORSE64_VMEXEC_H_
#define HORSE64_VMEXEC_H_

#include <stddef.h>
#include <stdint.h>

#define MAX_STACK_FRAMES 10

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

typedef struct h64vmerrorcatchframe {
    int function_frame_no;
    int catch_instruction_offset;
    int finally_instruction_offset;
    int error_obj_temporary_id;
    int in_catch, in_finally;
} h64vmerrorcatchframe;


typedef struct h64vmthread {
    h64misccompileroptions moptions;
    h64program *program;
    int can_access_globals;
    int can_call_unthreadable;

    h64stack *stack;
    poolalloc *heap, *str_pile;

    int funcframe_count, funcframe_alloc;
    h64vmfunctionframe *funcframe;
    int errorcatchframe_count;
    h64vmerrorcatchframe *errorframe;
    h64refvalue *caught_error;

    int execution_func_id;
    int execution_instruction_id;
} h64vmthread;

typedef struct h64exceptioninfo {
    int stack_frame_count;
    int stack_frame_funcid[MAX_STACK_FRAMES];
    int64_t stack_frame_byteoffset[MAX_STACK_FRAMES];

    int exception_class_id;
    char *msg;
} h64exceptioninfo;


static inline int VMTHREAD_FUNCSTACKBOTTOM(h64vmthread *vmthread) {
    if (vmthread->funcframe_count > 0)
        return vmthread->funcframe[vmthread->funcframe_count - 1].
            stack_bottom;
    return 0;
}

void vmthread_WipeFuncStack(h64vmthread *vmthread);

h64vmthread *vmthread_New();

int vmthread_RunFunctionWithReturnInt(
    h64vmthread *vmthread, int func_id,
    h64exceptioninfo **einfo,
    int *out_returnint
);

void vmthread_Free(h64vmthread *vmthread);

int vmexec_ExecuteProgram(
    h64program *pr, h64misccompileroptions *moptions
);

#endif  // HORSE64_VMEXEC_H_
