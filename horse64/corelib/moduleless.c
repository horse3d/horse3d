
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
    return 1;
}
