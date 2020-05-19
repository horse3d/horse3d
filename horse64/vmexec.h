#ifndef HORSE64_VMEXEC_H_
#define HORSE64_VMEXEC_H_

typedef struct h64program h64program;
typedef struct h64instruction h64instruction;
typedef struct poolalloc poolalloc;
typedef struct h64stack h64stack;
typedef struct h64refvalue h64refvalue;


typedef struct h64vmfunctionframe {
    int stack_bottom;
    int func_id;
} h64vmfunctionframe;

typedef struct h64vmerrorcatchframe {
    int function_frame_no;
    int catch_instruction_offset;
    int finally_instruction_offset;
    int error_obj_temporary_id;
    int in_catch, in_finally;
} h64vmerrorcatchframe;

typedef struct h64vmthread {
    h64program *program;
    int can_access_globals;
    int can_call_unthreadable;

    h64stack *stack;
    poolalloc *heap;
    int funcframe_count, funcframe_alloc;
    h64vmfunctionframe *funcframe;
    int errorcatchframe_count;
    h64vmerrorcatchframe *errorframe;
    h64refvalue *caught_error;

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

void vmthread_Free(h64vmthread *vmthread);

#endif  // HORSE64_VMEXEC_H_
