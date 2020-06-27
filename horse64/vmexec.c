
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bytecode.h"
#include "debugsymbols.h"
#include "gcvalue.h"
#include "poolalloc.h"
#include "stack.h"
#include "vmexec.h"

h64vmthread *vmthread_New() {
    h64vmthread *vmthread = malloc(sizeof(*vmthread));
    if (!vmthread)
        return NULL;
    memset(vmthread, 0, sizeof(*vmthread));

    vmthread->heap = poolalloc_New(sizeof(h64gcvalue));
    if (!vmthread->heap) {
        vmthread_Free(vmthread);
        return NULL;
    }
    return vmthread;
}

void vmthread_Free(h64vmthread *vmthread) {
    if (vmthread->heap) {
        // Free items on heap, FIXME

        // Free heap:
        poolalloc_Destroy(vmthread->heap);
    }
}

void vmthread_WipeFuncStack(h64vmthread *vmthread) {
    assert(VMTHREAD_FUNCSTACKBOTTOM(vmthread) <=
           STACK_TOTALSIZE(vmthread->stack));
    assert(VMTHREAD_FUNCSTACKBOTTOM(vmthread) ==
           vmthread->stack->current_func_floor);
    if (VMTHREAD_FUNCSTACKBOTTOM(vmthread) < STACK_TOTALSIZE(vmthread->stack)
            ) {
        int result = stack_ToSize(
            vmthread->stack,
            VMTHREAD_FUNCSTACKBOTTOM(vmthread),
            0
        );
        assert(result != 0);  // shrink should always succeed.
        return;
    }
}

static char _unexpectedlookupfail[] = "<unexpected lookup fail>";

static const char *_classnamelookup(h64program *pr, int64_t classid) {
    h64classsymbol *csymbol = h64debugsymbols_GetClassSymbolById(
        pr->symbols, classid
    );
    if (!csymbol)
        return _unexpectedlookupfail;
    return csymbol->name;
}

