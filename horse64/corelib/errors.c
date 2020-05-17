
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bytecode.h"
#include "corelib/errors.h"
#include "poolalloc.h"
#include "refval.h"
#include "stack.h"
#include "vmexec.h"

int stderror(
        h64vmthread *vmthread,
        int error_class_id,
        const char *errmsg,
        ...
        ) {
    char buf[4096];
    va_list args;
    va_start(args, errmsg);
    vsnprintf(buf, sizeof(buf) - 1, errmsg, args);
    buf[sizeof(buf) - 1] = '\0';
    va_end(args);
    vmthread_WipeFuncStack(vmthread);
    if (!stack_ToSize(vmthread->stack,
                      STACK_SIZE(vmthread->stack) + 1, 0)) {
        error_class_id = H64STDERROR_OUTOFMEMORYERROR;
        snprintf(buf, sizeof(buf) - 1, "out of memory");
        if (!stack_ToSize(vmthread->stack,
                          STACK_SIZE(vmthread->stack) + 1, 1)) {
            return -1;
        }
    }
    valuecontent *v = STACK_ENTRY(vmthread->stack, -1);
    v->type = H64VALTYPE_REFVAL;
    v->ptr_value = poolalloc_malloc(vmthread->heap, 1);
    if (v->ptr_value) {
        h64refvalue *rval = (h64refvalue *)v->ptr_value;
        memset(rval, 0, sizeof(*rval));
        rval->type = H64REFVALTYPE_ERRORCLASSINSTANCE;
        rval->heapreferencecount = 0;
        rval->stackreferencecount = 1;
        rval->classid = error_class_id;
    }
    return -1;
}

int corelib_RegisterErrorClasses(
        h64program *p
        ) {
    if (!p)
        return 0;

    assert(p->classes_count == 0);

    int i = 0;
    while (i < H64STDERROR_TOTAL_COUNT) {
        assert(stderrorclassnames[i] != NULL);
        int idx = h64program_AddClass(
            p, stderrorclassnames[i], NULL, NULL
        );
        if (idx >= 0) {
            assert(p->classes_count - 1 == idx);
        } else {
            return 0;
        }
        i++;
    }

    return 1;
}
