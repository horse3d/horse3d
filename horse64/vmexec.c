
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bytecode.h"
#include "compiler/operator.h"
#include "debugsymbols.h"
#include "gcvalue.h"
#include "poolalloc.h"
#include "stack.h"
#include "vmexec.h"

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

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

    vmthread->stack = stack_New();
    if (!vmthread->stack) {
        vmthread_Free(vmthread);
        poolalloc_Destroy(vmthread->heap);
        return NULL;
    }

    return vmthread;
}

void vmthread_Free(h64vmthread *vmthread) {
    if (!vmthread)
        return;

    if (vmthread->heap) {
        // Free items on heap, FIXME

        // Free heap:
        poolalloc_Destroy(vmthread->heap);
    }
    if (vmthread->stack) {
        stack_Free(vmthread->stack);
    }
    free(vmthread);
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
    assert(!pr->func[func_id].iscfunc);
    char *p = pr->func[func_id].instructions;
    char *pend = p + (intptr_t)pr->func[func_id].instructions_bytes;
    void *jumptable[H64INST_TOTAL_COUNT];
    void *op_jumptable[TOTAL_OP_COUNT];
    memset(op_jumptable, 0, sizeof(*op_jumptable) * TOTAL_OP_COUNT);
    h64stack *stack = vmthread->stack;
    poolalloc *heap = heap;
    int funcnestdepth = 0;

    goto setupinterpreter;

    inst_invalid: {
        fprintf(stderr, "invalid instruction\n");
        return 0;
    }
    triggeroom: {
        fprintf(stderr, "oom\n");
        return 0;
    }
    inst_setconst: {
        h64instruction_setconst *inst = (h64instruction_setconst *)p;
        valuecontent *vc = STACK_ENTRY(stack, inst->slot);
        valuecontent_Free(vc);
        if (inst->content.type == H64VALTYPE_CONSTPREALLOCSTR) {
            vc->type = H64VALTYPE_GCVAL;
            vc->ptr_value = poolalloc_malloc(
                heap, 0
            );
            if (!vc->ptr_value)
                goto triggeroom;
            h64gcvalue *gcval = (h64gcvalue *)vc->ptr_value;
            gcval->type = H64GCVALUETYPE_STRING;
            gcval->heapreferencecount = 0;
            gcval->externalreferencecount = 1;
            memset(&gcval->str_val, 0, sizeof(gcval->str_val));
            if (!vmstrings_Set(
                    vmthread, &gcval->str_val,
                    inst->content.constpreallocstr_len)) {
                poolalloc_free(heap, gcval);
                vc->ptr_value = NULL;
                goto triggeroom;
            }
            memcpy(
                gcval->str_val.s, inst->content.constpreallocstr_value,
                inst->content.constpreallocstr_len * sizeof(unicodechar)
            );
        } else {
            memcpy(vc, &inst->content, sizeof(*vc));
            if (vc->type == H64VALTYPE_GCVAL)
                ((h64gcvalue *)vc->ptr_value)->
                    externalreferencecount = 1;
        }
        p += sizeof(h64instruction_setconst);
        goto *jumptable[((h64instructionany *)p)->type];
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
        h64instruction_binop *inst = (h64instruction_binop *)p;

        valuecontent _tmpresultbuf = {0};
        valuecontent *tmpresult = STACK_ENTRY(stack, inst->slotto);
        if (likely(inst->slotto == inst->arg1slotfrom ||
                inst->slotto == inst->arg2slotfrom)) {
            tmpresult = &_tmpresultbuf;
        } else {
            valuecontent_Free(tmpresult);
            memset(tmpresult, 0, sizeof(*tmpresult));
        }

        binop_divide: {

        }
        binop_add: {

        }
        binop_substract: {

        }
        binop_multiply: {

        }
        binop_modulo: {

        }
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
        h64instruction_returnvalue *inst = (h64instruction_returnvalue *)p;

        // Get return value:
        valuecontent *vc = STACK_ENTRY(stack, inst->returnslotfrom);
        valuecontent vccopy;
        memcpy(&vccopy, vc, sizeof(vccopy));
        if (vccopy.type == H64VALTYPE_GCVAL) {
            // Make sure it won't be GC'ed when stack is reduced
            ((h64gcvalue *)vccopy.ptr_value)->externalreferencecount++;
        }

        // Remove function stack:
        int current_stack_size = (
            pr->func[func_id].input_stack_size +
            pr->func[func_id].inner_stack_size
        );
        #ifndef NDEBUG
        if (funcnestdepth <= 1 &&
                stack->entry_total_count - current_stack_size != 0) {
            fprintf(
                stderr, "horsevm: error: "
                "stack total count %d, current func stack %d, "
                "unwound last function should return this to 0 "
                "and doesn't\n",
                (int)stack->entry_total_count, (int)current_stack_size
            );
        }
        #endif
        assert(stack->entry_total_count >= current_stack_size);
        funcnestdepth--;
        if (funcnestdepth <= 0) {
            assert(stack->entry_total_count - current_stack_size == 0);
            if (!stack_ToSize(
                    stack, 1, 0
                    )) {
                if (vccopy.type == H64VALTYPE_GCVAL)
                    ((h64gcvalue *)vccopy.ptr_value)->externalreferencecount--;
                goto triggeroom;
            }

            // Place return value:
            valuecontent *newvc = stack_GetEntrySlow(
                stack, 0
            );
            valuecontent_Free(newvc);
            memcpy(newvc, &vccopy, sizeof(vccopy));
            return 1;
        } else {

        }
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
    op_jumptable[H64OP_MATH_DIVIDE] = &&binop_divide;
    op_jumptable[H64OP_MATH_ADD] = &&binop_add;
    op_jumptable[H64OP_MATH_SUBSTRACT] = &&binop_substract;
    op_jumptable[H64OP_MATH_MULTIPLY] = &&binop_multiply;
    op_jumptable[H64OP_MATH_MODULO] = &&binop_modulo;
    assert(stack != NULL);
    if (!stack_ToSize(
            stack, pr->func[func_id].input_stack_size +
            pr->func[func_id].inner_stack_size, 0
            )) {
        goto triggeroom;
    }
    stack->current_func_floor = 0;

    funcnestdepth++;
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
    assert(vmthread->stack->entry_total_count <= 1 || !result);
    if (!result || vmthread->stack->entry_total_count == 0) {
        *out_returnint = 0;
    } else {
        valuecontent *vc = stack_GetEntrySlow(vmthread->stack, 0);
        if (vc->type == H64VALTYPE_INT64) {
            int64_t v = vc->int_value;
            if (v > INT_MAX) v = INT_MAX;
            if (v < INT_MIN) v = INT_MIN;
            *out_returnint = 1;
            return result;
        } else if (vc->type == H64VALTYPE_FLOAT64) {
            int64_t v = roundl(vc->float_value);
            if (v > INT_MAX) v = INT_MAX;
            if (v < INT_MIN) v = INT_MIN;
            *out_returnint = 1;
            return result;
        }
        *out_returnint = 0;
    }
    return result;
}

int vmexec_ExecuteProgram(h64program *pr) {
    h64vmthread *mainthread = vmthread_New();
    if (!mainthread) {
        fprintf(stderr, "vmexec.c: out of memory during setup\n");
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
                "out of memory?\n");
            vmthread_Free(mainthread);
            return -1;
        }
        if (einfo) {
            fprintf(stderr, "Uncaught %s\n",
                (pr->symbols ?
                 _classnamelookup(pr, einfo->exception_class_id) :
                 "Exception"));
            vmthread_Free(mainthread);
            return -1;
        }
    }
    rval = 0;
    if (!vmthread_RunFunctionWithReturnInt(
            mainthread, pr, pr->main_func_index, &einfo, &rval
            )) {
        fprintf(stderr, "vmexec.c: fatal error in main, "
            "out of memory?\n");
        vmthread_Free(mainthread);
        return -1;
    }
    if (einfo) {
        fprintf(stderr, "Uncaught %s\n",
            (pr->symbols ?
             _classnamelookup(pr, einfo->exception_class_id) :
             "Exception"));
        vmthread_Free(mainthread);
        return -1;
    }
    vmthread_Free(mainthread);
    return rval;
}
