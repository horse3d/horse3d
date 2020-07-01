
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

#define DEBUGVMEXEC

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

#if defined(DEBUGVMEXEC)
static int vmthread_PrintExec(
        h64instructionany *inst
        ) {
    char *_s = disassembler_InstructionToStr(inst);
    if (!_s) return 0;
    fprintf(stderr, "horsevm: debug: vmexec %s\n", _s);
    free(_s);
    return 1;
}
#endif

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
        #if defined(DEBUGVMEXEC)
        fprintf(stderr, "horsevm: debug: vmexec triggeroom\n");
        #endif
        fprintf(stderr, "oom\n");
        return 0;
    }
    inst_setconst: {
        h64instruction_setconst *inst = (h64instruction_setconst *)p;
        #if defined(DEBUGVMEXEC)
        if (!vmthread_PrintExec(inst)) goto triggeroom;
        #endif
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
        #if defined(DEBUGVMEXEC)
        if (!vmthread_PrintExec(inst)) goto triggeroom;
        #endif

        int copyatend = 0;
        valuecontent _tmpresultbuf = {0};
        valuecontent *tmpresult = STACK_ENTRY(stack, inst->slotto);
        if (likely(inst->slotto == inst->arg1slotfrom ||
                inst->slotto == inst->arg2slotfrom)) {
            copyatend = 1;
            tmpresult = &_tmpresultbuf;
        } else {
            valuecontent_Free(tmpresult);
            memset(tmpresult, 0, sizeof(*tmpresult));
        }

        valuecontent *v1 = STACK_ENTRY(stack, inst->arg1slotfrom);
        valuecontent *v2 = STACK_ENTRY(stack, inst->arg2slotfrom);
        #ifndef NDEBUG
        if (!op_jumptable[inst->optype]) {
            fprintf(stderr, "binop %d missing in jump table\n",
                    inst->optype);
            return 0;
        }
        #endif
        int invalidtypes = 1;
        int divisionbyzero = 0;
        char invalidtypesmsg[] = (
            "operand not allowed for given types"
        );
        goto *op_jumptable[inst->optype];
        binop_divide: {
            if (unlikely((v1->type != H64VALTYPE_INT64 &&
                    v1->type != H64VALTYPE_FLOAT64) ||
                    (v2->type != H64VALTYPE_INT64 &&
                    v2->type != H64VALTYPE_FLOAT64))) {
                // invalid
            } else {
                invalidtypes = 0;
                if (v1->type == H64VALTYPE_FLOAT64 ||
                        v2->type == H64VALTYPE_FLOAT64) {
                    double v1no = 1;
                    if (v1->type == H64VALTYPE_FLOAT64) {
                        v1no = v1->float_value;
                    } else {
                        v1no = v1->int_value;
                    }
                    double v2no = 1;
                    if (v2->type == H64VALTYPE_FLOAT64) {
                        v2no = v2->float_value;
                    } else {
                        v2no = v2->int_value;
                    }
                    tmpresult->type = H64VALTYPE_FLOAT64;
                    tmpresult->float_value = (v1no / v2no);
                    if (isnan(tmpresult->float_value) || v2no == 0) {
                        divisionbyzero = 1;
                    }
                } else {
                    tmpresult->type = H64VALTYPE_INT64;
                    if (v2->int_value == 0) {
                        divisionbyzero = 1;
                    } else {
                        tmpresult->int_value = (
                            v1->int_value / v2->int_value
                        );
                    }
                }
            }
            goto binop_done;
        }
        binop_add: {
            if (unlikely((v1->type != H64VALTYPE_INT64 &&
                    v1->type != H64VALTYPE_FLOAT64) ||
                    (v2->type != H64VALTYPE_INT64 &&
                    v2->type != H64VALTYPE_FLOAT64))) {
                // invalid
            } else if (unlikely((
                    ((v1->type == H64VALTYPE_GCVAL &&
                     ((h64gcvalue *)v1->ptr_value)->type ==
                        H64GCVALUETYPE_STRING) ||
                     v1->type == H64VALTYPE_SHORTSTR)) &&
                    ((v2->type == H64VALTYPE_GCVAL &&
                     ((h64gcvalue *)v2->ptr_value)->type ==
                        H64GCVALUETYPE_STRING) ||
                     v2->type == H64VALTYPE_SHORTSTR))) {
                fprintf(stderr, "string concatenation not implemented\n");
                return 0;
            } else {
                invalidtypes = 0;
                if (v1->type == H64VALTYPE_FLOAT64 ||
                        v2->type == H64VALTYPE_FLOAT64) {
                    double v1no = 1;
                    if (v1->type == H64VALTYPE_FLOAT64) {
                        v1no = v1->float_value;
                    } else {
                        v1no = v1->int_value;
                    }
                    double v2no = 1;
                    if (v2->type == H64VALTYPE_FLOAT64) {
                        v2no = v2->float_value;
                    } else {
                        v2no = v2->int_value;
                    }
                    tmpresult->type = H64VALTYPE_FLOAT64;
                    tmpresult->float_value = (v1no + v2no);
                } else {
                    tmpresult->type = H64VALTYPE_INT64;
                    tmpresult->int_value = (
                        v1->int_value + v2->int_value
                    );
                }
            }
            goto binop_done;
        }
        binop_substract: {
            if (unlikely((v1->type != H64VALTYPE_INT64 &&
                    v1->type != H64VALTYPE_FLOAT64) ||
                    (v2->type != H64VALTYPE_INT64 &&
                    v2->type != H64VALTYPE_FLOAT64))) {
                // invalid
            } else {
                invalidtypes = 0;
                if (v1->type == H64VALTYPE_FLOAT64 ||
                        v2->type == H64VALTYPE_FLOAT64) {
                    double v1no = 1;
                    if (v1->type == H64VALTYPE_FLOAT64) {
                        v1no = v1->float_value;
                    } else {
                        v1no = v1->int_value;
                    }
                    double v2no = 1;
                    if (v2->type == H64VALTYPE_FLOAT64) {
                        v2no = v2->float_value;
                    } else {
                        v2no = v2->int_value;
                    }
                    tmpresult->type = H64VALTYPE_FLOAT64;
                    tmpresult->float_value = (v1no - v2no);
                } else {
                    tmpresult->type = H64VALTYPE_INT64;
                    tmpresult->int_value = (
                        v1->int_value - v2->int_value
                    );
                }
            }
            goto binop_done;
        }
        binop_multiply: {
            if (unlikely((v1->type != H64VALTYPE_INT64 &&
                    v1->type != H64VALTYPE_FLOAT64) ||
                    (v2->type != H64VALTYPE_INT64 &&
                    v2->type != H64VALTYPE_FLOAT64))) {
                // invalid
            } else {
                invalidtypes = 0;
                if (v1->type == H64VALTYPE_FLOAT64 ||
                        v2->type == H64VALTYPE_FLOAT64) {
                    double v1no = 1;
                    if (v1->type == H64VALTYPE_FLOAT64) {
                        v1no = v1->float_value;
                    } else {
                        v1no = v1->int_value;
                    }
                    double v2no = 1;
                    if (v2->type == H64VALTYPE_FLOAT64) {
                        v2no = v2->float_value;
                    } else {
                        v2no = v2->int_value;
                    }
                    tmpresult->type = H64VALTYPE_FLOAT64;
                    tmpresult->float_value = (v1no * v2no);
                } else {
                    tmpresult->type = H64VALTYPE_INT64;
                    tmpresult->int_value = (
                        v1->int_value * v2->int_value
                    );
                }
            }
            goto binop_done;
        }
        binop_modulo: {
            if (unlikely((v1->type != H64VALTYPE_INT64 &&
                    v1->type != H64VALTYPE_FLOAT64) ||
                    (v2->type != H64VALTYPE_INT64 &&
                    v2->type != H64VALTYPE_FLOAT64))) {
                // invalid
            } else {
                invalidtypes = 0;
                if (v1->type == H64VALTYPE_FLOAT64 ||
                        v2->type == H64VALTYPE_FLOAT64) {
                    double v1no = 1;
                    if (v1->type == H64VALTYPE_FLOAT64) {
                        v1no = v1->float_value;
                    } else {
                        v1no = v1->int_value;
                    }
                    double v2no = 1;
                    if (v2->type == H64VALTYPE_FLOAT64) {
                        v2no = v2->float_value;
                    } else {
                        v2no = v2->int_value;
                    }
                    tmpresult->type = H64VALTYPE_FLOAT64;
                    if (unlikely(v1no < 0 || v2no < 0)) {
                        if (v1no >= 0 && v2no < 0) {
                            tmpresult->float_value = -(
                                (-v2no) - fmod(v1no, -v2no)
                            );
                        } else if (v2no >= 0) {
                            tmpresult->float_value = (
                                v2no - fmod(-v1no, v2no)
                            );
                        } else {
                            tmpresult->float_value = (
                                -fmod(-v1no, -v2no)
                            );
                        }
                    } else {
                        tmpresult->float_value = fmod(v1no, v2no);
                    }
                    if (isnan(tmpresult->float_value) || v2no == 0) {
                        divisionbyzero = 1;
                    }
                } else {
                    tmpresult->type = H64VALTYPE_INT64;
                    if (v2->int_value == 0) {
                        divisionbyzero = 1;
                    } else {
                        tmpresult->int_value = (
                            v1->int_value % v2->int_value
                        );
                    }
                }
            }
            goto binop_done;
        }
        binop_cmp_equal: {
            if (likely((v1->type != H64VALTYPE_INT64 &&
                    v1->type != H64VALTYPE_FLOAT64) ||
                    (v2->type != H64VALTYPE_INT64 &&
                    v2->type != H64VALTYPE_FLOAT64))) {
                // generic case:
                fprintf(stderr, "equality case not implemented\n");
                return 0;
            } else {
                invalidtypes = 0;
                if (v1->type == H64VALTYPE_FLOAT64 ||
                        v2->type == H64VALTYPE_FLOAT64) {
                    double v1no = 1;
                    if (v1->type == H64VALTYPE_FLOAT64) {
                        v1no = v1->float_value;
                    } else {
                        v1no = v1->int_value;
                    }
                    double v2no = 1;
                    if (v2->type == H64VALTYPE_FLOAT64) {
                        v2no = v2->float_value;
                    } else {
                        v2no = v2->int_value;
                    }
                    tmpresult->type = H64VALTYPE_FLOAT64;
                    tmpresult->float_value = (v1no == v2no);
                } else {
                    tmpresult->type = H64VALTYPE_INT64;
                    tmpresult->int_value = (
                        v1->int_value == v2->int_value
                    );
                }
            }
            goto binop_done;
        }
        binop_cmp_notequal: {
            fprintf(stderr, "oopsie daisy\n");
            return 0;
        }
        binop_cmp_largerorequal: {
            if (likely((v1->type != H64VALTYPE_INT64 &&
                    v1->type != H64VALTYPE_FLOAT64) ||
                    (v2->type != H64VALTYPE_INT64 &&
                    v2->type != H64VALTYPE_FLOAT64))) {
                // invalid
            } else {
                invalidtypes = 0;
                if (v1->type == H64VALTYPE_FLOAT64 ||
                        v2->type == H64VALTYPE_FLOAT64) {
                    double v1no = 1;
                    if (v1->type == H64VALTYPE_FLOAT64) {
                        v1no = v1->float_value;
                    } else {
                        v1no = v1->int_value;
                    }
                    double v2no = 1;
                    if (v2->type == H64VALTYPE_FLOAT64) {
                        v2no = v2->float_value;
                    } else {
                        v2no = v2->int_value;
                    }
                    tmpresult->type = H64VALTYPE_FLOAT64;
                    tmpresult->float_value = (v1no >= v2no);
                } else {
                    tmpresult->type = H64VALTYPE_INT64;
                    tmpresult->int_value = (
                        v1->int_value >= v2->int_value
                    );
                }
            }
            goto binop_done;
        }
        binop_cmp_smallerorequal: {
            if (likely((v1->type != H64VALTYPE_INT64 &&
                    v1->type != H64VALTYPE_FLOAT64) ||
                    (v2->type != H64VALTYPE_INT64 &&
                    v2->type != H64VALTYPE_FLOAT64))) {
                // invalid
            } else {
                invalidtypes = 0;
                if (v1->type == H64VALTYPE_FLOAT64 ||
                        v2->type == H64VALTYPE_FLOAT64) {
                    double v1no = 1;
                    if (v1->type == H64VALTYPE_FLOAT64) {
                        v1no = v1->float_value;
                    } else {
                        v1no = v1->int_value;
                    }
                    double v2no = 1;
                    if (v2->type == H64VALTYPE_FLOAT64) {
                        v2no = v2->float_value;
                    } else {
                        v2no = v2->int_value;
                    }
                    tmpresult->type = H64VALTYPE_FLOAT64;
                    tmpresult->float_value = (v1no <= v2no);
                } else {
                    tmpresult->type = H64VALTYPE_INT64;
                    tmpresult->int_value = (
                        v1->int_value <= v2->int_value
                    );
                }
            }
            goto binop_done;
        }
        binop_cmp_larger: {
            if (likely((v1->type != H64VALTYPE_INT64 &&
                    v1->type != H64VALTYPE_FLOAT64) ||
                    (v2->type != H64VALTYPE_INT64 &&
                    v2->type != H64VALTYPE_FLOAT64))) {
                // invalid
            } else {
                invalidtypes = 0;
                if (v1->type == H64VALTYPE_FLOAT64 ||
                        v2->type == H64VALTYPE_FLOAT64) {
                    double v1no = 1;
                    if (v1->type == H64VALTYPE_FLOAT64) {
                        v1no = v1->float_value;
                    } else {
                        v1no = v1->int_value;
                    }
                    double v2no = 1;
                    if (v2->type == H64VALTYPE_FLOAT64) {
                        v2no = v2->float_value;
                    } else {
                        v2no = v2->int_value;
                    }
                    tmpresult->type = H64VALTYPE_FLOAT64;
                    tmpresult->float_value = (v1no > v2no);
                } else {
                    tmpresult->type = H64VALTYPE_INT64;
                    tmpresult->int_value = (
                        v1->int_value > v2->int_value
                    );
                }
            }
            goto binop_done;
        }
        binop_cmp_smaller: {
            if (likely((v1->type != H64VALTYPE_INT64 &&
                    v1->type != H64VALTYPE_FLOAT64) ||
                    (v2->type != H64VALTYPE_INT64 &&
                    v2->type != H64VALTYPE_FLOAT64))) {
                // invalid
            } else {
                invalidtypes = 0;
                if (v1->type == H64VALTYPE_FLOAT64 ||
                        v2->type == H64VALTYPE_FLOAT64) {
                    double v1no = 1;
                    if (v1->type == H64VALTYPE_FLOAT64) {
                        v1no = v1->float_value;
                    } else {
                        v1no = v1->int_value;
                    }
                    double v2no = 1;
                    if (v2->type == H64VALTYPE_FLOAT64) {
                        v2no = v2->float_value;
                    } else {
                        v2no = v2->int_value;
                    }
                    tmpresult->type = H64VALTYPE_FLOAT64;
                    tmpresult->float_value = (v1no < v2no);
                } else {
                    tmpresult->type = H64VALTYPE_INT64;
                    tmpresult->int_value = (
                        v1->int_value < v2->int_value
                    );
                }
            }
            goto binop_done;
        }
        binop_done:
        if (invalidtypes || divisionbyzero) {
            fprintf(stderr, "binop error not implemented\n");
            return 0;
        }
        if (copyatend) {
            valuecontent *target = STACK_ENTRY(stack, inst->slotto);
            if (target->type == H64VALTYPE_GCVAL) {
                // prevent actual value from being free'd
                ((h64gcvalue *)target->ptr_value)->
                    externalreferencecount++;
            }
            valuecontent_Free(target);
            memcpy(target, tmpresult, sizeof(*tmpresult));
        }
        p += sizeof(h64instruction_binop);
        goto *jumptable[((h64instructionany *)p)->type];
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
        #if defined(DEBUGVMEXEC)
        if (!vmthread_PrintExec(inst)) goto triggeroom;
        #endif

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
    op_jumptable[H64OP_CMP_EQUAL] = &&binop_cmp_equal;
    op_jumptable[H64OP_CMP_NOTEQUAL] = &&binop_cmp_notequal;
    op_jumptable[H64OP_CMP_LARGEROREQUAL] = &&binop_cmp_largerorequal;
    op_jumptable[H64OP_CMP_SMALLEROREQUAL] = &&binop_cmp_smallerorequal;
    op_jumptable[H64OP_CMP_LARGER] = &&binop_cmp_larger;
    op_jumptable[H64OP_CMP_SMALLER] = &&binop_cmp_smaller;
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
