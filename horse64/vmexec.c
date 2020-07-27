// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bytecode.h"
#include "compiler/disassembler.h"
#include "compiler/operator.h"
#include "corelib/errors.h"
#include "debugsymbols.h"
#include "gcvalue.h"
#include "poolalloc.h"
#include "stack.h"
#include "unicode.h"
#include "vmexec.h"

#define DEBUGVMEXEC


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

    int i = 0;
    while (i < vmthread->arg_reorder_space_count) {
        DELREF_NONHEAP(&vmthread->arg_reorder_space[i]);
        valuecontent_Free(&vmthread->arg_reorder_space[i]);
        i++;
    }
    free(vmthread->arg_reorder_space);
    if (vmthread->heap) {
        // Free items on heap, FIXME

        // Free heap:
        poolalloc_Destroy(vmthread->heap);
    }
    if (vmthread->stack) {
        stack_Free(vmthread->stack);
    }
    free(vmthread->funcframe);
    free(vmthread->exceptionframe);
    free(vmthread->kwarg_index_track_map);
    if (vmthread->str_pile) {
        poolalloc_Destroy(vmthread->str_pile);
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

static inline void popfuncframe(
        h64vmthread *vt, int dontclearstack
        ) {
    assert(vt->funcframe_count > 0);
    int64_t new_floor = (
        vt->funcframe[vt->funcframe_count - 1].stack_bottom
    );
    int64_t prev_floor = vt->stack->current_func_floor;
    #ifndef NDEBUG
    if (!dontclearstack)
        assert(new_floor <= prev_floor);
    #endif
    vt->stack->current_func_floor = new_floor;
    if (prev_floor < vt->stack->entry_count && !dontclearstack) {
        int result = stack_ToSize(
            vt->stack, prev_floor, 0
        );
        assert(result != 0);
    }
    vt->funcframe_count -= 1;
    #ifndef NDEBUG
    if (vt->moptions.vmexec_debug) {
        fprintf(
            stderr, "horsevm: debug: vmexec popfuncframe %d -> %d\n",
            vt->funcframe_count + 1, vt->funcframe_count
        );
    }
    #endif
    if (vt->funcframe_count <= 1) {
        vt->stack->current_func_floor = 0;
    }
}

static inline int pushfuncframe(
        h64vmthread *vt, int func_id, int return_slot,
        int return_to_func_id, ptrdiff_t return_to_execution_offset
        ) {
    #ifndef NDEBUG
    if (vt->moptions.vmexec_debug) {
        fprintf(
            stderr, "horsevm: debug: vmexec pushfuncframe %d -> %d\n",
            vt->funcframe_count, vt->funcframe_count + 1
        );
    }
    #endif
    if (vt->funcframe_count + 1 > vt->funcframe_alloc) {
        h64vmfunctionframe *new_funcframe = realloc(
            vt->funcframe, sizeof(*new_funcframe) *
            (vt->funcframe_count + 32)
        );
        if (!new_funcframe)
            return 0;
        vt->funcframe = new_funcframe;
        vt->funcframe_alloc = vt->funcframe_count + 32;
    }
    memset(
        &vt->funcframe[vt->funcframe_count], 0,
        sizeof(vt->funcframe[vt->funcframe_count])
    );
    #ifndef NDEBUG
    if (vt->funcframe_count > 0) {
        assert(
            vt->stack->entry_count -
            vt->stack->current_func_floor >=
            vt->program->func[func_id].input_stack_size
        );
    }
    #endif
    assert(vt->program != NULL &&
           func_id >= 0 && func_id < vt->program->func_count);
    int prevtop = vt->stack->entry_count;
    if (!stack_ToSize(
            vt->stack,
            (vt->funcframe_count == 0 ?
             vt->program->func[func_id].input_stack_size : 0) +
            vt->program->func[func_id].inner_stack_size, 0
            )) {
        return 0;
    }
    vt->funcframe[vt->funcframe_count].stack_bottom = (
        vt->stack->entry_count - (
        vt->program->func[func_id].input_stack_size +
        vt->program->func[func_id].inner_stack_size)
    );
    vt->funcframe[vt->funcframe_count].func_id = func_id;
    vt->funcframe[vt->funcframe_count].return_slot = return_slot;
    vt->funcframe[vt->funcframe_count].
            return_to_func_id = return_to_func_id;
    vt->funcframe[vt->funcframe_count].
            return_to_execution_offset = return_to_execution_offset;
    vt->funcframe_count++;
    vt->stack->current_func_floor = (
        vt->funcframe[vt->funcframe_count - 1].stack_bottom
    );
    return 1;
}

static int pushexceptionframe(
        h64vmthread* vmthread,
        int64_t catch_instruction_offset,
        int64_t finally_instruction_offset,
        int exception_obj_temporary_slot
        ) {
    int new_alloc = vmthread->exceptionframe_count + 10;
    if (new_alloc > vmthread->exceptionframe_alloc ||
            new_alloc < vmthread->exceptionframe_alloc - 20) {
        h64vmexceptioncatchframe *newframes = realloc(
            vmthread->exceptionframe,
            sizeof(*newframes) * new_alloc
        );
        if (!newframes && vmthread->exceptionframe_count >
                vmthread->exceptionframe_alloc) {
            return 0;
        }
        if (newframes) {
            vmthread->exceptionframe = newframes;
            vmthread->exceptionframe_alloc = new_alloc;
        }
    }
    h64vmexceptioncatchframe *newframe = (
        &vmthread->exceptionframe[vmthread->exceptionframe_count]
    );
    memset(newframe, 0, sizeof(*newframe));
    newframe->storeddelayedexception.exception_class_id = -1;
    newframe->catch_instruction_offset = catch_instruction_offset;
    newframe->finally_instruction_offset = finally_instruction_offset;
    newframe->exception_obj_temporary_id = exception_obj_temporary_slot;
    newframe->func_frame_no = vmthread->funcframe_count - 1;
    vmthread->exceptionframe_count++;
    return 1;
}

static void popexceptionframe(h64vmthread *vmthread) {
    assert(vmthread->exceptionframe_count >= 0);
    if (vmthread->exceptionframe[
            vmthread->exceptionframe_count - 1
            ].storeddelayedexception.exception_class_id >= 0) {
        free(vmthread->exceptionframe[
            vmthread->exceptionframe_count - 1
        ].storeddelayedexception.msg);
    }
    if (vmthread->exceptionframe[
            vmthread->exceptionframe_count - 1
            ].caught_types_more) {
        assert(vmthread->exceptionframe[
            vmthread->exceptionframe_count - 1
            ].caught_types_count > 5);
        free(vmthread->exceptionframe[
             vmthread->exceptionframe_count - 1
             ].caught_types_more);
    }
    vmthread->exceptionframe_count--;
}

static void vmthread_exceptions_ProceedToFinally(
        h64vmthread *vmthread,
        ATTR_UNUSED int64_t *current_func_id,  // unused in release builds
        ptrdiff_t *current_exec_offset
        ) {
    assert(vmthread->exceptionframe_count > 0);
    assert(vmthread->exceptionframe[
        vmthread->exceptionframe_count - 1
    ].triggered_catch);
    assert(!vmthread->exceptionframe[
        vmthread->exceptionframe_count - 1
    ].triggered_finally);
    assert(vmthread->exceptionframe[
        vmthread->exceptionframe_count - 1
    ].finally_instruction_offset >= 0);
    vmthread->exceptionframe[
        vmthread->exceptionframe_count - 1
    ].triggered_finally = 1;
    assert(vmthread->exceptionframe[
        vmthread->exceptionframe_count - 1
    ].func_frame_no == vmthread->funcframe_count - 1);
    assert(vmthread->funcframe[
           vmthread->funcframe_count - 1
           ].func_id ==
           *current_func_id);
    *current_exec_offset = vmthread->exceptionframe[
        vmthread->exceptionframe_count - 1
    ].finally_instruction_offset;
}

static int vmthread_exceptions_Raise(
        h64vmthread *vmthread, int64_t class_id,
        int64_t *current_func_id, ptrdiff_t *current_exec_offset,
        int canfailonoom,
        int *returneduncaughtexception,
        h64exceptioninfo *out_uncaughtexception,
        const char *msg, ...
        ) {
    int bubble_up_exception_later = 0;
    int unroll_to_frame = -1;
    int exception_to_slot = -1;
    int jump_to_finally = 0;
    if (returneduncaughtexception) *returneduncaughtexception = 0;

    // Figure out from top-most catch frame what to do:
    while (1) {
        if (vmthread->exceptionframe_count > 0) {
            // Get to which function frame we should unroll:
            unroll_to_frame = vmthread->exceptionframe[
                vmthread->exceptionframe_count - 1
            ].func_frame_no;
            exception_to_slot = vmthread->exceptionframe[
                vmthread->exceptionframe_count - 1
            ].exception_obj_temporary_id;
            if (vmthread->exceptionframe[
                    vmthread->exceptionframe_count - 1
                    ].triggered_catch ||
                    vmthread->exceptionframe[
                        vmthread->exceptionframe_count - 1
                    ].catch_instruction_offset < 0) {
                // Wait, we ran into 'catch' already.
                // But what about finally?
                if (!vmthread->exceptionframe[
                        vmthread->exceptionframe_count - 1
                        ].triggered_finally) {
                    // No finally yet. -> enter, but
                    // bubble up exception later.
                    bubble_up_exception_later = 1;
                    exception_to_slot = -1;
                    jump_to_finally = 1;
                    break;  // done setting up, resume past loop
                } else {
                    // Finally was also entered, so we
                    // failed while running it.
                    //  -> this catch frame must be ignored
                    bubble_up_exception_later = 0;
                    unroll_to_frame = -1;
                    exception_to_slot = -1;
                    popexceptionframe(vmthread);
                    continue;  // try next catch frame instead
                }
            }
        }
        break;
    }
    if (unroll_to_frame < 0) {
        unroll_to_frame = vmthread->funcframe_count - 1;
    }

    // Combine error info:
    int buflen = 2048;
    char _bufalloc[2048] = "";
    char *buf = NULL;
    if ((exception_to_slot >= 0 || unroll_to_frame < 0) &&
            !bubble_up_exception_later && msg) {
        buf = _bufalloc;
        va_list args;
        va_start(args, msg);
        vsnprintf(buf, buflen - 1, msg, args);
        buf[buflen - 1] = '\0';
        va_end(args);
    }
    h64exceptioninfo e = {0};
    e.exception_class_id = class_id;
    if (buf) {
        e.msg = utf8_to_utf32(
            buf, strlen(buf), NULL, NULL, &e.msglen
        );
    }

    // Extract backtrace:
    int k = 1;
    if (MAX_EXCEPTION_STACK_FRAMES >= 1) {
        e.stack_frame_funcid[0] = *current_func_id;
        e.stack_frame_byteoffset[0] = *current_exec_offset;
    }
    assert(unroll_to_frame < vmthread->funcframe_count);
    int i = vmthread->funcframe_count - 1;
    while (i > unroll_to_frame && i >= 0) {
        if (k < MAX_EXCEPTION_STACK_FRAMES) {
            e.stack_frame_funcid[k] = (
                vmthread->funcframe[i].return_to_func_id
            );
            e.stack_frame_byteoffset[k] = (
                vmthread->funcframe[i].return_to_execution_offset
            );
        }
        popfuncframe(vmthread, 0);
        k++;
        i--;
    }

    // If this is a final, uncaught exception, bail out here:
    if (vmthread->exceptionframe_count <= 0 &&
            !bubble_up_exception_later) {
        assert(e.exception_class_id >= 0);
        if (returneduncaughtexception) *returneduncaughtexception = 1;
        if (out_uncaughtexception) {
            memcpy(out_uncaughtexception, &e, sizeof(e));
            assert(out_uncaughtexception->exception_class_id >= 0);
        } else {
            if (returneduncaughtexception) *returneduncaughtexception = 0;
            return 0;
        }
        return 1;
    }
    // Write out exception to stack slot if needed:
    if (exception_to_slot >= 0 && !bubble_up_exception_later) {
        valuecontent *vc = STACK_ENTRY(
            vmthread->stack, exception_to_slot
        );
        DELREF_NONHEAP(vc);
        valuecontent_Free(vc);
        memset(vc, 0, sizeof(*vc));
        vc->type = H64VALTYPE_EXCEPTION;
        vc->exception_class_id = class_id;
        vc->einfo = malloc(sizeof(e));
        if (!vc->einfo && canfailonoom) {
            return 0;
        }
        memcpy(vc->einfo, &e, sizeof(e));
        ADDREF_NONHEAP(vc);
    } else {
        assert(buf == NULL || bubble_up_exception_later);
    }

    // Set proper execution position:
    int frameid = vmthread->exceptionframe[
        vmthread->exceptionframe_count - 1
    ].func_frame_no;
    assert(frameid >= 0 && frameid < vmthread->funcframe_count);
    *current_func_id = vmthread->funcframe[frameid].func_id;
    int dontpop = 0;  // whether we need to keep the catch frame we used
    if (vmthread->exceptionframe[
            vmthread->exceptionframe_count - 1
            ].catch_instruction_offset >= 0 &&
            !vmthread->exceptionframe[
                vmthread->exceptionframe_count - 1
            ].triggered_catch) {
        // Go into catch clause.
        assert(
            !vmthread->exceptionframe[
                vmthread->exceptionframe_count - 1
            ].triggered_catch
        );
        *current_exec_offset = vmthread->exceptionframe[
            vmthread->exceptionframe_count - 1
        ].catch_instruction_offset;
        vmthread->exceptionframe[
            vmthread->exceptionframe_count - 1
        ].triggered_catch = 1;
        if (vmthread->exceptionframe[
                vmthread->exceptionframe_count - 1
                ].finally_instruction_offset >= 0) {
            dontpop = 1;  // keep catch frame to run finally later
        }
    } else {
        assert(
            !vmthread->exceptionframe[
                vmthread->exceptionframe_count - 1
            ].triggered_finally
        );
        vmthread->exceptionframe[
            vmthread->exceptionframe_count - 1
        ].triggered_finally = 1;
        *current_exec_offset = vmthread->exceptionframe[
            vmthread->exceptionframe_count - 1
        ].finally_instruction_offset;
        if (bubble_up_exception_later) {
            assert(
                vmthread->exceptionframe[
                    vmthread->exceptionframe_count - 1
                ].storeddelayedexception.exception_class_id < 0
            );
            memcpy(
                &vmthread->exceptionframe[
                    vmthread->exceptionframe_count - 1
                ].storeddelayedexception, &e, sizeof(e)
            );
            assert(
                vmthread->exceptionframe[
                    vmthread->exceptionframe_count - 1
                ].storeddelayedexception.exception_class_id >= 0
            );
        }
    }
    assert(*current_exec_offset > 0);

    if (!dontpop)
        popexceptionframe(vmthread);
    return 1;
}

static void vmthread_exceptions_EndFinally(
        h64vmthread *vmthread,
        int64_t *current_func_id, ptrdiff_t *current_exec_offset,
        int *returneduncaughtexception,
        h64exceptioninfo *out_uncaughtexception
        ) {
    assert(vmthread->exceptionframe_count > 0);
    assert(vmthread->exceptionframe[
        vmthread->exceptionframe_count - 1
    ].triggered_catch);
    assert(vmthread->exceptionframe[
        vmthread->exceptionframe_count - 1
    ].triggered_finally);
    if (vmthread->exceptionframe[
            vmthread->exceptionframe_count - 1
            ].storeddelayedexception.exception_class_id >= 0) {
        h64exceptioninfo e;
        memcpy(
            &e, &vmthread->exceptionframe[
            vmthread->exceptionframe_count - 1
            ].storeddelayedexception, sizeof(e)
        );
        memset(
            &vmthread->exceptionframe[
            vmthread->exceptionframe_count - 1
            ].storeddelayedexception, 0, sizeof(e)
        );
        vmthread->exceptionframe[
            vmthread->exceptionframe_count - 1
        ].storeddelayedexception.exception_class_id = -1;
        popexceptionframe(vmthread);
        assert(e.exception_class_id >= 0);
        int wasoom = (e.exception_class_id == H64STDERROR_OUTOFMEMORYERROR);
        int result = 0;
        if (e.msg != NULL) {
            result = vmthread_exceptions_Raise(
                vmthread, e.exception_class_id,
                current_func_id, current_exec_offset,
                !wasoom, returneduncaughtexception,
                out_uncaughtexception,
                (e.msg ? "%s" : NULL), e.msg
            );
        } else {
            result = vmthread_exceptions_Raise(
                vmthread, e.exception_class_id,
                current_func_id, current_exec_offset,
                !wasoom, returneduncaughtexception,
                out_uncaughtexception,
                NULL
            );
        }
        if (!result) {
            assert(!wasoom);
            result = vmthread_exceptions_Raise(
                vmthread, H64STDERROR_OUTOFMEMORYERROR,
                current_func_id, current_exec_offset,
                0, returneduncaughtexception,
                out_uncaughtexception,
                NULL
            );
            assert(result != 0);
        }
    } else {
        popexceptionframe(vmthread);
    }
}

#ifdef NDEBUG
#define CAN_PREEXCEPTION_PRINT_INFO 0
#else
#define CAN_PREEXCEPTION_PRINT_INFO 1
#endif

static void vmexec_PrintPreExceptionInfo(
        h64vmthread *vmthread, int64_t class_id, int64_t func_id,
        int64_t offset
        ) {
    char buf[256] = "<custom user exception>";
    if (class_id >= 0 && class_id < H64STDERROR_TOTAL_COUNT) {
        snprintf(
            buf, sizeof(buf) - 1,
            "%s", stderrorclassnames[class_id]
        );
    }
    fprintf(stderr,
        "horsevm: debug: vmexec ** RAISING EXCEPTION %" PRId64
        " (%s) in func %" PRId64 " at offset %" PRId64 "\n",
        class_id, buf, func_id,
        (int64_t)offset
    );
    fprintf(stderr,
        "horsevm: debug: vmexec ** stack total entries: %" PRId64
        ", func stack bottom: %" PRId64 "\n",
        vmthread->stack->entry_count,
        vmthread->stack->current_func_floor
    );
    fprintf(stderr,
        "horsevm: debug: vmexec ** func frame count: %d\n",
        vmthread->funcframe_count
    );
}

static void vmexec_PrintPostExceptionInfo(
        ATTR_UNUSED h64vmthread *vmthread, ATTR_UNUSED int64_t class_id,
        int64_t func_id, int64_t offset
        ) {
    fprintf(stderr,
        "horsevm: debug: vmexec ** RESUME post exception"
        " in func %" PRId64 " at offset %" PRId64 "\n",
        func_id,
        (int64_t)offset
    );
}

// RAISE_EXCEPTION is a shortcut to handle raising an exception.
// It was made for use inside vmthread_RunFunction, its signature is:
// (int64_t class_id, const char *msg, ...args for msg's formatters...)
#define RAISE_EXCEPTION(class_id, ...) \
    {\
    ptrdiff_t offset = (p - pr->func[func_id].instructions);\
    int returneduncaught = 0; \
    h64exceptioninfo uncaughtexception = {0}; \
    uncaughtexception.exception_class_id = -1; \
    if (CAN_PREEXCEPTION_PRINT_INFO &&\
            vmthread->moptions.vmexec_debug) {\
        vmexec_PrintPreExceptionInfo(\
            vmthread, class_id, func_id,\
            (p - pr->func[func_id].instructions)\
        );\
    }\
    int raiseresult = vmthread_exceptions_Raise( \
        vmthread, class_id, \
        &func_id, &offset, \
        (class_id != H64STDERROR_OUTOFMEMORYERROR), \
        &returneduncaught, \
        &uncaughtexception, __VA_ARGS__ \
    );\
    if (!raiseresult && class_id != H64STDERROR_OUTOFMEMORYERROR) {\
        memset(&uncaughtexception, 0, sizeof(uncaughtexception));\
        uncaughtexception.exception_class_id = -1; \
        raiseresult = vmthread_exceptions_Raise( \
            vmthread, H64STDERROR_OUTOFMEMORYERROR, \
            &func_id, &offset, 0, \
            &returneduncaught, \
            &uncaughtexception, "Allocation failure" \
        );\
    }\
    if (!raiseresult) {\
        fprintf(stderr, "Out of memory raising OutOfMemoryError.\n");\
        _exit(1);\
        return 0;\
    }\
    if (returneduncaught) {\
        assert(uncaughtexception.exception_class_id >= 0 &&\
               "vmthread_exceptions_Raise must set uncaughtexception"); \
        *returneduncaughtexception = 1;\
        memcpy(einfo, &uncaughtexception, sizeof(uncaughtexception));\
        return 1;\
    }\
    if (CAN_PREEXCEPTION_PRINT_INFO &&\
            vmthread->moptions.vmexec_debug) {\
        vmexec_PrintPostExceptionInfo(\
            vmthread, class_id, func_id, offset\
        );\
    }\
    assert(pr->func[func_id].instructions != NULL);\
    p = (pr->func[func_id].instructions + offset);\
    }

int _vmthread_RunFunction_NoPopFuncFrames(
        h64vmthread *vmthread, int64_t func_id,
        int *returneduncaughtexception,
        h64exceptioninfo *einfo
        ) {
    if (!vmthread || !einfo)
        return 0;
    h64program *pr = vmthread->program;

    #ifndef NDEBUG
    if (vmthread->moptions.vmexec_debug)
        fprintf(
            stderr, "horsevm: debug: vmexec call "
            "C->h64 func %" PRId64 "\n",
            func_id
        );
    #endif
    assert(func_id >= 0 && func_id < pr->func_count);
    assert(!pr->func[func_id].iscfunc);
    char *p = pr->func[func_id].instructions;
    char *pend = p + (intptr_t)pr->func[func_id].instructions_bytes;
    void *jumptable[H64INST_TOTAL_COUNT];
    void *op_jumptable[TOTAL_OP_COUNT];
    memset(op_jumptable, 0, sizeof(*op_jumptable) * TOTAL_OP_COUNT);
    h64stack *stack = vmthread->stack;
    poolalloc *heap = vmthread->heap;
    int64_t original_stack_size = (
        stack->entry_count - pr->func[func_id].input_stack_size
    );
    stack->current_func_floor = original_stack_size;
    int funcnestdepth = 0;
    #ifndef NDEBUG
    if (vmthread->moptions.vmexec_debug)
        fprintf(
            stderr, "horsevm: debug: vmexec call "
            "C->h64 has stack floor %" PRId64 "\n",
            stack->current_func_floor
        );
    #endif

    goto setupinterpreter;

    inst_invalid: {
        fprintf(stderr, "invalid instruction\n");
        return 0;
    }
    triggeroom: {
        #if defined(DEBUGVMEXEC)
        fprintf(stderr, "horsevm: debug: vmexec triggeroom\n");
        #endif
        RAISE_EXCEPTION(H64STDERROR_OUTOFMEMORYERROR,
                        "Allocation failure");
        goto *jumptable[((h64instructionany *)p)->type];
    }
    inst_setconst: {
        h64instruction_setconst *inst = (h64instruction_setconst *)p;
        #ifndef NDEBUG
        if (vmthread->moptions.vmexec_debug &&
                !vmthread_PrintExec((void*)inst)) goto triggeroom;
        #endif
        assert(
            stack != NULL && inst->slot >= 0 &&
            inst->slot < stack->entry_count -
            stack->current_func_floor &&
            stack->alloc_count >= stack->entry_count
        );
        valuecontent *vc = STACK_ENTRY(stack, inst->slot);
        DELREF_NONHEAP(vc);
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
            if (!vmstrings_AllocBuffer(
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
        assert(vc->type != H64VALTYPE_CONSTPREALLOCSTR);
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
        h64instruction_getfunc *inst = (h64instruction_getfunc *)p;
        #ifndef NDEBUG
        if (vmthread->moptions.vmexec_debug &&
                !vmthread_PrintExec((void*)inst)) goto triggeroom;
        #endif

        valuecontent *vc = STACK_ENTRY(stack, inst->slotto);
        DELREF_NONHEAP(vc);
        valuecontent_Free(vc);
        vc->type = H64VALTYPE_FUNCREF;
        vc->int_value = (int64_t)inst->funcfrom;

        p += sizeof(h64instruction_getfunc);
        goto *jumptable[((h64instructionany *)p)->type];
    }
    inst_getclass: {
        h64instruction_getclass *inst = (h64instruction_getclass *)p;
        #ifndef NDEBUG
        if (vmthread->moptions.vmexec_debug &&
                !vmthread_PrintExec((void*)inst)) goto triggeroom;
        #endif

        valuecontent *vc = STACK_ENTRY(stack, inst->slotto);
        DELREF_NONHEAP(vc);
        valuecontent_Free(vc);
        vc->type = H64VALTYPE_CLASSREF;
        vc->int_value = (int64_t)inst->classfrom;

        p += sizeof(h64instruction_getclass);
        goto *jumptable[((h64instructionany *)p)->type];
    }
    inst_valuecopy: {
        h64instruction_valuecopy *inst = (h64instruction_valuecopy *)p;
        #ifndef NDEBUG
        if (vmthread->moptions.vmexec_debug &&
                !vmthread_PrintExec((void*)inst)) goto triggeroom;
        #endif

        assert(STACK_ENTRY(stack, inst->slotfrom)->type !=
               H64VALTYPE_CONSTPREALLOCSTR);

        if (inst->slotto != inst->slotfrom) {
            valuecontent *vc = STACK_ENTRY(stack, inst->slotto);
            DELREF_NONHEAP(vc);
            valuecontent_Free(vc);
            memcpy(
                vc,
                STACK_ENTRY(stack, inst->slotfrom), sizeof(*vc)
            );
            ADDREF_NONHEAP(vc);
        }

        p += sizeof(h64instruction_valuecopy);
        goto *jumptable[((h64instructionany *)p)->type];
    }
    inst_binop: {
        h64instruction_binop *inst = (h64instruction_binop *)p;
        #ifndef NDEBUG
        if (vmthread->moptions.vmexec_debug &&
                !vmthread_PrintExec((void*)inst)) goto triggeroom;
        #endif

        int copyatend = 0;
        valuecontent _tmpresultbuf = {0};
        valuecontent *tmpresult = STACK_ENTRY(stack, inst->slotto);
        if (likely(inst->slotto == inst->arg1slotfrom ||
                inst->slotto == inst->arg2slotfrom)) {
            copyatend = 1;
            tmpresult = &_tmpresultbuf;
        } else {
            DELREF_NONHEAP(tmpresult);
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
                    tmpresult->type = H64VALTYPE_BOOL;
                    tmpresult->int_value = (v1no == v2no);
                } else {
                    tmpresult->type = H64VALTYPE_BOOL;
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
                    tmpresult->type = H64VALTYPE_BOOL;
                    tmpresult->int_value = (v1no >= v2no);
                } else {
                    tmpresult->type = H64VALTYPE_BOOL;
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
                    tmpresult->type = H64VALTYPE_BOOL;
                    tmpresult->int_value = (v1no <= v2no);
                } else {
                    tmpresult->type = H64VALTYPE_BOOL;
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
                    tmpresult->type = H64VALTYPE_BOOL;
                    tmpresult->int_value = (v1no > v2no);
                } else {
                    tmpresult->type = H64VALTYPE_BOOL;
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
                    tmpresult->type = H64VALTYPE_BOOL;
                    tmpresult->int_value = (v1no < v2no);
                } else {
                    tmpresult->type = H64VALTYPE_BOOL;
                    tmpresult->int_value = (
                        v1->int_value < v2->int_value
                    );
                }
            }
            goto binop_done;
        }
        binop_done:
        if (invalidtypes) {
            RAISE_EXCEPTION(
                H64STDERROR_TYPEERROR,
                "cannot apply %s operator to given types",
                operator_OpPrintedAsStr(inst->optype)
            );
            goto *jumptable[((h64instructionany *)p)->type];
        } else if (divisionbyzero) {
            RAISE_EXCEPTION(
                H64STDERROR_MATHERROR,
                "division by zero"
            );
            goto *jumptable[((h64instructionany *)p)->type];
        }
        if (copyatend) {
            valuecontent *target = STACK_ENTRY(stack, inst->slotto);
            if (target->type == H64VALTYPE_GCVAL) {
                // prevent actual value from being free'd
                ((h64gcvalue *)target->ptr_value)->
                    externalreferencecount++;
            }
            DELREF_NONHEAP(target);
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
        h64instruction_call *inst = (h64instruction_call *)p;
        #ifndef NDEBUG
        if (vmthread->moptions.vmexec_debug &&
                !vmthread_PrintExec((void*)inst)) goto triggeroom;
        #endif

        int64_t stacktop = STACK_TOP(stack);
        valuecontent *vc = STACK_ENTRY(stack, inst->slotcalledfrom);
        if (vc->type != H64VALTYPE_FUNCREF && (
                vc->type != H64VALTYPE_GCVAL ||
                ((h64gcvalue *)vc->ptr_value)->type !=
                H64GCVALUETYPE_FUNCREF_CLOSURE ||
                ((h64gcvalue *)vc->ptr_value)->closure_info == NULL
                )) {
            RAISE_EXCEPTION(H64STDERROR_TYPEERROR,
                            "not a callable object type");
            goto *jumptable[((h64instructionany *)p)->type];
        }
        int64_t target_func_id = -1;
        h64closureinfo *cinfo = NULL;
        if (vc->type == H64VALTYPE_FUNCREF) {
            target_func_id = vc->int_value;
        } else {
            cinfo = ((h64gcvalue *)vc->ptr_value)->closure_info;
            target_func_id = cinfo->closure_func_id;
        }
        assert(target_func_id >= 0);

        // Validate that the positional argument count fits:
        int effective_posarg_count = inst->posargs;
        if (inst->expandlastposarg) {
            effective_posarg_count--;
            valuecontent *lastposarg = (
                STACK_ENTRY(stack, stacktop - 1 - inst->kwargs)
            );
            if (lastposarg->type != H64VALTYPE_GCVAL ||
                    ((h64gcvalue *)vc->ptr_value)->type !=
                    H64GCVALUETYPE_LIST) {
                RAISE_EXCEPTION(H64STDERROR_TYPEERROR,
                                "multiarg parameter must be a list");
                goto *jumptable[((h64instructionany *)p)->type];
            }
            effective_posarg_count += (
                vmlist_Count(((h64gcvalue *)vc->ptr_value)->list_values)
            );
        }
        int func_posargs = pr->func[target_func_id].input_stack_size - (
            pr->func[target_func_id].kwarg_count -
            cinfo->closure_vbox_count -
            (cinfo->closure_self != NULL ? 1 : 0)
        );
        assert(func_posargs >= 0);
        int func_lastposargismultiarg = (
            pr->func[target_func_id].last_posarg_is_multiarg
        );
        if (effective_posarg_count < func_posargs -
                (func_lastposargismultiarg ? 1 : 0)) {
            RAISE_EXCEPTION(
                H64STDERROR_ARGUMENTERROR,
                "called func requires more positional arguments"
            );
            goto *jumptable[((h64instructionany *)p)->type];
        }
        if (effective_posarg_count > func_posargs &&
                !func_lastposargismultiarg) {
            RAISE_EXCEPTION(
                H64STDERROR_ARGUMENTERROR,
                "called func cannot take this many positional arguments"
            );
            goto *jumptable[((h64instructionany *)p)->type];
        }

        // Make sure stack is large enough to enter function:
        int64_t stack_args_bottom = (
            stacktop - inst->posargs - inst->kwargs * 2
        );
        int64_t required_new_stack_size = (
            stack_args_bottom +
            (int64_t)pr->func[target_func_id].input_stack_size
        );
        assert(required_new_stack_size >= 0);
        assert(stack->current_func_floor >= 0);
        if (required_new_stack_size +
                stack->current_func_floor > STACK_TOTALSIZE(stack)) {
            int resize_result = stack_ToSize(
                stack, required_new_stack_size +
                stack->current_func_floor, 0
            );
            if (!resize_result)
                goto triggeroom;
        }

        // Make sure keyword arguments are actually known to target,
        // and do assert()s that keyword args are sorted:
        {
            if (vmthread->kwarg_index_track_count <
                    pr->func[target_func_id].kwarg_count) {
                int oldcount = vmthread->kwarg_index_track_count;
                int64_t *new_track_slots = realloc(
                    vmthread->kwarg_index_track_map,
                    sizeof(*new_track_slots) *
                        pr->func[target_func_id].kwarg_count
                );
                if (!new_track_slots)
                    goto triggeroom;
                vmthread->kwarg_index_track_map = new_track_slots;
                vmthread->kwarg_index_track_count =
                    pr->func[target_func_id].kwarg_count;
                memset(
                    &vmthread->kwarg_index_track_map[oldcount],
                    0, sizeof(valuecontent) *
                    (vmthread->kwarg_index_track_count - oldcount)
                );
            }
            int i = 0;
            while (i < inst->kwargs * 2) {
                int64_t idx = stack_args_bottom + inst->posargs + i;
                assert(STACK_ENTRY(stack, idx)->type == H64VALTYPE_INT64);
                if (i > 0) {
                    assert(STACK_ENTRY(stack, idx)->int_value >=
                           STACK_ENTRY(stack, idx - 2)->int_value);
                }
                int64_t name_idx = STACK_ENTRY(stack, idx)->int_value;
                int found = -1;
                int k = i / 2;
                while (k < pr->func[target_func_id].kwarg_count) {
                    if (pr->func[target_func_id].kwargnameindexes[k]
                            == name_idx) {
                        vmthread->kwarg_index_track_map[i / 2] = k;
                        found = 1;
                        break;
                    }
                    k++;
                }
                if (!found) {
                    RAISE_EXCEPTION(
                        H64STDERROR_ARGUMENTERROR,
                        "called func does not recognize all passed "
                        "keyword arguments"
                    );
                    goto *jumptable[((h64instructionany *)p)->type];
                }
                i += 2;
            }
        }

        // Evaluate fast-track:
        const int noargreorder = (
            likely(!func_lastposargismultiarg &&
                   !inst->expandlastposarg &&
                   inst->posargs == func_posargs &&
                   inst->kwargs ==
                       pr->func[target_func_id].kwarg_count));

        // See how many positional args we can definitely leave on the
        // stack as-is:
        int leftalone_args = inst->posargs + inst->kwargs;
        int reformat_argslots = 0;
        int reformat_slots_used = 0;
        int full_posargs = inst->posargs;
        if (unlikely(!noargreorder)) {
            // Compute what slots exactly we need to shift around:
            leftalone_args = func_posargs -
                             (func_lastposargismultiarg ? 1 : 0);
            if (inst->posargs - (inst->expandlastposarg ? 1 : 0) >
                    leftalone_args)
                leftalone_args = inst->posargs -
                                 (inst->expandlastposarg ? 1 : 0);
            if (leftalone_args < 0)
                leftalone_args = 0;
            reformat_argslots = (
                effective_posarg_count - leftalone_args
            ) + pr->func[target_func_id].kwarg_count;
            if (reformat_argslots > vmthread->arg_reorder_space_count) {
                valuecontent *new_space = realloc(
                    vmthread->arg_reorder_space,
                    sizeof(*new_space) * reformat_argslots
                );
                if (!new_space)
                    goto triggeroom;
                vmthread->arg_reorder_space = new_space;
                vmthread->arg_reorder_space_count = reformat_argslots;
            }
            int full_posargs = inst->posargs;

            // Clear out stack above where we may need to reorder:
            // First, copy out positional args:
            reformat_slots_used = 0;
            int i = effective_posarg_count - leftalone_args;
            while (i < inst->posargs) {
                if (i == inst->posargs - 1 && inst->expandlastposarg) {
                    // Remaining args are squished into a list, extract:
                    valuecontent *vlist = STACK_ENTRY(
                        stack, (int64_t)i + stack_args_bottom
                    );
                    assert(
                        vlist->type == H64VALTYPE_GCVAL &&
                        ((h64gcvalue *)vlist->ptr_value)->type ==
                            H64GCVALUETYPE_LIST
                    );
                    genericlist *l = (
                        ((h64gcvalue *)vlist->ptr_value)->list_values
                    );

                    int64_t count = vmlist_Count(l);
                    int64_t k2 = 0;
                    while (k2 < count) {
                        valuecontent *vlistentry = vmlist_Get(
                            l, k2
                        );
                        assert(vlistentry != NULL);
                        memcpy(
                            &vmthread->arg_reorder_space[
                                reformat_slots_used
                            ],
                            vlistentry,
                            sizeof(valuecontent)
                        );
                        ADDREF_NONHEAP(
                            &vmthread->arg_reorder_space[
                                reformat_slots_used
                            ]
                        );
                        k2++;
                        reformat_slots_used++;
                        full_posargs++;
                    }
                    // (Possibly) dump list:
                    DELREF_NONHEAP(
                        STACK_ENTRY(
                            stack, (int64_t)i + stack_args_bottom
                        )
                    );
                    valuecontent_Free(vlist);
                } else {
                    // Transfer argument as-is from stack:
                    memcpy(
                        &vmthread->arg_reorder_space[reformat_slots_used],
                        STACK_ENTRY(stack, (int64_t)i + stack_args_bottom),
                        sizeof(valuecontent)
                    );
                    memset(  // zero out original
                        STACK_ENTRY(
                            stack, (int64_t)i + stack_args_bottom
                        ), 0, sizeof(valuecontent)
                    );
                    reformat_slots_used++;
                }
                i++;
            }
            // Now copy out kw args too:
            int temp_slots_kwarg_start = reformat_slots_used;
            i = 0;
            while (i < pr->func[target_func_id].kwarg_count) {
                assert(reformat_slots_used < reformat_argslots);
                vmthread->arg_reorder_space[reformat_slots_used].type =
                    H64VALTYPE_UNSPECIFIED_KWARG;
                i++;
                reformat_slots_used++;
            }
            i = 0;
            while (i < inst->kwargs) {
                int64_t target_slot = vmthread->kwarg_index_track_map[i];
                assert(temp_slots_kwarg_start + target_slot <
                       reformat_argslots);
                memcpy(
                    &vmthread->arg_reorder_space[
                        temp_slots_kwarg_start + target_slot
                    ],
                    STACK_ENTRY(stack, (int64_t)i * 2 + inst->posargs +
                                       stack_args_bottom),
                    sizeof(valuecontent)
                );
                memset(
                    STACK_ENTRY(stack, (int64_t)i * 2 + inst->posargs +
                                       stack_args_bottom),
                    0, sizeof(valuecontent)
                );
                i += 2;
            }
            // Now cut off the stack above where we need to change it:
            int result = stack_ToSize(
                stack, stack_args_bottom + leftalone_args +
                stack->current_func_floor, 0
            );
            assert(result != 0);  // shrinks, so shouldn't fail
       }
       // Ok, now we rearrange the stack to be what is actually needed:
       {
            // Increase to total amount required for target func:
            assert(
                stack_args_bottom +
                pr->func[target_func_id].input_stack_size +
                stack->current_func_floor >= STACK_TOTALSIZE(stack)
            );
            int result = stack_ToSize(
                stack, stack_args_bottom +
                pr->func[target_func_id].input_stack_size +
                pr->func[target_func_id].inner_stack_size +
                stack->current_func_floor, 0
            );
            if (!result) {
                oom_with_sortinglist:
                // Free our temporary sorting space:
                if (unlikely(!noargreorder)) {
                    int z = 0;
                    while (z < reformat_slots_used) {
                        DELREF_NONHEAP(
                            &vmthread->arg_reorder_space[z]
                        );
                        valuecontent_Free(&vmthread->arg_reorder_space[z]);
                        z++;
                    }
                    memset(
                        vmthread->arg_reorder_space, 0,
                        sizeof(*vmthread->arg_reorder_space) *
                            (reformat_slots_used)
                    );
                }
                // Trigger out of memory:
                goto triggeroom;
            }
            // Make space below positional arguments for closure args:
            int closure_arg_count = (
                cinfo ? (cinfo->closure_self ? 1 : 0) +
                cinfo->closure_vbox_count :
                0);
            if (closure_arg_count > 0) {
                int old_bottom = stack_args_bottom;
                int new_bottom = (
                    old_bottom + closure_arg_count
                );
                assert(new_bottom + pr->func[target_func_id].kwarg_count +
                       func_posargs <= STACK_TOP(stack));
                assert(
                    leftalone_args <=
                    pr->func[target_func_id].kwarg_count + func_posargs
                );
                if (leftalone_args > 0)
                    memmove(
                        STACK_ENTRY(stack, new_bottom),
                        STACK_ENTRY(stack, old_bottom),
                        leftalone_args
                    );
                memset(
                    STACK_ENTRY(stack, old_bottom),
                    0, sizeof(valuecontent) * (new_bottom - old_bottom)
                );
            }
            // Place positional arguments on stack as needed:
            if (unlikely(!noargreorder)) {
                int stackslot = stack_args_bottom + closure_arg_count +
                                leftalone_args;
                int reorderslot = 0;
                int posarg = leftalone_args;
                while (posarg < func_posargs - (
                        func_lastposargismultiarg ? 1 : 0)) {
                    assert(posarg < full_posargs);
                    assert(
                        stackslot - (stack_args_bottom + closure_arg_count)
                        < func_posargs
                    );
                    assert(reorderslot < reformat_argslots);
                    memcpy(
                        STACK_ENTRY(stack, stackslot),
                        &vmthread->arg_reorder_space[reorderslot],
                        sizeof(valuecontent)
                    );
                    memset(
                        &vmthread->arg_reorder_space[reorderslot],
                        0, sizeof(valuecontent)
                    );
                    stackslot++;
                    reorderslot++;
                    posarg++;
                }
                // Stuff args that go into multiarg parameter into a list:
                if (func_lastposargismultiarg) {
                    valuecontent *multiarglist = (
                        STACK_ENTRY(stack, stackslot)
                    );
                    assert(multiarglist->type ==
                           H64VALTYPE_NONE);
                    multiarglist->type =
                        H64VALTYPE_GCVAL;
                    multiarglist->ptr_value =
                        poolalloc_malloc(
                            heap, 0
                        );
                    if (!multiarglist->ptr_value)
                        goto triggeroom;
                    h64gcvalue *gcval = (h64gcvalue *)multiarglist->ptr_value;
                    gcval->type = H64GCVALUETYPE_LIST;
                    gcval->heapreferencecount = 0;
                    gcval->externalreferencecount = 1;
                    gcval->list_values = vmlist_New();
                    if (!gcval->list_values) {
                        poolalloc_free(heap, multiarglist->ptr_value);
                        multiarglist->ptr_value = NULL;
                        goto oom_with_sortinglist;
                    }
                    while (posarg < full_posargs) {
                        assert(reorderslot < reformat_argslots);
                        int addresult = vmlist_Add(
                            gcval->list_values,
                            &vmthread->arg_reorder_space[reorderslot]
                        );
                        if (!addresult)
                            goto oom_with_sortinglist;
                        DELREF_NONHEAP(&vmthread->
                                       arg_reorder_space[reorderslot]);
                        valuecontent_Free(
                            &vmthread->arg_reorder_space[reorderslot]
                        );
                        memset(
                            &vmthread->arg_reorder_space[reorderslot],
                            0, sizeof(valuecontent)
                        );
                        reorderslot++;
                        posarg++;
                    }
                    stackslot++;
                }
                // Finally, copy the keyword arguments on top:
                if (pr->func[target_func_id].kwarg_count > 0) {
                    assert(
                        reorderslot + pr->func[target_func_id].kwarg_count
                        <= reformat_argslots
                    );
                    memcpy(
                        STACK_ENTRY(stack, stackslot),
                        &vmthread->arg_reorder_space[reorderslot],
                        sizeof(valuecontent) *
                            pr->func[target_func_id].kwarg_count
                    );
                }
                // Wipe re-order temp space:
                memset(
                    vmthread->arg_reorder_space, 0,
                    sizeof(*vmthread->arg_reorder_space) *
                        (reformat_slots_used)
                );
            }
            // Set closure args:
            int i = stack_args_bottom;
            if (cinfo && cinfo->closure_self) {
                // Insert self argument:
                assert(STACK_ENTRY(stack, i)->type == H64VALTYPE_NONE);
                valuecontent *closurearg = (
                    STACK_ENTRY(stack, i)
                );
                closurearg->type = H64VALTYPE_GCVAL;
                closurearg->ptr_value =
                    poolalloc_malloc(
                        heap, 0
                    );
                if (!closurearg->ptr_value)
                    goto triggeroom;
                memcpy(
                    closurearg->ptr_value,
                    cinfo->closure_self,
                    sizeof(valuecontent)
                );
                ADDREF_NONHEAP(closurearg);
                i++;
            }
            int ctop = i + (cinfo ? cinfo->closure_vbox_count : 0);
            int closureargid = 0;
            while (i < ctop) {
                // Insert value box argument:
                assert(
                    STACK_ENTRY(stack, i)->type == H64VALTYPE_NONE
                );
                valuecontent *closurearg = (
                    STACK_ENTRY(stack, i)
                );
                closurearg->type = H64VALTYPE_GCVAL;
                closurearg->ptr_value =
                    poolalloc_malloc(
                        heap, 0
                    );
                if (!closurearg->ptr_value)
                    goto triggeroom;
                memcpy(
                    closurearg->ptr_value,
                    (void*)cinfo->closure_vbox[closureargid],
                    sizeof(valuecontent)
                );
                ADDREF_NONHEAP(closurearg);
                i++;
                closureargid++;
            }
        }
        // The stack is now complete in its new ordering.
        // Therefore, we can do the call:
        int64_t new_func_floor = (
            stack->current_func_floor + (int64_t)stack_args_bottom
        );
        if (likely(pr->func[target_func_id].iscfunc)) {
            int (*cfunc)(h64vmthread *vmthread) = (
                (int (*)(h64vmthread *vmthread))
                pr->func[target_func_id].cfunclookup
            );
            assert(cfunc != NULL);

        } else {

        }
        return 0;
    }
    inst_settop: {
        h64instruction_settop *inst = (h64instruction_settop *)p;
        #ifndef NDEBUG
        if (vmthread->moptions.vmexec_debug &&
                !vmthread_PrintExec((void*)inst)) goto triggeroom;
        #endif

        assert(inst->topto >= 0);
        int64_t newtop = (int64_t)inst->topto + stack->current_func_floor;
        if (newtop != stack->entry_count) {
            if (stack_ToSize(
                    stack, newtop, 0
                    ) < 0) {
                goto triggeroom;
            }
        }

        p += sizeof(h64instruction_settop);
        goto *jumptable[((h64instructionany *)p)->type];
    }
    inst_returnvalue: {
        h64instruction_returnvalue *inst = (h64instruction_returnvalue *)p;
        #ifndef NDEBUG
        if (vmthread->moptions.vmexec_debug &&
                !vmthread_PrintExec((void*)inst)) goto triggeroom;
        #endif

        // Get return value:
        valuecontent *vc = STACK_ENTRY(stack, inst->returnslotfrom);
        valuecontent vccopy;
        memcpy(&vccopy, vc, sizeof(vccopy));
        ADDREF_NONHEAP(&vccopy);

        // Remove function stack:
        int current_stack_size = (
            pr->func[func_id].input_stack_size +
            pr->func[func_id].inner_stack_size
        );
        #ifndef NDEBUG
        if (funcnestdepth <= 1 &&
                stack->entry_count - current_stack_size != 0) {
            fprintf(
                stderr, "horsevm: error: "
                "stack total count %d, current func stack %d, "
                "unwound last function should return this to 0 "
                "and doesn't\n",
                (int)stack->entry_count, (int)current_stack_size
            );
        }
        #endif
        assert(stack->entry_count >= current_stack_size);
        funcnestdepth--;
        if (funcnestdepth <= 0) {
            popfuncframe(vmthread, 1);  // pop frame but leave stack!
            func_id = -1;
            assert(stack->entry_count - current_stack_size == 0);
            if (!stack_ToSize(
                    stack, original_stack_size + 1, 0
                    )) {
                DELREF_NONHEAP(&vccopy);

                // Need to "manually" raise error since we're outside of any
                // function at this point:
                if (returneduncaughtexception)
                    *returneduncaughtexception = 1;
                memset(einfo, 0, sizeof(*einfo));
                einfo->exception_class_id = H64STDERROR_OUTOFMEMORYERROR;
                return 0;
            }
            assert(stack->entry_count == original_stack_size + 1);

            // Place return value:
            valuecontent *newvc = stack_GetEntrySlow(
                stack, original_stack_size
            );
            DELREF_NONHEAP(newvc);
            valuecontent_Free(newvc);
            memcpy(newvc, &vccopy, sizeof(vccopy));
            return 1;
        }
        assert(vmthread->funcframe_count > 1);
        int returnslot = (
            vmthread->funcframe[vmthread->funcframe_count].
            return_slot
        );
        int returnfuncid = (
            vmthread->funcframe[vmthread->funcframe_count].
            return_to_func_id
        );
        ptrdiff_t returnoffset = (
            vmthread->funcframe[vmthread->funcframe_count].
            return_to_execution_offset
        );
        popfuncframe(vmthread, 0);

        // Place return value:
        if (returnslot >= 0) {
            valuecontent *newvc = stack_GetEntrySlow(
                stack, returnslot
            );
            DELREF_NONHEAP(newvc);
            valuecontent_Free(newvc);
            memcpy(newvc, &vccopy, sizeof(vccopy));
        } else {
            DELREF_NONHEAP(&vccopy);
            valuecontent_Free(&vccopy);
        }

        // Return to old execution:
        func_id = returnfuncid;
        p = pr->func[func_id].instructions + returnoffset;
        pend = pr->func[func_id].instructions + (
            (ptrdiff_t)pr->func[func_id].instructions_bytes
        );
        goto *jumptable[((h64instructionany *)p)->type];
    }
    inst_jumptarget: {
        fprintf(stderr, "jumptarget instruction "
            "not valid in final bytecode\n");
        return 0;
    }
    inst_condjump: {
        h64instruction_condjump *inst = (h64instruction_condjump *)p;
        #ifndef NDEBUG
        if (vmthread->moptions.vmexec_debug &&
                !vmthread_PrintExec((void*)inst)) goto triggeroom;
        #endif

        int jumpevalvalue = 1;
        valuecontent *vc = STACK_ENTRY(stack, inst->conditionalslot);
        if (vc->type == H64VALTYPE_INT64 ||
                vc->type == H64VALTYPE_BOOL) {
            jumpevalvalue = (vc->int_value != 0);
        } else if (vc->type == H64VALTYPE_FLOAT64) {
            jumpevalvalue = fabs(vc->float_value - 0) != 0;
        } else if (vc->type == H64VALTYPE_NONE ||
                vc->type == H64VALTYPE_EMPTYARG) {
            jumpevalvalue = 0;
        }
        if (jumpevalvalue) {
            p += sizeof(h64instruction_condjump);
            goto *jumptable[((h64instructionany *)p)->type];
        }

        p += (
            (ptrdiff_t)inst->jumpbytesoffset
        );
        assert(p >= pr->func[func_id].instructions &&
               p < pend);
        goto *jumptable[((h64instructionany *)p)->type];
    }
    inst_jump: {
        h64instruction_jump *inst = (h64instruction_jump *)p;
        #ifndef NDEBUG
        if (vmthread->moptions.vmexec_debug &&
                !vmthread_PrintExec((void*)inst)) goto triggeroom;
        #endif

        p += (
            (ptrdiff_t)inst->jumpbytesoffset
        );
        assert(p >= pr->func[func_id].instructions &&
               p < pend);
        goto *jumptable[((h64instructionany *)p)->type];
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
        h64instruction_pushcatchframe *inst = (
            (h64instruction_pushcatchframe *)p
        );
        #ifndef NDEBUG
        if (vmthread->moptions.vmexec_debug &&
                !vmthread_PrintExec((void*)inst)) goto triggeroom;
        #endif

        #ifndef NDEBUG
        int previous_count = vmthread->exceptionframe_count;
        #endif
        if (!pushexceptionframe(
                vmthread,
                ((inst->mode & CATCHMODE_JUMPONCATCH) != 0 ?
                 (p - pr->func[func_id].instructions) +
                 (int64_t)inst->jumponcatch : -1),
                ((inst->mode & CATCHMODE_JUMPONFINALLY) != 0 ?
                 (p - pr->func[func_id].instructions) +
                 (int64_t)inst->jumponfinally : -1),
                inst->slotexceptionto)) {
            goto triggeroom;
        }
        #ifndef NDEBUG
        assert(vmthread->exceptionframe_count > 0 &&
               vmthread->exceptionframe_count > previous_count);
        #endif

        p += sizeof(h64instruction_pushcatchframe);
        while (((h64instructionany *)p)->type == H64INST_ADDCATCHTYPE ||
                ((h64instructionany *)p)->type == H64INST_ADDCATCHTYPEBYREF
                ) {
            #ifndef NDEBUG
            if (vmthread->moptions.vmexec_debug &&
                    !vmthread_PrintExec((void*)p)) goto triggeroom;
            #endif
            int64_t class_id = -1;
            if (((h64instructionany *)p)->type == H64INST_ADDCATCHTYPE) {
                class_id = ((h64instruction_addcatchtype *)p)->classid;
            } else {
                int16_t slotfrom = (
                    ((h64instruction_addcatchtypebyref *)p)->slotfrom
                );
                valuecontent *vc = STACK_ENTRY(stack, slotfrom);
                if (vc->type == H64VALTYPE_CLASSREF) {
                    int64_t _class_id = vc->int_value;
                    assert(_class_id >= 0 &&
                           _class_id < pr->classes_count);
                    if (pr->classes[_class_id].is_exception) {
                        // is Exception-derived!
                        class_id = _class_id;
                    }
                }
            }
            if (class_id < 0) {
                RAISE_EXCEPTION(H64STDERROR_TYPEERROR,
                                "catch on non-Exception type");
                goto *jumptable[((h64instructionany *)p)->type];
            }
            assert(vmthread->exceptionframe_count > 0);
            h64vmexceptioncatchframe *topframe = &(vmthread->
                exceptionframe[vmthread->exceptionframe_count - 1]);
            if (topframe->caught_types_count + 1 > 5) {
                int64_t *caught_types_more_new = malloc(
                    sizeof(*caught_types_more_new) *
                    ((topframe->caught_types_count + 1) - 5)
                );
                if (!caught_types_more_new)
                    goto triggeroom;
                topframe->caught_types_more = caught_types_more_new;
            }
            int index = topframe->caught_types_count;
            if (index >= 5) {
                topframe->caught_types_more[index - 5] = class_id;
            } else {
                topframe->caught_types_firstfive[index] = class_id;
            }
            topframe->caught_types_count++;
            if (((h64instructionany *)p)->type == H64INST_ADDCATCHTYPE) {
                p += sizeof(h64instruction_addcatchtype);
            } else {
                p += sizeof(h64instruction_addcatchtypebyref);
            }
        }
        goto *jumptable[((h64instructionany *)p)->type];
    }
    inst_addcatchtypebyref: {
        fprintf(stderr, "INVALID isolated addcatchtypebyref!!\n");
        return 0;
    }
    inst_addcatchtype: {
        fprintf(stderr, "INVALID isolated addcatchtype!!\n");
        return 0;
    }
    inst_popcatchframe: {
        h64instruction_popcatchframe *inst = (
            (h64instruction_popcatchframe *)p
        );
        #ifndef NDEBUG
        if (vmthread->moptions.vmexec_debug &&
                !vmthread_PrintExec((void*)inst)) goto triggeroom;
        #endif

        // See if we got a finally block to terminate:
        assert(vmthread->exceptionframe_count > 0);
        if (vmthread->exceptionframe[
                vmthread->exceptionframe_count - 1
                ].triggered_catch &&
                vmthread->exceptionframe[
                vmthread->exceptionframe_count - 1
                ].triggered_finally) {
            int64_t offset = (p - pr->func[func_id].instructions);
            int64_t oldoffset = offset;
            int exitwithexception = 0;
            h64exceptioninfo e = {0};
            vmthread_exceptions_EndFinally(
                vmthread, &func_id, &offset,
                &exitwithexception, &e
            );  // calls popexceptionframe
            if (exitwithexception) {
                int result = stack_ToSize(stack, 0, 0);
                stack->current_func_floor = 0;
                assert(result != 0);
                *returneduncaughtexception = 1;
                memcpy(einfo, &e, sizeof(e));
                return 1;
            }
            if (offset == oldoffset) {
                offset += sizeof(h64instruction_popcatchframe);
            }
            p = (pr->func[func_id].instructions + offset);
        } else {
            popexceptionframe(vmthread);
            p += sizeof(h64instruction_popcatchframe);
        }

        goto *jumptable[((h64instructionany *)p)->type];
    }
    inst_getmember: {
        h64instruction_getmember *inst = (
            (h64instruction_getmember *)p
        );
        #ifndef NDEBUG
        if (vmthread->moptions.vmexec_debug &&
                !vmthread_PrintExec((void*)inst)) goto triggeroom;
        #endif

        // Prepare target:
        valuecontent _tmpbuf;
        valuecontent *target = (
            STACK_ENTRY(stack, inst->slotto)
        );
        int copyatend = 0;
        if (inst->slotto == inst->objslotfrom) {
            target = &_tmpbuf;
            memset(target, 0, sizeof(*target));
            copyatend = 1;
        } else {
            DELREF_NONHEAP(target);
            valuecontent_Free(target);
            memset(target, 0, sizeof(*target));
        }
        valuecontent *vc = STACK_ENTRY(stack, inst->objslotfrom);
        int64_t nameidx = inst->nameidx;
        if (nameidx == vmthread->program->as_str_name_index
                ) {  // .as_str
            // See what this actually is as a string with .as_str:
            unicodechar strvalue[128];
            int64_t strvaluelen = -1;
            if (vc->type == H64VALTYPE_GCVAL) {
                if (((h64gcvalue *)vc->ptr_value)->type ==
                        H64GCVALUETYPE_STRING) {
                    // Special case, just re-reference string:
                    if (copyatend) {
                        // Just leave as is, target and source are the same
                        p += sizeof(*inst);
                        goto *jumptable[((h64instructionany *)p)->type];
                    }
                    ((h64gcvalue *)vc->ptr_value)->
                        externalreferencecount++;
                    target->type = H64VALTYPE_GCVAL;
                    target->ptr_value = vc->ptr_value;
                    p += sizeof(*inst);
                    goto *jumptable[((h64instructionany *)p)->type];
                }
            } else if (vc->type == H64VALTYPE_INT64) {
                // Int to utf-8 string:
                char intvalue[128];
                snprintf(
                    intvalue, sizeof(intvalue) - 1,
                    "%" PRId64, vc->int_value
                );
                const int len = strlen(intvalue);
                // Conversion to utf-32:
                int i = 0;
                while (i < len) {
                    strvalue[i] = (
                        (unicodechar)(uint8_t)intvalue[i]
                    );
                    i++;
                }
                strvaluelen = len;
            } else if (vc->type == H64VALTYPE_FLOAT64) {
                // Float to utf-8 string:
                char floatvalue[128];
                snprintf(
                    floatvalue, sizeof(floatvalue) - 1,
                    "%f", vc->float_value
                );
                int len = strlen(floatvalue);

                // If we have trailing zeroes only, cut fractions off:
                int dotpos = -1;
                int nonzero_pastdot_digit = 0;
                int i = 0;
                while (i < len) {
                    if (floatvalue[i] == '.') {
                        assert(i > 0);
                        dotpos = i;
                    }
                    if (dotpos >= 0 && floatvalue[i] >= '1') {
                        nonzero_pastdot_digit = 1;
                        break;
                    }
                    i++;
                }
                if (dotpos >= 0 && !nonzero_pastdot_digit) {
                    // Confirmed fractional with only zeros -> remove
                    floatvalue[dotpos] = '\0';
                    len = dotpos;
                }

                // Conversion to utf-32:
                i = 0;
                while (i < len) {
                    strvalue[i] = (
                        (unicodechar)(uint8_t)floatvalue[i]
                    );
                    i++;
                }
                strvaluelen = len;
            } else if (vc->type == H64VALTYPE_BOOL) {
                if (vc->int_value != 0) {
                    strvalue[0] = (unicodechar)'t';
                    strvalue[1] = (unicodechar)'r';
                    strvalue[2] = (unicodechar)'u';
                    strvalue[3] = (unicodechar)'e';
                    strvaluelen = 4;
                } else {
                    strvalue[0] = (unicodechar)'f';
                    strvalue[1] = (unicodechar)'a';
                    strvalue[2] = (unicodechar)'l';
                    strvalue[3] = (unicodechar)'s';
                    strvalue[4] = (unicodechar)'e';
                    strvaluelen = 5;
                }
            } else if (vc->type == H64VALTYPE_NONE) {
                strvalue[0] = (unicodechar)'n';
                strvalue[1] = (unicodechar)'o';
                strvalue[2] = (unicodechar)'n';
                strvalue[3] = (unicodechar)'e';
                strvaluelen = 4;
            }
            // If .as_str failed to run, abort with an error:
            if (strvaluelen < 0) {
                RAISE_EXCEPTION(
                    H64STDERROR_RUNTIMEEXCEPTION,
                    "internal error: as_str not "
                    "implemented for this type"
                );
                goto *jumptable[((h64instructionany *)p)->type];
            }
            // Actually set string value that we obtained:
            target->type = H64VALTYPE_GCVAL;
            target->ptr_value = poolalloc_malloc(
                heap, 0
            );
            if (!vc->ptr_value)
                goto triggeroom;
            h64gcvalue *gcval = (h64gcvalue *)target->ptr_value;
            gcval->type = H64GCVALUETYPE_STRING;
            gcval->heapreferencecount = 0;
            gcval->externalreferencecount = 1;
            if (!vmstrings_AllocBuffer(
                    vmthread, &gcval->str_val, strvaluelen)) {
                poolalloc_free(heap, gcval);
                target->ptr_value = NULL;
                goto triggeroom;
            }
            memcpy(
                gcval->str_val.s,
                strvalue, strvaluelen
            );
            ADDREF_NONHEAP(target);
            if (copyatend) {
                DELREF_NONHEAP(STACK_ENTRY(stack, inst->slotto));
                valuecontent_Free(STACK_ENTRY(stack, inst->slotto));
                memcpy(STACK_ENTRY(stack, inst->slotto),
                       target, sizeof(*target));
            }
        } else if (nameidx == vmthread->program->add_name_index
                ) {  // .add
            if (vc->type == H64VALTYPE_GCVAL && (
                    ((h64gcvalue *)vc->ptr_value)->type ==
                    H64GCVALUETYPE_LIST ||
                    ((h64gcvalue *)vc->ptr_value)->type ==
                    H64GCVALUETYPE_SET)) {
                target->type = H64VALTYPE_GCVAL;
                target->ptr_value = poolalloc_malloc(
                    heap, 0
                );
                if (!vc->ptr_value)
                    goto triggeroom;
                h64gcvalue *gcval = (h64gcvalue *)target->ptr_value;
                gcval->type = H64GCVALUETYPE_FUNCREF_CLOSURE;
                gcval->heapreferencecount = 0;
                gcval->externalreferencecount = 1;
                gcval->closure_info = (
                    malloc(sizeof(*gcval->closure_info))
                );
                if (!gcval->closure_info) {
                    poolalloc_free(heap, gcval);
                    target->ptr_value = NULL;
                    goto triggeroom;
                }
                memset(gcval->closure_info, 0,
                       sizeof(*gcval->closure_info));
                gcval->closure_info->closure_func_id = (
                    vmthread->program->containeradd_func_index
                );
                gcval->closure_info->closure_self = (
                    (h64gcvalue *)vc->ptr_value
                );
                ((h64gcvalue *)vc->ptr_value)->heapreferencecount++;
            } else {
                RAISE_EXCEPTION(
                    H64STDERROR_MEMBERERROR,
                    "given member not present on this value"
                );
                goto *jumptable[((h64instructionany *)p)->type];
            }
        } else {
            RAISE_EXCEPTION(
                H64STDERROR_MEMBERERROR,
                "given member not present on this value"
            );
            goto *jumptable[((h64instructionany *)p)->type];
        }
        p += sizeof(*inst);
        goto *jumptable[((h64instructionany *)p)->type];
    }
    inst_jumptofinally: {
        h64instruction_jumptofinally *inst = (
            (h64instruction_jumptofinally *)p
        );
        #ifndef NDEBUG
        if (vmthread->moptions.vmexec_debug &&
                !vmthread_PrintExec((void*)inst)) goto triggeroom;
        #endif

        int64_t offset = (p - pr->func[func_id].instructions);
        vmthread_exceptions_ProceedToFinally(
            vmthread, &func_id, &offset
        );
        p = (pr->func[func_id].instructions + offset);
        goto *jumptable[((h64instructionany *)p)->type];
    }
    inst_newlist: {
        h64instruction_newlist *inst = (
            (h64instruction_newlist *)p
        );
        #ifndef NDEBUG
        if (vmthread->moptions.vmexec_debug &&
                !vmthread_PrintExec((void*)inst)) goto triggeroom;
        #endif

        valuecontent *vc = STACK_ENTRY(stack, inst->slotto);
        DELREF_NONHEAP(vc);
        valuecontent_Free(vc);
        memset(vc, 0, sizeof(*vc));
        vc->type = H64VALTYPE_GCVAL;
        vc->ptr_value = poolalloc_malloc(
            heap, 0
        );
        if (!vc->ptr_value)
            goto triggeroom;
        h64gcvalue *gcval = (h64gcvalue *)vc->ptr_value;
        gcval->type = H64GCVALUETYPE_LIST;
        gcval->heapreferencecount = 0;
        gcval->externalreferencecount = 1;
        gcval->list_values = vmlist_New();
        if (!gcval->list_values) {
            poolalloc_free(heap, vc->ptr_value);
            vc->ptr_value = NULL;
            goto triggeroom;
        }

        p += sizeof(h64instruction_newlist);
        goto *jumptable[((h64instructionany *)p)->type];
    }
    inst_newset: {
        fprintf(stderr, "newset not implemented\n");
        return 0;
    }
    inst_newmap: {
        fprintf(stderr, "newmap not implemented\n");
        return 0;
    }
    inst_newvector: {
        fprintf(stderr, "newvector not implemented\n");
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
    jumptable[H64INST_GETMEMBER] = &&inst_getmember;
    jumptable[H64INST_JUMPTOFINALLY] = &&inst_jumptofinally;
    jumptable[H64INST_NEWLIST] = &&inst_newlist;
    jumptable[H64INST_NEWSET] = &&inst_newset;
    jumptable[H64INST_NEWMAP] = &&inst_newmap;
    jumptable[H64INST_NEWVECTOR] = &&inst_newvector;
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
    if (!pushfuncframe(vmthread, func_id, -1, -1, 0)) {
        goto triggeroom;
    }
    vmthread->funcframe[vmthread->funcframe_count - 1].stack_bottom = (
        original_stack_size
    );

    funcnestdepth++;
    goto *jumptable[((h64instructionany *)p)->type];
}

int vmthread_RunFunction(
        h64vmthread *vmthread, int64_t func_id,
        int *returneduncaughtexception,
        h64exceptioninfo *einfo
        ) {
    // Remember func frames & old stack we had before, then launch:
    int64_t old_stack = vmthread->stack->entry_count - (
        vmthread->program->func[func_id].input_stack_size
    );
    int64_t old_floor = vmthread->stack->current_func_floor;
    int funcframesbefore = vmthread->funcframe_count;
    int exceptionframesbefore = vmthread->exceptionframe_count;
    int inneruncaughtexception = 0;
    int result = _vmthread_RunFunction_NoPopFuncFrames(
        vmthread, func_id, &inneruncaughtexception, einfo
    );  // ^ run actual function

    // Make sure we don't leave excess func frames behind:
    assert(vmthread->funcframe_count >= funcframesbefore);
    assert(vmthread->exceptionframe_count >= exceptionframesbefore);
    int i = vmthread->funcframe_count;
    while (i > funcframesbefore) {
        assert(inneruncaughtexception);  // only allow unclean frames if error
        popfuncframe(vmthread, 1);  // disable cleaning stack because ...
            // ... func stack bottoms might be nonsense, and this might
            // assert otherwise. We'll just wipe it manually later.
        i--;
    }
    i = vmthread->exceptionframe_count;
    while (i > exceptionframesbefore) {
        assert(inneruncaughtexception);  // only allow unclean frames if error
        popexceptionframe(vmthread);
        i--;
    }
    // Stack clean-up:
    assert(vmthread->stack->entry_count >= old_stack);
    if (inneruncaughtexception) {
        // An exception, so we need to do manual stack cleaning:
        if (vmthread->stack->entry_count > old_stack) {
            int _sizing_worked = stack_ToSize(vmthread->stack, old_stack, 0);
            assert(_sizing_worked);
        }
        assert(vmthread->stack->entry_count == old_stack);
        assert(old_floor <= vmthread->stack->current_func_floor);
        vmthread->stack->current_func_floor = old_floor;
    } else {
        // No exception, so make sure stack was properly cleared:
        assert(vmthread->stack->entry_count == old_stack + 1);
        //   (old stack + return value on top)
        // Set old function floor:
        vmthread->stack->current_func_floor = old_floor;
    }
    if (returneduncaughtexception)
        *returneduncaughtexception = inneruncaughtexception;
    return result;
}

int vmthread_RunFunctionWithReturnInt(
        h64vmthread *vmthread, int64_t func_id,
        int *returneduncaughtexception,
        h64exceptioninfo *einfo, int *out_returnint
        ) {
    if (!vmthread || !einfo || !out_returnint)
        return 0;
    int innerreturneduncaughtexception = 0;
    int64_t old_stack_size = vmthread->stack->entry_count;
    int result = vmthread_RunFunction(
        vmthread, func_id, &innerreturneduncaughtexception, einfo
    );
    assert(
        ((vmthread->stack->entry_count <= old_stack_size + 1) ||
        !result) && vmthread->stack->entry_count >= old_stack_size
    );
    if (innerreturneduncaughtexception) {
        *returneduncaughtexception = 1;
        return 1;
    }
    if (!result || vmthread->stack->entry_count <= old_stack_size) {
        *out_returnint = 0;
    } else {
        valuecontent *vc = STACK_ENTRY(
            vmthread->stack, old_stack_size
        );
        if (vc->type == H64VALTYPE_INT64) {
            int64_t v = vc->int_value;
            if (v > INT32_MAX) v = INT32_MAX;
            if (v < INT32_MIN) v = INT32_MIN;
            *out_returnint = v;
            return result;
        } else if (vc->type == H64VALTYPE_FLOAT64) {
            int64_t v = roundl(vc->float_value);
            if (v > INT32_MAX) v = INT32_MAX;
            if (v < INT32_MIN) v = INT32_MIN;
            *out_returnint = v;
            return result;
        } else if (vc->type == H64VALTYPE_BOOL) {
            int64_t v = vc->int_value;
            *out_returnint = ((v != 0) ? 0 : -1);
            return result;
        }
        *out_returnint = 0;
    }
    return result;
}

int vmexec_ExecuteProgram(
        h64program *pr, h64misccompileroptions *moptions
        ) {
    h64vmthread *mainthread = vmthread_New();
    if (!mainthread) {
        fprintf(stderr, "vmexec.c: out of memory during setup\n");
        return -1;
    }
    mainthread->program = pr;
    assert(pr->main_func_index >= 0);
    memcpy(&mainthread->moptions, moptions, sizeof(*moptions));
    h64exceptioninfo einfo = {0};
    int haduncaughtexception = 0;
    int rval = 0;
    if (pr->globalinit_func_index >= 0) {
        if (!vmthread_RunFunctionWithReturnInt(
                mainthread, pr->globalinit_func_index,
                &haduncaughtexception, &einfo, &rval
                )) {
            fprintf(stderr, "vmexec.c: fatal error in $$globalinit, "
                "out of memory?\n");
            vmthread_Free(mainthread);
            return -1;
        }
        if (haduncaughtexception) {
            assert(einfo.exception_class_id >= 0);
            fprintf(stderr, "Uncaught %s\n",
                (pr->symbols ?
                 _classnamelookup(pr, einfo.exception_class_id) :
                 "Exception"));
            vmthread_Free(mainthread);
            return -1;
        }
        int result = stack_ToSize(mainthread->stack, 0, 0);
        assert(result != 0);
    }
    haduncaughtexception = 0;
    rval = 0;
    if (!vmthread_RunFunctionWithReturnInt(
            mainthread, pr->main_func_index,
            &haduncaughtexception, &einfo, &rval
            )) {
        fprintf(stderr, "vmexec.c: fatal error in main, "
            "out of memory?\n");
        vmthread_Free(mainthread);
        return -1;
    }
    if (haduncaughtexception) {
        assert(einfo.exception_class_id >= 0);
        fprintf(stderr, "Uncaught %s\n",
            (pr->symbols ?
             _classnamelookup(pr, einfo.exception_class_id) :
             "Exception"));
        vmthread_Free(mainthread);
        return -1;
    }
    vmthread_Free(mainthread);
    return rval;
}

int vmexec_ReturnFuncError(
        h64vmthread *vmthread, int64_t error_id,
        const char *msg, ...
        ) {
    assert(STACK_TOP(vmthread->stack) > 0);
    valuecontent *vc = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vc);
    valuecontent_Free(vc);
    memset(vc, 0, sizeof(*vc));
    vc->type = H64VALTYPE_EXCEPTION;
    vc->exception_class_id = error_id;
    vc->einfo = malloc(sizeof(*vc->einfo));
    if (vc->einfo) {
        memset(vc->einfo, 0, sizeof(*vc->einfo));
        vc->einfo->exception_class_id = error_id;
        char buf[4096];
        va_list args;
        va_start(args, msg);
        vsnprintf(buf, sizeof(buf) - 1, msg, args);
        va_end(args);
        vc->einfo->msg = utf8_to_utf32(
            buf, strlen(buf), NULL, NULL, &vc->einfo->msglen
        );
        if (!vc->einfo->msg)
            vc->einfo->msglen = 0;
    }
    ADDREF_NONHEAP(vc);
    return 0;
}
