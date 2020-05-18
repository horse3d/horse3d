
#include <stdarg.h>

#include "bytecode.h"
#include "corelib/errors.h"
#include "corelib/moduleless.h"
#include "stack.h"
#include "vmexec.h"

int corelib_print(h64vmthread *vmthread, int stackbottom) {
    if (stackbottom >= STACK_SIZE(vmthread->stack)) {
        
    }
    return 0;
}

int corelib_RegisterFuncs(h64program *p) {
    if (!h64program_RegisterCFunction(
            p, "print", &corelib_print,
            NULL, 1, NULL, 1, NULL, 1, -1
            ))
        return 0;
    return 1;
}