int vmthread_RunFunction(
        h64vmthread *vmthread,
        h64program *pr, int func_id,
        h64exceptioninfo **einfo
        ) {
    if (!vmthread || !einfo)
        return 0;

    assert(func_id >= 0 && func_id < pr->func_count);
    char *p = pr->func[func_id].instructions;
    char *pend = p + (intptr_t)pr->func[func_id].instructions_bytes;
    void *jumptable[H64INST_TOTAL_COUNT];

    goto setupinterpreter;

    inst_invalid: {
        fprintf(stderr, "invalid instruction\n");
        return 0;
    }
    inst_setconst: {
        fprintf(stderr, "setconst not implemented\n");
        return 0;
    }
    inst_setglobal: {
        fprintf(stderr, "setglobal not implemented\n");
        return 0;
    }
    inst_getglobal: {
        fprintf(stderr, "getglobal not implemented\n");
        return 0;
    }
    inst_getfunc: {
        fprintf(stderr, "getfunc not implemented\n");
        return 0;
    }
    inst_getclass: {
        fprintf(stderr, "getclass not implemented\n");
        return 0;
    }
    inst_valuecopy: {
        fprintf(stderr, "valuecopy not implemented\n");
        return 0;
    }
    inst_binop: {
        fprintf(stderr, "binop not implemented\n");
        return 0;
    }
    inst_unop: {
        fprintf(stderr, "unop not implemented\n");
        return 0;
    }
    inst_call: {
        fprintf(stderr, "call not implemented\n");
        return 0;
    }
    inst_settop: {
        fprintf(stderr, "settop not implemented\n");
        return 0;
    }
    inst_returnvalue: {
        fprintf(stderr, "returnvalue not implemented\n");
        return 0;
    }
    inst_jumptarget: {
        fprintf(stderr, "jumptarget not implemented\n");
        return 0;
    }
    inst_condjump: {
        fprintf(stderr, "condjump not implemented\n");
        return 0;
    }
    inst_jump: {
        fprintf(stderr, "jump not implemented\n");
        return 0;
    }
    inst_newiterator: {
        fprintf(stderr, "newiterator not implemented\n");
        return 0;
    }
    inst_iterate: {
        fprintf(stderr, "iterate not implemented\n");
        return 0;
    }
    inst_pushcatchframe: {
        fprintf(stderr, "pushcatchframe not implemented\n");
        return 0;
    }
    inst_addcatchtypebyref: {
        fprintf(stderr, "addcatchtypebyref not implemented\n");
        return 0;
    }
    inst_addcatchtype: {
        fprintf(stderr, "addcatchtype not implemented\n");
        return 0;
    }
    inst_popcatchframe: {
        fprintf(stderr, "popcatchframe not implemented\n");
        return 0;
    }

    setupinterpreter:
    jumptable[H64INST_INVALID] = &&inst_invalid;
    jumptable[H64INST_SETCONST] = &&inst_setconst;
    jumptable[H64INST_SETGLOBAL] = &&inst_setglobal;
    jumptable[H64INST_GETGLOBAL] = &&inst_getglobal;
    jumptable[H64INST_GETFUNC] = &&inst_getfunc;
    jumptable[H64INST_GETCLASS] = &&inst_getclass;
    jumptable[H64INST_VALUECOPY] = &&inst_valuecopy;
    jumptable[H64INST_BINOP] = &&inst_binop;
    jumptable[H64INST_UNOP] = &&inst_unop;
    jumptable[H64INST_CALL] = &&inst_call;
    jumptable[H64INST_SETTOP] = &&inst_settop;
    jumptable[H64INST_RETURNVALUE] = &&inst_returnvalue;
    jumptable[H64INST_JUMPTARGET] = &&inst_jumptarget;
    jumptable[H64INST_CONDJUMP] = &&inst_condjump;
    jumptable[H64INST_JUMP] = &&inst_jump;
    jumptable[H64INST_NEWITERATOR] = &&inst_newiterator;
    jumptable[H64INST_ITERATE] = &&inst_iterate;
    jumptable[H64INST_PUSHCATCHFRAME] = &&inst_pushcatchframe;
    jumptable[H64INST_ADDCATCHTYPEBYREF] = &&inst_addcatchtypebyref;
    jumptable[H64INST_ADDCATCHTYPE] = &&inst_addcatchtype;
    jumptable[H64INST_POPCATCHFRAME] = &&inst_popcatchframe;
    goto *jumptable[((h64instructionany *)p)->type];
}

int vmthread_RunFunctionWithReturnInt(
        h64vmthread *vmthread, h64program *pr,
        int func_id,
        h64exceptioninfo **einfo, int *out_returnint
        ) {
    if (!vmthread || !einfo || !out_returnint)
        return 0;
    int result = vmthread_RunFunction(
        vmthread, pr, func_id, einfo
    );
    *out_returnint = 0;
    return 1;
}

int vmexec_ExecuteProgram(h64program *pr) {
    h64vmthread *mainthread = vmthread_New();
    if (!mainthread) {
        fprintf(stderr, "vmexec.c: out of memory during setup");
        return -1;
    }
    assert(pr->main_func_index >= 0);
    h64exceptioninfo *einfo = NULL;
    int rval = 0;
    if (pr->globalinit_func_index >= 0) {
        if (!vmthread_RunFunctionWithReturnInt(
                mainthread, pr, pr->globalinit_func_index, &einfo, &rval
                )) {
            fprintf(stderr, "vmexec.c: fatal error in $$globalinit, "
                "out of memory?");
            return -1;
        }
        if (einfo) {
            fprintf(stderr, "Uncaught %s\n",
                (pr->symbols ?
                 _classnamelookup(pr, einfo->exception_class_id) :
                 "Exception"));
            return -1;
        }
    }
    rval = 0;
    if (!vmthread_RunFunctionWithReturnInt(
            mainthread, pr, pr->main_func_index, &einfo, &rval
            )) {
        fprintf(stderr, "vmexec.c: fatal error in main, "
            "out of memory?");
        return -1;
    }
    if (einfo) {
        fprintf(stderr, "Uncaught %s\n",
            (pr->symbols ?
             _classnamelookup(pr, einfo->exception_class_id) :
             "Exception"));
        return -1;
    }
    return rval;
}
