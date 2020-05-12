#ifndef HORSE64_VMEXEC_H_
#define HORSE64_VMEXEC_H_

typedef struct h64program h64program;
typedef struct h64instruction h64instruction;


typedef struct h64vmthread {
    h64program *program;
    int can_access_globals;
    int can_call_unthreadable;

    int execution_func_id;
    int execution_instruction_id;
} h64vmthread;

#endif  // HORSE64_VMEXEC_H_
