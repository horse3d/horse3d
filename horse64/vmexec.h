#ifndef HORSE64_VMEXEC_H_
#define HORSE64_VMEXEC_H_

typedef struct h64program h64program;
typedef struct h64instruction h64instruction;
typedef struct poolalloc poolalloc;
typedef struct h64stack h64stack;

typedef struct h64vmthread {
    h64program *program;
    int can_access_globals;
    int can_call_unthreadable;

    h64stack *stack;
    poolalloc *heap;
    int current_func_bottom;

    int execution_func_id;
    int execution_instruction_id;
} h64vmthread;


void vmthread_WipeFuncStack(h64vmthread *vmthread);

h64vmthread *vmthread_New();

void vmthread_Free(h64vmthread *vmthread);

#endif  // HORSE64_VMEXEC_H_
