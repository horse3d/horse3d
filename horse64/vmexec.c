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
#include "datetime.h"
#include "debugsymbols.h"
#include "gcvalue.h"
#include "nonlocale.h"
#include "poolalloc.h"
#include "stack.h"
#include "valuecontentstruct.h"
#include "vmexec.h"
#include "vmlist.h"
#include "vmschedule.h"
#include "vmstrings.h"
#include "vmsuspendtypeenum.h"
#include "widechar.h"

#define DEBUGVMEXEC


void vmthread_SetSuspendState(
        h64vmthread *vmthread,
        suspendtype suspend_type, int64_t suspend_arg
        ) {
    assert(vmthread->vmexec_owner != NULL);
    #ifndef NDEBUG
    if (vmthread->vmexec_owner->moptions.vmscheduler_debug)
        h64fprintf(
            stderr, "horsevm: debug: vmschedule.c: "
            "[t%p:%s] STATE of thread suspend "
            "changed %d -> %d"
            " (arg: %" PRId64 ")\n",
            vmthread,
            (vmthread->is_on_main_thread ? "nonparallel" : "parallel"),
            (int)vmthread->suspend_info->suspendtype,
            (int)suspend_type, (int64_t)suspend_arg
        );
    #endif
    suspendtype old_type = vmthread->suspend_info->suspendtype;
    if (old_type != SUSPENDTYPE_NONE) {
        vmthread->vmexec_owner->suspend_overview->
            waittypes_currently_active[old_type]--;
        assert(vmthread->vmexec_owner->suspend_overview->
            waittypes_currently_active[old_type] >= 0);
    }
    vmthread->suspend_info->suspendtype = suspend_type;
    vmthread->suspend_info->suspendarg = suspend_arg;
    vmthread->suspend_info->suspenditemready = 0;
    if (suspend_type != SUSPENDTYPE_NONE) {
        vmthread->vmexec_owner->suspend_overview->
            waittypes_currently_active[
                suspend_type
            ]++;
        assert(vmthread->vmexec_owner->suspend_overview->
            waittypes_currently_active[
                suspend_type
            ] > 0);
        if (old_type == SUSPENDTYPE_NONE)
            vmthread->upcoming_resume_info->run_from_start = 0;
    }
    if (suspend_type == SUSPENDTYPE_ASYNCCALLSCHEDULED ||
            suspend_type == SUSPENDTYPE_NONE) {
        vmthread->upcoming_resume_info->func_id = -1;
        #ifndef NDEBUG
        vmthread->upcoming_resume_info->precall_old_stack = -1;
        vmthread->upcoming_resume_info->precall_old_floor = -1;
        vmthread->upcoming_resume_info->precall_funcframesbefore = -1;
        vmthread->upcoming_resume_info->precall_errorframesbefore = -1;
        #endif
    }
    #ifndef NDEBUG
    if (vmthread->vmexec_owner->moptions.vmscheduler_verbose_debug) {
        h64fprintf(
            stderr, "horsevm: debug: vmschedule.c: THREAD STATES:"
        );
        int i = 0;
        while (i < vmthread->vmexec_owner->thread_count) {
            h64vmthread *vth = vmthread->vmexec_owner->thread[i];
            h64fprintf(stderr,
                " [t%p:%s]->%d(arg: %" PRId64 ","
                "resume ptr:%p,"
                "resume->func_id:%" PRId64 ","
                "resume->offset:%" PRId64 ")",
                vth,
                (vth->is_on_main_thread ? "nonparallel" : "parallel"),
                (int)vth->suspend_info->suspendtype,
                (int64_t)vth->suspend_info->suspendarg,
                vth->upcoming_resume_info,
                (int64_t)vth->upcoming_resume_info->func_id,
                (int64_t)vth->upcoming_resume_info->byteoffset
            );
            i++;
        }
        h64fprintf(stderr, "\n");
    }
    #endif
}

h64vmthread *vmthread_New(h64vmexec *owner, int is_on_main_thread) {
    h64vmthread *vmthread = malloc(sizeof(*vmthread));
    if (!vmthread)
        return NULL;
    memset(vmthread, 0, sizeof(*vmthread));
    vmthread->foreground_async_work_funcid = -1;

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
    vmthread->call_settop_reverse = -1;
    vmthread->is_on_main_thread = 0;

    vmthread->upcoming_resume_info = malloc(
        sizeof(*vmthread->upcoming_resume_info)
    );
    if (!vmthread->upcoming_resume_info) {
        vmthread_Free(vmthread);
        return NULL;
    }
    memset(vmthread->upcoming_resume_info, 0,
           sizeof(*vmthread->upcoming_resume_info));
    vmthread->upcoming_resume_info->func_id = -1;

    vmthread->suspend_info = malloc(
        sizeof(*vmthread->suspend_info)
    );
    if (!vmthread->suspend_info) {
        vmthread_Free(vmthread);
        return NULL;
    }
    memset(vmthread->suspend_info, 0,
            sizeof(*vmthread->suspend_info));
    vmthread->vmexec_owner = owner;
    assert(owner->suspend_overview != NULL);
    owner->suspend_overview->
        waittypes_currently_active[
            SUSPENDTYPE_ASYNCCALLSCHEDULED
        ]++;

    if (owner) {
        h64vmthread **new_thread = realloc(
            owner->thread, sizeof(*new_thread) * (
            owner->thread_count + 1
            )
        );
        if (!new_thread) {
            vmthread_Free(vmthread);
            return NULL;
        }
        owner->thread = new_thread;
        owner->thread[owner->thread_count] = vmthread;
        owner->thread_count++;
        vmthread->vmexec_owner = owner;
    }

    return vmthread;
}

h64vmexec *vmexec_New() {
    h64vmexec *vmexec = malloc(sizeof(*vmexec));
    if (!vmexec)
        return NULL;
    memset(vmexec, 0, sizeof(*vmexec));

    vmexec->suspend_overview = malloc(
        sizeof(*vmexec->suspend_overview)
    );
    if (!vmexec->suspend_overview) {
        free(vmexec);
        return NULL;
    }
    memset(
        vmexec->suspend_overview, 0,
        sizeof(*vmexec->suspend_overview)
    );
    vmexec->suspend_overview->waittypes_currently_active = (
        malloc(sizeof(*vmexec->suspend_overview->
                      waittypes_currently_active) *
        (SUSPENDTYPE_TOTALCOUNT))
    );
    if (!vmexec->suspend_overview->waittypes_currently_active) {
        free(vmexec->suspend_overview);
        free(vmexec);
        return NULL;
    }
    memset(
        vmexec->suspend_overview->waittypes_currently_active, 0,
        sizeof(*vmexec->suspend_overview->
                      waittypes_currently_active) *
        (SUSPENDTYPE_TOTALCOUNT)
    );

    vmexec->worker_overview = malloc(sizeof(*vmexec->worker_overview));
    if (!vmexec->worker_overview) {
        free(vmexec->suspend_overview->waittypes_currently_active);
        free(vmexec->suspend_overview);
        free(vmexec);
        return NULL;
    }
    memset(
        vmexec->worker_overview, 0,
        sizeof(*vmexec->worker_overview)
    );

    return vmexec;
}

void vmexec_Free(h64vmexec *vmexec) {
    if (!vmexec)
        return;
    if (vmexec->thread) {
        int i = 0;
        while (i < vmexec->thread_count) {
            if (vmexec->thread[i])
                vmthread_Free(vmexec->thread[i]);
            i++;
        }
        free(vmexec->thread);
    }
    if (vmexec->suspend_overview) {
        free(vmexec->suspend_overview->waittypes_currently_active);
        free(vmexec->suspend_overview);
    }
    vmschedule_FreeWorkerSet(vmexec->worker_overview);
    free(vmexec);
}

void vmthread_Free(h64vmthread *vmthread) {
    if (!vmthread)
        return;

    if (vmthread->vmexec_owner) {
        int i = 0;
        while (i < vmthread->vmexec_owner->thread_count) {
            if (vmthread->vmexec_owner->thread[i] == vmthread) {
                if (i + 1 < vmthread->vmexec_owner->thread_count)
                    memcpy(
                        &vmthread->vmexec_owner->thread[i],
                        &vmthread->vmexec_owner->thread[i + 1],
                        (vmthread->vmexec_owner->thread_count - i - 1) *
                            sizeof(*vmthread->vmexec_owner->thread)
                    );
                vmthread->vmexec_owner->thread_count--;
                continue;
            }
            i++;
        }
    }

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
    free(vmthread->errorframe);
    free(vmthread->kwarg_index_track_map);
    if (vmthread->str_pile) {
        poolalloc_Destroy(vmthread->str_pile);
    }
    if (vmthread->suspend_info) {
        free(vmthread->suspend_info);
    }
    if (vmthread->upcoming_resume_info) {
        free(vmthread->upcoming_resume_info);
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

#if defined(DEBUGVMEXEC)
static int vmthread_PrintExec(
        h64vmthread *vt, funcid_t fid, h64instructionany *inst
        ) {
    char *_s = disassembler_InstructionToStr(inst);
    if (!_s) return 0;
    h64fprintf(
        stderr, "horsevm: debug: vmexec [t%p:%s] "
        "f:%" PRId64 " "
        "o:%" PRId64 " st:%" PRId64 "/%" PRId64 " %s\n",
        vt, (vt->is_on_main_thread ? "nonparallel" : "parallel"),
        (int64_t)fid,
        (int64_t)((char*)inst - (char*)vt->vmexec_owner->
                  program->func[fid].instructions),
        (int64_t)vt->stack->current_func_floor,
        (int64_t)vt->stack->entry_count -
                 vt->stack->current_func_floor,
        _s
    );
    free(_s);
    return 1;
}
#endif

static void poperrorframe(h64vmthread *vmthread);

static inline int popfuncframe(
        h64vmthread *vt, h64misccompileroptions *moptions,
        int dontresizestack
        ) {
    assert(vt->funcframe_count > 0);
    int64_t new_floor = (
        vt->funcframe_count > 1 ?
        vt->funcframe[vt->funcframe_count - 2].stack_func_floor :
        0
    );
    int64_t prev_floor = vt->stack->current_func_floor;
    int64_t new_top = vt->funcframe[vt->funcframe_count - 1].
        restore_stack_size;
    if (vt->funcframe_count - 2 >= 0 &&
            new_top < vt->funcframe[vt->funcframe_count - 2].
            stack_space_for_this_func)
        new_top = vt->funcframe[vt->funcframe_count - 2].
                  stack_space_for_this_func;
    int new_rescueframe_count = vt->funcframe[vt->funcframe_count - 1].
        rescueframe_count_on_enter;
    while (vt->errorframe_count > new_rescueframe_count)
        poperrorframe(vt);
    #ifndef NDEBUG
    if (!dontresizestack)
        assert(new_floor <= prev_floor);
    #endif
    vt->stack->current_func_floor = new_floor;
    if (!dontresizestack) {
        #if defined(DEBUGVMEXEC)
        if (moptions->vmexec_debug)
            h64fprintf(
                stderr, "horsevm: debug: vmexec [t%p:%s] (stack "
                "resize back to old func frame %" PRId64 " at %p: "
                "%" PRId64 ", with restore_stack_size %" PRId64
                ", stack_space_for_this_func %" PRId64 ")\n",
                vt, (vt->is_on_main_thread ? "nonparallel" : "parallel"),
                (int64_t)vt->funcframe_count - 1,
                (void*)&vt->funcframe[vt->funcframe_count - 1],
                new_top,
                vt->funcframe[vt->funcframe_count - 1].
                    restore_stack_size,
                vt->funcframe[vt->funcframe_count - 1].
                    stack_space_for_this_func
            );
        #endif
        if (new_top < vt->stack->entry_count) {
            int result = stack_ToSize(
                vt->stack, new_top, 0
            );
            assert(result != 0);
        } else {
            int result = stack_ToSize(
                vt->stack, new_top, 0
            );
            if (!result)
                return 0;
        }
    }
    vt->funcframe_count -= 1;
    #ifndef NDEBUG
    if (vt->vmexec_owner->moptions.vmexec_debug) {
        h64fprintf(
            stderr, "horsevm: debug: vmexec popfuncframe %d -> %d\n",
            vt->funcframe_count + 1, vt->funcframe_count
        );
    }
    #endif
    if (vt->funcframe_count <= 1) {
        vt->stack->current_func_floor = 0;
    }
    return 1;
}

static inline int pushfuncframe(
        h64vmthread *vt, int func_id, int return_slot,
        int return_to_func_id, ptrdiff_t return_to_execution_offset,
        int64_t new_func_floor
        ) {
    h64vmexec *vmexec = vt->vmexec_owner;
    #ifndef NDEBUG
    if (vmexec->moptions.vmexec_debug) {
        h64fprintf(
            stderr, "horsevm: debug: vmexec [t%p:%s] "
            "pushfuncframe %d -> %d\n",
            vt, (vt->is_on_main_thread ? "nonparallel" : "parallel"),
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
            vmexec->program->func[func_id].input_stack_size
        );
    }
    #endif
    assert(
        vmexec->program != NULL &&
        func_id >= 0 &&
        func_id < vmexec->program->func_count
    );
    vt->funcframe[vt->funcframe_count].restore_stack_size = (
        vt->stack->entry_count
    );
    if (vt->call_settop_reverse >= 0)
        vt->funcframe[vt->funcframe_count].restore_stack_size = (
            vt->call_settop_reverse
        );
    vt->call_settop_reverse = -1;
    #ifndef NDEBUG
    if (vmexec->moptions.vmexec_debug) {
        h64fprintf(
            stderr, "horsevm: debug: vmexec [t%p:%s] "
            "   (return stack size on frame %" PRId64 " at %p: "
            "%" PRId64 ")\n",
            vt, (vt->is_on_main_thread ? "nonparallel" : "parallel"),
            (int64_t)vt->funcframe_count,
            (void*)&vt->funcframe[vt->funcframe_count],
            vt->funcframe[vt->funcframe_count].restore_stack_size
        );
    }
    #endif
    assert(new_func_floor >= 0);
    if (!stack_ToSize(
            vt->stack,
            (new_func_floor +
             vmexec->program->func[func_id].input_stack_size +
             vmexec->program->func[func_id].inner_stack_size),
            0
            )) {
        return 0;
    }
    vt->funcframe[vt->funcframe_count].rescueframe_count_on_enter = (
        vt->errorframe_count
    );
    vt->funcframe[vt->funcframe_count].stack_func_floor = new_func_floor;
    vt->funcframe[vt->funcframe_count].func_id = func_id;
    vt->funcframe[vt->funcframe_count].stack_space_for_this_func = (
        vmexec->program->func[func_id].input_stack_size +
        vmexec->program->func[func_id].inner_stack_size
    );
    vt->funcframe[vt->funcframe_count].return_slot = return_slot;
    vt->funcframe[vt->funcframe_count].
            return_to_func_id = return_to_func_id;
    vt->funcframe[vt->funcframe_count].
            return_to_execution_offset = return_to_execution_offset;
    vt->funcframe_count++;
    vt->stack->current_func_floor = (
        vt->funcframe[vt->funcframe_count - 1].stack_func_floor
    );
    vt->call_settop_reverse = -1;
    return 1;
}

static int pusherrorframe(
        h64vmthread* vmthread, int frameid,
        int64_t catch_instruction_offset,
        int64_t finally_instruction_offset,
        int error_obj_temporary_slot
        ) {
    int new_alloc = vmthread->errorframe_count + 10;
    if (new_alloc > vmthread->errorframe_alloc ||
            new_alloc < vmthread->errorframe_alloc - 20) {
        h64vmrescueframe *newframes = realloc(
            vmthread->errorframe,
            sizeof(*newframes) * new_alloc
        );
        if (!newframes && vmthread->errorframe_count >
                vmthread->errorframe_alloc) {
            return 0;
        }
        if (newframes) {
            vmthread->errorframe = newframes;
            vmthread->errorframe_alloc = new_alloc;
        }
    }
    h64vmrescueframe *newframe = (
        &vmthread->errorframe[vmthread->errorframe_count]
    );
    memset(newframe, 0, sizeof(*newframe));
    newframe->id = frameid;
    newframe->storeddelayederror.error_class_id = -1;
    newframe->catch_instruction_offset = catch_instruction_offset;
    newframe->finally_instruction_offset = finally_instruction_offset;
    newframe->error_obj_temporary_id = error_obj_temporary_slot;
    newframe->func_frame_no = vmthread->funcframe_count - 1;
    vmthread->errorframe_count++;
    vmthread->call_settop_reverse = -1;
    return 1;
}

static void poperrorframe(h64vmthread *vmthread) {
    assert(vmthread->errorframe_count >= 0);
    if (vmthread->errorframe[
            vmthread->errorframe_count - 1
            ].storeddelayederror.error_class_id >= 0) {
        free(vmthread->errorframe[
            vmthread->errorframe_count - 1
        ].storeddelayederror.msg);
    }
    if (vmthread->errorframe[
            vmthread->errorframe_count - 1
            ].caught_types_more) {
        assert(vmthread->errorframe[
            vmthread->errorframe_count - 1
            ].caught_types_count > 5);
        free(vmthread->errorframe[
             vmthread->errorframe_count - 1
             ].caught_types_more);
    }
    vmthread->errorframe_count--;
    vmthread->call_settop_reverse = -1;
}

static void vmthread_errors_ProceedToFinally(
        h64vmthread *vmthread,
        ATTR_UNUSED int64_t *current_func_id,  // unused in release builds
        ptrdiff_t *current_exec_offset
        ) {
    assert(vmthread->errorframe_count > 0);
    assert(!vmthread->errorframe[
               vmthread->errorframe_count - 1
           ].triggered_catch || vmthread->errorframe[
               vmthread->errorframe_count - 1
           ].storeddelayederror.error_class_id < 0);
    assert(!vmthread->errorframe[
        vmthread->errorframe_count - 1
    ].triggered_finally);
    assert(vmthread->errorframe[
        vmthread->errorframe_count - 1
    ].finally_instruction_offset >= 0);
    vmthread->errorframe[
        vmthread->errorframe_count - 1
    ].triggered_finally = 1;
    assert(vmthread->errorframe[
        vmthread->errorframe_count - 1
    ].func_frame_no == vmthread->funcframe_count - 1);
    assert(vmthread->funcframe[
           vmthread->funcframe_count - 1
           ].func_id ==
           *current_func_id);
    *current_exec_offset = vmthread->errorframe[
        vmthread->errorframe_count - 1
    ].finally_instruction_offset;
}

static int vmthread_errors_Raise(
        h64vmthread *vmthread, int64_t class_id,
        int64_t *current_func_id, ptrdiff_t *current_exec_offset,
        int canfailonoom,
        int *returneduncaughterror,
        h64errorinfo *out_uncaughterror,
        int utf32,
        const char *msg, ...
        ) {
    if (vmthread->call_settop_reverse)
        vmthread->call_settop_reverse = -1;
    int bubble_up_error_later = 0;
    int unroll_to_frame = -1;
    int error_to_slot = -1;
    int jump_to_finally = 0;
    if (returneduncaughterror) *returneduncaughterror = 0;

    // Clear out left over work of any kind:
    vmthread_ClearAsyncForegroundWork(vmthread);

    // Figure out from top-most catch frame what to do:
    while (1) {
        if (vmthread->errorframe_count > 0) {
            // Get to which function frame we should unroll:
            unroll_to_frame = vmthread->errorframe[
                vmthread->errorframe_count - 1
            ].func_frame_no;
            error_to_slot = vmthread->errorframe[
                vmthread->errorframe_count - 1
            ].error_obj_temporary_id;
            if (vmthread->errorframe[
                    vmthread->errorframe_count - 1
                    ].triggered_catch ||
                    vmthread->errorframe[
                        vmthread->errorframe_count - 1
                    ].catch_instruction_offset < 0) {
                // Wait, we ran into 'rescue' already.
                // But what about finally?
                if (!vmthread->errorframe[
                        vmthread->errorframe_count - 1
                        ].triggered_finally) {
                    // No finally yet. -> enter, but
                    // bubble up error later.
                    bubble_up_error_later = 1;
                    error_to_slot = -1;
                    jump_to_finally = 1;
                    break;  // done setting up, resume past loop
                } else {
                    // Finally was also entered, so we
                    // failed while running it.
                    //  -> this catch frame must be ignored
                    bubble_up_error_later = 0;
                    unroll_to_frame = -1;
                    error_to_slot = -1;
                    poperrorframe(vmthread);
                    continue;  // try next catch frame instead
                }
            }
        } else {
            // Uncaught errors must go to stack slot 0:
            error_to_slot = 0;
        }
        break;
    }
    if (unroll_to_frame < 0) {
        unroll_to_frame = vmthread->funcframe_count - 1;
    }

    // Combine error info:
    h64errorinfo e = {0};
    e.error_class_id = class_id;
    int storemsg = (
        (error_to_slot >= 0 || bubble_up_error_later) &&
        msg
    );
    if (!utf32) {
        int buflen = 2048;
        char _bufalloc[2048] = "";
        char *buf = NULL;
        if (storemsg) {
            buf = _bufalloc;
            va_list args;
            va_start(args, msg);
            vsnprintf(buf, buflen - 1, msg, args);
            buf[buflen - 1] = '\0';
            va_end(args);
        }
        if (buf) {
            e.msg = utf8_to_utf32(
                buf, strlen(buf), NULL, NULL, &e.msglen
            );
        }
    } else {
        if (storemsg) {
            va_list args;
            va_start(args, msg);
            int len = va_arg(args, int);
            va_end(args);
            e.msg = malloc(len * sizeof(h64wchar));
            if (!e.msg) {
                e.msglen = 0;
            } else {
                e.msglen = len;
                memcpy(e.msg, msg, e.msglen * sizeof(h64wchar));
            }
        }
    }
    #ifndef NDEBUG
    if (vmthread->vmexec_owner->moptions.vmexec_debug) {
        h64fprintf(stderr,
            "horsevm: debug: vmexec vmthread_errors_Raise -> "
            "error class %" PRId64 " with msglen=%d "
            "storemsg=%d (u32str=%d)\n",
            (int64_t)class_id, (int)e.msglen, (int)storemsg,
            (int)utf32
        );
    }
    #endif

    // Extract backtrace:
    int k = 1;
    if (MAX_ERROR_STACK_FRAMES >= 1) {
        e.stack_frame_funcid[0] = *current_func_id;
        e.stack_frame_byteoffset[0] = *current_exec_offset;
    }
    assert(unroll_to_frame < vmthread->funcframe_count);
    int fataloom = 0;
    int i = vmthread->funcframe_count - 1;
    while (i > unroll_to_frame && i >= 0) {
        if (k < MAX_ERROR_STACK_FRAMES) {
            e.stack_frame_funcid[k] = (
                vmthread->funcframe[i].return_to_func_id
            );
            e.stack_frame_byteoffset[k] = (
                vmthread->funcframe[i].return_to_execution_offset
            );
        }
        if (!popfuncframe(
                    vmthread, &vmthread->vmexec_owner->moptions, 0
                    ) &&
                (i - 1 < 0 || i - 1 == unroll_to_frame)) {
            fataloom = 1;
        }
        k++;
        i--;
    }

    // If we do a fatal oom here, that's no good:
    if (fataloom) {
        // Can't really recover from that:
        if (returneduncaughterror) *returneduncaughterror = 0;
        return 0;
    }

    // If this is a final, uncaught error, bail out here:
    if (vmthread->errorframe_count <= 0) {
        assert(!bubble_up_error_later);
        assert(e.error_class_id >= 0);
        if (returneduncaughterror) *returneduncaughterror = 1;
        if (out_uncaughterror) {
            memcpy(out_uncaughterror, &e, sizeof(e));
            out_uncaughterror->refcount = 0;
            assert(out_uncaughterror->error_class_id >= 0);
        } else {
            if (returneduncaughterror) *returneduncaughterror = 0;
            return 0;
        }
        return 1;
    }
    // Write out error to stack slot if needed:
    if (error_to_slot >= 0 && !bubble_up_error_later) {
        valuecontent *vc = STACK_ENTRY(
            vmthread->stack, error_to_slot
        );
        DELREF_NONHEAP(vc);
        valuecontent_Free(vc);
        memset(vc, 0, sizeof(*vc));
        vc->type = H64VALTYPE_ERROR;
        vc->error_class_id = class_id;
        vc->einfo = malloc(sizeof(e));
        if (!vc->einfo && canfailonoom) {
            return 0;
        }
        memcpy(vc->einfo, &e, sizeof(e));
        vc->einfo->refcount = 0;
        ADDREF_NONHEAP(vc);
    } else {
        assert(e.msglen == 0 || storemsg);
    }

    // Set proper execution position:
    int frameid = vmthread->errorframe[
        vmthread->errorframe_count - 1
    ].func_frame_no;
    assert(frameid >= 0 && frameid < vmthread->funcframe_count);
    *current_func_id = vmthread->funcframe[frameid].func_id;
    if (vmthread->errorframe[
            vmthread->errorframe_count - 1
            ].catch_instruction_offset >= 0 &&
            !vmthread->errorframe[
                vmthread->errorframe_count - 1
            ].triggered_catch) {
        // Go into rescue clause.
        assert(
            !vmthread->errorframe[
                vmthread->errorframe_count - 1
            ].triggered_catch
        );
        *current_exec_offset = vmthread->errorframe[
            vmthread->errorframe_count - 1
        ].catch_instruction_offset;
        vmthread->errorframe[
            vmthread->errorframe_count - 1
        ].triggered_catch = 1;
    } else {
        assert(
            !vmthread->errorframe[
                vmthread->errorframe_count - 1
            ].triggered_finally
        );
        vmthread->errorframe[
            vmthread->errorframe_count - 1
        ].triggered_finally = 1;
        *current_exec_offset = vmthread->errorframe[
            vmthread->errorframe_count - 1
        ].finally_instruction_offset;
        if (bubble_up_error_later) {
            assert(
                vmthread->errorframe[
                    vmthread->errorframe_count - 1
                ].storeddelayederror.error_class_id < 0
            );
            memcpy(
                &vmthread->errorframe[
                    vmthread->errorframe_count - 1
                ].storeddelayederror, &e, sizeof(e)
            );
            assert(
                vmthread->errorframe[
                    vmthread->errorframe_count - 1
                ].storeddelayederror.error_class_id >= 0
            );
        }
    }
    assert(*current_exec_offset > 0);
    return 1;
}

int vmthread_ResetCallTempStack(h64vmthread *vmthread) {
    if (vmthread->call_settop_reverse >= 0) {
        if (!stack_ToSize(vmthread->stack,
                vmthread->call_settop_reverse, 1)) {
            vmthread->call_settop_reverse = -1;
            return 0;
        }
        vmthread->call_settop_reverse = -1;
    }
    return 1;
}

static int _rescueframefromid(
        h64vmthread *vmthread, int rescueframeid
        ) {
    assert(vmthread->funcframe_count > 0);
    int i = vmthread->errorframe_count - 1;
    while (i >= vmthread->funcframe[
            vmthread->funcframe_count - 1
            ].rescueframe_count_on_enter) {
        if (vmthread->errorframe[i].id == rescueframeid)
            return i;
        i--;
    }
    return -1;
}

static void vmthread_errors_EndFinally(
        h64vmthread *vmthread, int rescueframeid,
        int64_t *current_func_id, ptrdiff_t *current_exec_offset,
        int *returneduncaughterror,
        h64errorinfo *out_uncaughterror
        ) {
    int endedframeslot = _rescueframefromid(vmthread, rescueframeid);
    assert(endedframeslot >= 0);
    assert(vmthread->errorframe_count > 0);
    if (vmthread->errorframe[
            endedframeslot
            ].finally_instruction_offset > 0) {
        // This catch frame actually had a finally block.
        assert(vmthread->errorframe[
            endedframeslot
        ].triggered_finally);
        // See if we have a delayed error that needs bubbling up:
        if (vmthread->errorframe[
                endedframeslot
                ].storeddelayederror.error_class_id >= 0) {
            h64errorinfo e;
            memcpy(
                &e, &vmthread->errorframe[
                endedframeslot
                ].storeddelayederror, sizeof(e)
            );
            memset(
                &vmthread->errorframe[
                endedframeslot
                ].storeddelayederror, 0, sizeof(e)
            );
            vmthread->errorframe[
                endedframeslot
            ].storeddelayederror.error_class_id = -1;
            while (vmthread->errorframe_count > endedframeslot)
                poperrorframe(vmthread);
            assert(e.error_class_id >= 0);
            int wasoom = (e.error_class_id == H64STDERROR_OUTOFMEMORYERROR);
            int result = 0;
            if (e.msg != NULL) {
                result = vmthread_errors_Raise(
                    vmthread, e.error_class_id,
                    current_func_id, current_exec_offset,
                    !wasoom, returneduncaughterror,
                    out_uncaughterror,
                    1, (const char *)e.msg, (int)e.msglen
                );
            } else {
                result = vmthread_errors_Raise(
                    vmthread, e.error_class_id,
                    current_func_id, current_exec_offset,
                    !wasoom, returneduncaughterror,
                    out_uncaughterror,
                    1, NULL, (int)0
                );
            }
            if (!result) {
                assert(!wasoom);
                result = vmthread_errors_Raise(
                    vmthread, H64STDERROR_OUTOFMEMORYERROR,
                    current_func_id, current_exec_offset,
                    0, returneduncaughterror,
                    out_uncaughterror,
                    1, NULL, (int)0
                );
                assert(result != 0);
            }
        }
    }
    while (vmthread->errorframe_count > endedframeslot)
        poperrorframe(vmthread);
}

#ifdef NDEBUG
#define CAN_PREERROR_PRINT_INFO 0
#else
#define CAN_PREERROR_PRINT_INFO 1
#endif

static void vmexec_PrintPreErrorInfo(
        h64vmthread *vmthread, int64_t class_id, int64_t func_id,
        int64_t offset
        ) {
    char buf[256] = "<custom user error>";
    if (class_id >= 0 && class_id < H64STDERROR_TOTAL_COUNT) {
        snprintf(
            buf, sizeof(buf) - 1,
            "%s", stderrorclassnames[class_id]
        );
    }
    h64fprintf(stderr,
        "horsevm: debug: vmexec ** RAISING ERROR %" PRId64
        " (%s) in func %" PRId64 " at offset %" PRId64 "\n",
        class_id, buf, func_id,
        (int64_t)offset
    );
    h64fprintf(stderr,
        "horsevm: debug: vmexec ** stack total entries: %" PRId64
        ", func stack bottom: %" PRId64 "\n",
        vmthread->stack->entry_count,
        vmthread->stack->current_func_floor
    );
    h64fprintf(stderr,
        "horsevm: debug: vmexec ** func frame count: %d\n",
        vmthread->funcframe_count
    );
}

static void vmexec_PrintPostErrorInfo(
        ATTR_UNUSED h64vmthread *vmthread, ATTR_UNUSED int64_t class_id,
        int64_t func_id, int64_t offset
        ) {
    h64fprintf(stderr,
        "horsevm: debug: vmexec ** ERROR raised, resuming. (it was in "
        " in func %" PRId64 " at offset %" PRId64 ")\n",
        func_id,
        (int64_t)offset
    );
}

// RAISE_ERROR is a shortcut to handle raising an error.
// It was made for use inside vmthread_RunFunction, its signature is:
// (int64_t class_id, const char *msg, ...args for msg's formatters...)
#define RAISE_ERROR_EX(class_id, is_u32, ...) \
    {\
    ptrdiff_t offset = (p - pr->func[func_id].instructions);\
    int returneduncaught = 0; \
    h64errorinfo uncaughterror = {0}; \
    uncaughterror.error_class_id = -1; \
    if (CAN_PREERROR_PRINT_INFO &&\
            vmexec->moptions.vmexec_debug) {\
        vmexec_PrintPreErrorInfo(\
            vmthread, class_id, func_id,\
            (p - pr->func[func_id].instructions)\
        );\
    }\
    int raiseresult = vmthread_errors_Raise( \
        vmthread, class_id, \
        &func_id, &offset, \
        (class_id != H64STDERROR_OUTOFMEMORYERROR), \
        &returneduncaught, \
        &uncaughterror, is_u32, __VA_ARGS__ \
    );\
    if (!raiseresult && class_id != H64STDERROR_OUTOFMEMORYERROR) {\
        memset(&uncaughterror, 0, sizeof(uncaughterror));\
        uncaughterror.error_class_id = -1; \
        raiseresult = vmthread_errors_Raise( \
            vmthread, H64STDERROR_OUTOFMEMORYERROR, \
            &func_id, &offset, 0, \
            &returneduncaught, \
            &uncaughterror, is_u32, "Allocation failure" \
        );\
    }\
    if (!raiseresult) {\
        h64fprintf(stderr, "Out of memory raising OutOfMemoryError.\n");\
        _exit(1);\
        return 0;\
    }\
    if (returneduncaught) {\
        assert(uncaughterror.error_class_id >= 0 &&\
               "vmthread_errors_Raise must set uncaughterror"); \
        *returneduncaughterror = 1;\
        memcpy(einfo, &uncaughterror, sizeof(uncaughterror));\
        einfo->refcount = 0;\
        return 1;\
    }\
    if (CAN_PREERROR_PRINT_INFO &&\
            vmthread->vmexec_owner->moptions.vmexec_debug) {\
        vmexec_PrintPostErrorInfo(\
            vmthread, class_id, func_id, offset\
        );\
    }\
    assert(pr->func[func_id].instructions != NULL);\
    p = (pr->func[func_id].instructions + offset);\
    }

// RAISE_ERROR is a shortcut to handle raising an error.
// It was made for use inside vmthread_RunFunction, its signature is:
// (int64_t class_id, const char *msg, ...args for msg's formatters...)
#define RAISE_ERROR(class_id, ...)\
    RAISE_ERROR_EX(class_id, 0, __VA_ARGS__)

#define RAISE_ERROR_U32(class_id, msg, msglen) \
    RAISE_ERROR_EX(class_id, 1, (const char *)msg, (int)msglen)

// Macro for suspending inside _vmthread_RunFunction_NoPopFuncFrames:
#define SUSPEND_VM(suspend_valuecontent) \
    /* Handle function suspend: */ \
    if (returneduncaughterror) \
        *returneduncaughterror = 0; \
    if (returnedsuspend) \
        *returnedsuspend = 1; \
    if (suspend_info) { \
        /* NOTE: can't directly copy to */ \
        /* vmhread->suspend_info here, because */ \
        /* we don't have the worker_overview */ \
        /* mutex. */ \
        memset( \
            suspend_info, 0, \
            sizeof(*suspend_info) \
        ); \
        suspend_info->suspendtype = ( \
            suspend_valuecontent->suspend_type \
        ); \
        suspend_info->suspendarg = ( \
            suspend_valuecontent->suspend_intarg \
        ); \
    } \
    vmthread->upcoming_resume_info->byteoffset = ( \
        p - pr->func[func_id].instructions \
    ); \
    vmthread->upcoming_resume_info->func_id = func_id; \
    vmthread->upcoming_resume_info->funcnestdepth = funcnestdepth; \
    DELREF_NONHEAP(suspend_valuecontent); \
    valuecontent_Free(suspend_valuecontent); \
    if (vmexec->moptions.vmscheduler_debug) \
        h64fprintf( \
            stderr, "horsevm: debug: vmschedule.c: " \
            "[t%p:%s:W%d] SUSPEND in func %" PRId64 \
            " (stack floor: %" PRId64 ", total: %" PRId64\
            ", call_settop_reverse: %" PRId64 ")\n", \
            start_thread,\
            (start_thread->is_on_main_thread ?\
             "nonparallel" : "parallel"),\
            worker_no,\
            (int64_t)func_id,\
            start_thread->stack->current_func_floor,\
            start_thread->stack->entry_count,\
            start_thread->call_settop_reverse\
        );

int _vmthread_RunFunction_NoPopFuncFrames(
        h64vmexec *vmexec, h64vmthread *start_thread,
        vmthreadresumeinfo *rinfo, int worker_no,
        int *returneduncaughterror,
        h64errorinfo *einfo,
        int *returnedsuspend,
        vmthreadsuspendinfo *suspend_info
        ) {
    if (!vmexec || !start_thread || !einfo)
        return 0;
    h64program *pr = vmexec->program;

    int64_t func_id = rinfo->func_id;
    #ifndef NDEBUG
    if (vmexec->moptions.vmexec_debug)
        h64fprintf(
            stderr, "horsevm: debug: vmexec [t%p:%s:W%d] "
            "call C->h64 func %" PRId64 "\n",
            start_thread,
            (start_thread->is_on_main_thread ?
             "nonparallel" : "parallel"),
            (int)worker_no, func_id
        );
    #endif
    int isresume = 0;
    if (!rinfo->run_from_start) {
        isresume = 1;
        #ifndef NDEBUG
        if (vmexec->moptions.vmscheduler_debug)
            h64fprintf(
                stderr, "horsevm: debug: vmexec.c: "
                "[t%p:%s] %s in func %" PRId64 "\n",
                start_thread,
                (start_thread->is_on_main_thread ?
                 "nonparallel" : "parallel"),
                (rinfo->run_from_start ?
                 "ASYNCCALL" : "RESUME"),
                (int64_t)func_id
            );
        #endif
        assert(start_thread->funcframe_count > 0);
    } else {
        // Must have a clean function frame stack:
        assert(start_thread->funcframe_count == 0);
    }
    assert(func_id >= 0 && func_id < pr->func_count);
    assert(!pr->func[func_id].iscfunc);
    char *p = pr->func[func_id].instructions;
    if (isresume)
        p += rinfo->byteoffset;
    char *pend = p + (intptr_t)pr->func[func_id].instructions_bytes;
    void *jumptable[H64INST_TOTAL_COUNT];
    void *op_jumptable[TOTAL_OP_COUNT];
    memset(op_jumptable, 0, sizeof(*op_jumptable) * TOTAL_OP_COUNT);
    h64stack *stack = start_thread->stack;
    poolalloc *heap = start_thread->heap;
    int64_t original_stack_size = (
        start_thread->upcoming_resume_info->precall_old_stack
        // NOTE: we do NOT want to get this from rinfo, since rinfo
        // is -1 on non-resume. Instead, this value will be set even when
        // everything else is -1'ed for future resumes by our caller.
    );
    assert(original_stack_size >= 0);
    #ifndef NDEBUG
    if (!isresume) {
        // This should have been set by our caller:
        assert(
            (int64_t)(stack->entry_count - pr->func[func_id].input_stack_size)
            == start_thread->upcoming_resume_info->precall_old_stack
        );
    }
    #endif
    int funcnestdepth = 0;
    if (isresume) {
        funcnestdepth = rinfo->funcnestdepth;
    } else {
        stack->current_func_floor = original_stack_size;
        assert(rinfo->funcnestdepth <= 0);
    }
    #ifndef NDEBUG
    if (vmexec->moptions.vmexec_debug) {
        char nd[32];
        snprintf(nd, sizeof(nd) - 1, "%d", funcnestdepth);
        char stackd[32];
        snprintf(stackd, sizeof(stackd) - 1, "%" PRId64,
                 stack->entry_count);
        h64fprintf(
            stderr, "horsevm: debug: vmexec %s "
            "C->h64 has stack floor %" PRId64 "%s%s%s%s\n",
            (isresume ? "resume" : "call"),
            stack->current_func_floor,
            (isresume ? " with resume nest depth " : ""),
            (isresume ? nd : ""),
            (isresume ? "/stack total " : ""),
            (isresume ? stackd : "")
        );
    }
    #endif
    #ifndef NDEBUG
    if (isresume) {
        if (!(stack->current_func_floor +
                pr->func[func_id].inner_stack_size +
                pr->func[func_id].input_stack_size <=
                stack->entry_count ||
                start_thread->call_settop_reverse >= 0))
            h64fprintf(
                stderr, "horsevm: debug: vmexec.c: [t%p] "
                "ERROR, STACK TOO SMALL ON RESUME: "
                "floor %" PRId64 " total %" PRId64
                " input+inner=%" PRId64 "+%" PRId64
                "\n",
                start_thread,
                stack->current_func_floor,
                stack->entry_count,
                pr->func[func_id].input_stack_size,
                pr->func[func_id].inner_stack_size
            );
        assert(
            stack->current_func_floor +
            pr->func[func_id].inner_stack_size +
            pr->func[func_id].input_stack_size <=
            stack->entry_count ||
            start_thread->call_settop_reverse >= 0
        );
    }
    #endif
    h64vmthread *vmthread = start_thread;
    vmexec->active_thread = vmthread;
    int callignoreifnone = 0;
    classid_t _raise_error_class_id = -1;
    int32_t _raise_msg_stack_slot = -1;

    goto setupinterpreter;

    inst_invalid: {
        h64fprintf(stderr, "invalid instruction\n");
        return 0;
    }
    triggeroom: {
        #if defined(DEBUGVMEXEC)
        h64fprintf(stderr, "horsevm: debug: vmexec triggeroom\n");
        #endif
        vmthread_ClearAsyncForegroundWork(vmthread);
        RAISE_ERROR(H64STDERROR_OUTOFMEMORYERROR,
                        "Allocation failure");
        goto *jumptable[((h64instructionany *)p)->type];
    }
    inst_setconst: {
        h64instruction_setconst *inst = (h64instruction_setconst *)p;
        #ifndef NDEBUG
        if (vmthread->vmexec_owner->moptions.vmexec_debug &&
                !vmthread_PrintExec(vmthread, func_id, (void*)inst))
            goto triggeroom;
        #endif

        #ifndef NDEBUG
        if (stack != NULL && !(inst->slot >= 0 &&
                inst->slot < stack->entry_count -
                stack->current_func_floor &&
                stack->alloc_count >= stack->entry_count)) {
            h64fprintf(
                stderr, "horsevm: error: "
                "invalid setconst outside of stack, "
                "stack func floor: %" PRId64
                ", stack total size: %" PRId64
                ", setconst target stack slot: %d\n",
                (int64_t)stack->current_func_floor,
                (int64_t)STACK_TOTALSIZE(stack),
                (int)inst->slot
            );
            stack_PrintDebug(stack);
        }
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
                inst->content.constpreallocstr_len * sizeof(h64wchar)
            );
        } else if (inst->content.type == H64VALTYPE_CONSTPREALLOCBYTES) {
            vc->type = H64VALTYPE_GCVAL;
            vc->ptr_value = poolalloc_malloc(
                heap, 0
            );
            if (!vc->ptr_value)
                goto triggeroom;
            h64gcvalue *gcval = (h64gcvalue *)vc->ptr_value;
            gcval->type = H64GCVALUETYPE_BYTES;
            gcval->heapreferencecount = 0;
            gcval->externalreferencecount = 1;
            memset(&gcval->bytes_val, 0, sizeof(gcval->bytes_val));
            if (!vmbytes_AllocBuffer(
                    vmthread, &gcval->bytes_val,
                    inst->content.constpreallocbytes_len)) {
                poolalloc_free(heap, gcval);
                vc->ptr_value = NULL;
                goto triggeroom;
            }
            memcpy(
                gcval->bytes_val.s,
                inst->content.constpreallocbytes_value,
                inst->content.constpreallocbytes_len
            );
        } else {
            memcpy(vc, &inst->content, sizeof(*vc));
            if (vc->type == H64VALTYPE_GCVAL)
                ((h64gcvalue *)vc->ptr_value)->
                    externalreferencecount = 1;
        }
        assert(vc->type != H64VALTYPE_CONSTPREALLOCBYTES &&
               vc->type != H64VALTYPE_CONSTPREALLOCSTR);
        p += sizeof(h64instruction_setconst);
        goto *jumptable[((h64instructionany *)p)->type];
    }
    inst_setglobal: {
        h64instruction_setglobal *inst = (
            (h64instruction_setglobal *)p
        );
        #ifndef NDEBUG
        if (vmthread->vmexec_owner->moptions.vmexec_debug &&
                !vmthread_PrintExec(vmthread, func_id, (void*)inst))
            goto triggeroom;
        #endif

        if (!vmthread->is_on_main_thread) {
            RAISE_ERROR(
                H64STDERROR_INVALIDNOASYNCRESOURCEERROR,
                "cannot access globals from nonparallel function"
            );
            goto *jumptable[((h64instructionany *)p)->type];
        }

        valuecontent *vcfrom = STACK_ENTRY(
            stack, inst->slotfrom
        );
        valuecontent *vcto = &(
            pr->globalvar[inst->globalto].content
        );
        memcpy(vcto, vcfrom, sizeof(*vcto));

        p += sizeof(*inst);
        goto *jumptable[((h64instructionany *)p)->type];
    }
    inst_getglobal: {
        h64instruction_getglobal *inst = (
            (h64instruction_getglobal *)p
        );
        #ifndef NDEBUG
        if (vmthread->vmexec_owner->moptions.vmexec_debug &&
                !vmthread_PrintExec(vmthread, func_id, (void*)inst))
            goto triggeroom;
        #endif

        if (!vmthread->is_on_main_thread &&
                !pr->globalvar[inst->globalfrom].is_simple_constant) {
            RAISE_ERROR(
                H64STDERROR_INVALIDNOASYNCRESOURCEERROR,
                "cannot access globals from nonparallel function"
            );
            goto *jumptable[((h64instructionany *)p)->type];
        }

        valuecontent *vcfrom = &(
            pr->globalvar[inst->globalfrom].content
        );
        valuecontent *vcto = STACK_ENTRY(
            stack, inst->slotto
        );
        memcpy(vcto, vcfrom, sizeof(*vcto));

        p += sizeof(*inst);
        goto *jumptable[((h64instructionany *)p)->type];
    }
    inst_setbyindexexpr: {
        h64instruction_setbyindexexpr *inst = (
            (h64instruction_setbyindexexpr *)p
        );
        #ifndef NDEBUG
        if (vmthread->vmexec_owner->moptions.vmexec_debug &&
                !vmthread_PrintExec(vmthread, func_id, (void*)inst))
            goto triggeroom;
        #endif

        valuecontent *vc = STACK_ENTRY(stack, inst->slotobjto);
        valuecontent *vcset = STACK_ENTRY(stack, inst->slotvaluefrom);
        assert(vc->type != H64VALTYPE_CONSTPREALLOCSTR &&
               vc->type != H64VALTYPE_CONSTPREALLOCBYTES);
        valuecontent *vcindex = STACK_ENTRY(stack, inst->slotindexto);
        if (unlikely(vcindex->type != H64VALTYPE_FLOAT64 &&
                vcindex->type != H64VALTYPE_INT64)) {
            RAISE_ERROR(
                H64STDERROR_TYPEERROR,
                "indexing value must be a number"
            );
            goto *jumptable[((h64instructionany *)p)->type];
        }
        int64_t index_value = -1;
        if (likely(vcindex->type == H64VALTYPE_INT64)) {
            index_value = vcindex->int_value;
        } else {
            assert(vcindex->type == H64VALTYPE_FLOAT64);
            int64_t rounded_value = (
                (int64_t)roundl(vcindex->float_value)
            );
            if (fabs(vcindex->float_value - round(rounded_value)) > 0.001) {
                RAISE_ERROR(
                    H64STDERROR_INDEXERROR,
                    "index must not have fractional part"
                );
                goto *jumptable[((h64instructionany *)p)->type];
            }
            index_value = rounded_value;
        }
        if (vc->type == H64VALTYPE_GCVAL &&
                ((h64gcvalue*)vc->ptr_value)->type ==
                H64GCVALUETYPE_LIST) {
            h64gcvalue *gcval = ((h64gcvalue*)vc->ptr_value);
            if (index_value < 1 || index_value >
                    gcval->list_values->list_total_entry_count + 1) {
                RAISE_ERROR(
                    H64STDERROR_INDEXERROR,
                    "index outside of range"
                );
                goto *jumptable[((h64instructionany *)p)->type];
            }
            assert(vcset->type != H64VALTYPE_CONSTPREALLOCSTR &&
                   vcset->type != H64VALTYPE_CONSTPREALLOCBYTES);
            // Note: vmlist_Set handles the appending case of
            // index_value == list_total_entry_count + 1 already.
            if (!vmlist_Set(
                    gcval->list_values,
                    index_value,
                    vcset))
                goto triggeroom;
        } else if ((vc->type == H64VALTYPE_GCVAL &&
                ((h64gcvalue*)vc->ptr_value)->type ==
                H64GCVALUETYPE_STRING) ||
                vc->type == H64VALTYPE_SHORTSTR) {
            int64_t assignto_len = -1;
            char *assignto_buf = NULL;
            if (vc->type == H64VALTYPE_SHORTSTR) {
                assignto_len = vc->shortstr_len;
                assignto_buf = (char *)vc->shortstr_value;
                    // ^ char* since unaligned
            } else {
                h64gcvalue *gcval = ((h64gcvalue*)vc->ptr_value);
                assignto_len = gcval->str_val.len;
                assignto_buf = (char *)gcval->str_val.s;
            }
            if (index_value < 1 || index_value > assignto_len) {
                RAISE_ERROR(
                    H64STDERROR_INDEXERROR,
                    "index outside of range"
                );
                goto *jumptable[((h64instructionany *)p)->type];
            }
            assert(assignto_buf != NULL && assignto_len > 0);
            h64wchar setchar;
            if (likely(vcset->type == H64VALTYPE_SHORTSTR &&
                    vcset->shortstr_len == 1)) {
                setchar = vcset->shortstr_value[0];
            } else if (likely(vc->type == H64VALTYPE_GCVAL &&
                    ((h64gcvalue *)vc->ptr_value)->type ==
                        H64GCVALUETYPE_STRING &&
                    ((h64gcvalue *)vc->ptr_value)->str_val.len == 1)) {
                setchar = (
                    ((h64gcvalue *)vc->ptr_value)->str_val.s[0]
                );
            } else {
                RAISE_ERROR(
                    H64STDERROR_TYPEERROR,
                    "can only assign single character string "
                    "when indexing a string"
                );
                goto *jumptable[((h64instructionany *)p)->type];
            }
            assert(index_value - 1 >= 0 && index_value - 1 < assignto_len);
            memcpy(
                assignto_buf + (index_value - 1) * sizeof(h64wchar),
                &setchar, sizeof(h64wchar)
            );
        } else {
            RAISE_ERROR(
                H64STDERROR_TYPEERROR,
                "given value cannot be indexed"
            );
            goto *jumptable[((h64instructionany *)p)->type];
        }
        p += sizeof(h64instruction_setbyindexexpr);
        goto *jumptable[((h64instructionany *)p)->type];
    }
    inst_setbyattributename: {
        h64instruction_setbyattributename *inst = (
            (h64instruction_setbyattributename *)p
        );
        #ifndef NDEBUG
        if (vmthread->vmexec_owner->moptions.vmexec_debug &&
                !vmthread_PrintExec(vmthread, func_id, (void*)inst))
            goto triggeroom;
        #endif

        valuecontent *vc = STACK_ENTRY(stack, inst->slotobjto);
        if (vc->type != H64VALTYPE_GCVAL ||
                ((h64gcvalue*)vc->ptr_value)->type !=
                H64GCVALUETYPE_OBJINSTANCE) {
            RAISE_ERROR(
                H64STDERROR_ATTRIBUTEERROR,
                "given attribute not present on this value"
            );
            goto *jumptable[((h64instructionany *)p)->type];
        }
        valuecontent *vfrom = STACK_ENTRY(stack, inst->slotvaluefrom);

        h64gcvalue *gcval = (h64gcvalue *)vc->ptr_value;
        attridx_t aindex = h64program_LookupClassAttribute(
            pr, gcval->class_id, inst->nameidx
        );
        if (aindex < 0) {
            RAISE_ERROR(
                H64STDERROR_ATTRIBUTEERROR,
                "given attribute not present on this value"
            );
            goto *jumptable[((h64instructionany *)p)->type];
        }
        if (aindex >= H64CLASS_METHOD_OFFSET) {
            RAISE_ERROR(
                H64STDERROR_ATTRIBUTEERROR,
                "cannot alter func attribute"
            );
            goto *jumptable[((h64instructionany *)p)->type];
        }
        assert(aindex >= 0 && aindex < gcval->varattr_count);

        DELREF_NONHEAP(&gcval->varattr[aindex]);
        valuecontent_Free(&gcval->varattr[aindex]);
        memcpy(
            &gcval->varattr[aindex], vfrom, sizeof(*vfrom)
        );
        ADDREF_NONHEAP(&gcval->varattr[aindex]);

        p += sizeof(*inst);
        goto *jumptable[((h64instructionany *)p)->type];
    }
    inst_setbyattributeidx: {
        h64instruction_setbyattributeidx *inst = (
            (h64instruction_setbyattributeidx *)p
        );
        #ifndef NDEBUG
        if (vmthread->vmexec_owner->moptions.vmexec_debug &&
                !vmthread_PrintExec(vmthread, func_id, (void*)inst))
            goto triggeroom;
        #endif

        valuecontent *vc = STACK_ENTRY(stack, inst->slotobjto);
        if (vc->type != H64VALTYPE_GCVAL ||
                ((h64gcvalue*)vc->ptr_value)->type !=
                H64GCVALUETYPE_OBJINSTANCE) {
            RAISE_ERROR(
                H64STDERROR_ATTRIBUTEERROR,
                "given attribute not present on this value"
            );
            goto *jumptable[((h64instructionany *)p)->type];
        }
        valuecontent *vfrom = STACK_ENTRY(stack, inst->slotvaluefrom);

        h64gcvalue *gcval = (h64gcvalue *)vc->ptr_value;
        attridx_t aindex = inst->varattrto;
        if (aindex < 0 || aindex >= gcval->varattr_count) {
            RAISE_ERROR(
                H64STDERROR_ATTRIBUTEERROR,
                "given attribute not present on this value"
            );
            goto *jumptable[((h64instructionany *)p)->type];
        }

        DELREF_NONHEAP(&gcval->varattr[aindex]);
        valuecontent_Free(&gcval->varattr[aindex]);
        memcpy(
            &gcval->varattr[aindex], vfrom, sizeof(*vfrom)
        );
        ADDREF_NONHEAP(&gcval->varattr[aindex]);

        p += sizeof(*inst);
        goto *jumptable[((h64instructionany *)p)->type];
    }
    inst_getfunc: {
        h64instruction_getfunc *inst = (h64instruction_getfunc *)p;
        #ifndef NDEBUG
        if (vmthread->vmexec_owner->moptions.vmexec_debug &&
                !vmthread_PrintExec(vmthread, func_id, (void*)inst))
            goto triggeroom;
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
        if (vmthread->vmexec_owner->moptions.vmexec_debug &&
                !vmthread_PrintExec(vmthread, func_id, (void*)inst))
            goto triggeroom;
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
        if (vmthread->vmexec_owner->moptions.vmexec_debug &&
                !vmthread_PrintExec(vmthread, func_id, (void*)inst))
            goto triggeroom;
        #endif

        assert(STACK_ENTRY(stack, inst->slotfrom)->type !=
               H64VALTYPE_CONSTPREALLOCSTR &&
               STACK_ENTRY(stack, inst->slotfrom)->type !=
               H64VALTYPE_CONSTPREALLOCBYTES);

        if (inst->slotto != inst->slotfrom) {
            #ifndef NDEBUG
            assert(inst->slotto < STACK_TOP(stack));
            assert(inst->slotfrom < STACK_TOP(stack));
            #endif
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
    #include "vmexec_inst_unopbinop_INCLUDE.c"
    inst_call: {
        callignoreifnone = 0;
        goto sharedending_call_callignoreifnone;
    }
    inst_callignoreifnone: {
        callignoreifnone = 1;
        goto sharedending_call_callignoreifnone;
    }
    sharedending_call_callignoreifnone: {
        h64instruction_call *inst = (h64instruction_call *)p;
        assert(sizeof(h64instruction_call) ==
               sizeof(h64instruction_callignoreifnone));
        #ifndef NDEBUG
        if (vmthread->vmexec_owner->moptions.vmexec_debug) {
            if (!vmthread_PrintExec(vmthread, func_id, (void*)inst)) {
                if (!vmthread_ResetCallTempStack(vmthread)) {
                    if (returneduncaughterror)
                        *returneduncaughterror = 0;
                    return 0;
                }
                goto triggeroom;
            }
        }
        #endif

        // IMPORTANT: if no callsettop was used, we must return to
        // current stack size post call:
        if (vmthread->call_settop_reverse < 0) {
            vmthread->call_settop_reverse = (
                STACK_TOP(stack)
            );
        }

        int64_t stacktop = STACK_TOP(stack);
        valuecontent *vc = STACK_ENTRY(stack, inst->slotcalledfrom);

        // Check if what we're calling is callable:
        if (unlikely(
                vc->type != H64VALTYPE_FUNCREF && (
                vc->type != H64VALTYPE_GCVAL ||
                ((h64gcvalue *)vc->ptr_value)->type !=
                H64GCVALUETYPE_FUNCREF_CLOSURE ||
                ((h64gcvalue *)vc->ptr_value)->closure_info == NULL
                ))) {
            if (!vmthread_ResetCallTempStack(vmthread)) {
                if (returneduncaughterror)
                    *returneduncaughterror = 0;
                return 0;
            }
            if (!callignoreifnone || vc->type != H64VALTYPE_NONE) {
                RAISE_ERROR(H64STDERROR_TYPEERROR,
                            "not a callable object type");
                goto *jumptable[((h64instructionany *)p)->type];
            }
            valuecontent *vcreturnto = STACK_ENTRY(stack, inst->returnto);
            DELREF_NONHEAP(vcreturnto);
            valuecontent_Free(vcreturnto);
            memset(vcreturnto, 0, sizeof(*vcreturnto));
            vcreturnto->type = H64VALTYPE_NONE;
            p += sizeof(h64instruction_call);
            goto *jumptable[((h64instructionany *)p)->type];
        }

        // Extract more info about what we're calling:
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
        if (unlikely(inst->flags & CALLFLAG_UNPACKLASTPOSARG)) {
            effective_posarg_count--;
            valuecontent *lastposarg = (
                STACK_ENTRY(stack, stacktop - 1 - inst->kwargs)
            );
            if (lastposarg->type != H64VALTYPE_GCVAL ||
                    ((h64gcvalue *)vc->ptr_value)->type !=
                    H64GCVALUETYPE_LIST) {
                if (!vmthread_ResetCallTempStack(vmthread)) {
                    if (returneduncaughterror)
                        *returneduncaughterror = 0;
                    return 0;
                }
                RAISE_ERROR(
                    H64STDERROR_TYPEERROR,
                    "unpack parameter must be a list"
                );
                goto *jumptable[((h64instructionany *)p)->type];
            }
            effective_posarg_count += (
                vmlist_Count(((h64gcvalue *)vc->ptr_value)->list_values)
            );
        }
        int closure_arg_count = (
            cinfo ? (cinfo->closure_self ? 1 : 0) +
            cinfo->closure_vbox_count :
            0);
        int func_posargs = pr->func[target_func_id].input_stack_size - (
            pr->func[target_func_id].kwarg_count +
            closure_arg_count
        );
        assert(func_posargs >= 0);
        if (unlikely(effective_posarg_count != func_posargs)) {
            if (effective_posarg_count < func_posargs) {
                if (!vmthread_ResetCallTempStack(vmthread)) {
                    if (returneduncaughterror)
                        *returneduncaughterror = 0;
                    return 0;
                }
                RAISE_ERROR(
                    H64STDERROR_ARGUMENTERROR,
                    "called func requires more positional arguments"
                );
                goto *jumptable[((h64instructionany *)p)->type];
            } else {
                if (!vmthread_ResetCallTempStack(vmthread)) {
                    if (returneduncaughterror)
                        *returneduncaughterror = 0;
                    return 0;
                }
                RAISE_ERROR(
                    H64STDERROR_ARGUMENTERROR,
                    "called func requires less positional arguments"
                );
                goto *jumptable[((h64instructionany *)p)->type];
            }
        }

        // See where stack part we care about starts:
        int64_t stack_args_bottom = (
            stacktop - inst->posargs - inst->kwargs * 2
        );
        assert(stack->current_func_floor >= 0);
        assert(stack_args_bottom >= 0);

        // Make sure keyword arguments are actually known to target,
        // and do assert()s that keyword args are sorted:
        {
            if (unlikely(vmthread->kwarg_index_track_count <
                    pr->func[target_func_id].kwarg_count)) {
                int oldcount = vmthread->kwarg_index_track_count;
                int32_t *new_track_slots = realloc(
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
                    0, sizeof(int32_t) *
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
                    if (!vmthread_ResetCallTempStack(vmthread)) {
                        if (returneduncaughterror)
                            *returneduncaughterror = 0;
                        return 0;
                    }
                    RAISE_ERROR(
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
        const int _unpacklastposarg = (
            inst->flags & CALLFLAG_UNPACKLASTPOSARG
        );
        const int noargreorder = (likely(
            !_unpacklastposarg &&
            inst->posargs == func_posargs &&
            inst->kwargs ==
                pr->func[target_func_id].kwarg_count
        ));

        // See how many positional args we can definitely leave on the
        // stack as-is:
        int leftalone_args = inst->posargs + inst->kwargs;
        int reformat_argslots = 0;
        int reformat_slots_used = 0;
        const int inst_posargs = inst->posargs;
        if (unlikely(!noargreorder)) {
            // Ok, so we need to copy out part of the stack to reorder it.
            // Compute what slots exactly we need to shift around:
            leftalone_args = func_posargs;
            if (inst->posargs - (
                    _unpacklastposarg ? 1 : 0) <
                    leftalone_args)
                leftalone_args = inst->posargs -
                                 (_unpacklastposarg ? 1 : 0);
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
                if (!new_space) {
                    if (!vmthread_ResetCallTempStack(vmthread)) {
                        if (returneduncaughterror)
                            *returneduncaughterror = 0;
                        return 0;
                    }
                    goto triggeroom;
                }
                vmthread->arg_reorder_space = new_space;
                vmthread->arg_reorder_space_count = reformat_argslots;
            }
            assert(vmthread->arg_reorder_space != NULL);

            // Clear out stack above where we may need to reorder:
            // First, copy out positional args:
            reformat_slots_used = 0;
            int i = leftalone_args;
            assert(i >= 0);
            while (i < inst->posargs) {
                if (i == inst->posargs - 1 && _unpacklastposarg) {
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
                    assert(vmthread->arg_reorder_space != NULL);
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
                valuecontent *kwarg_value = (
                     STACK_ENTRY(
                        stack, (int64_t)i * 2 + // * 2 for nameidx, value pairs
                        1 + // -> we want the value, but not the nameidx
                        (inst->posargs + stack_args_bottom) // base offset
                    )
                );
                memcpy(
                    &vmthread->arg_reorder_space[
                        temp_slots_kwarg_start + target_slot
                    ], kwarg_value, sizeof(valuecontent)
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
        } else if (unlikely(inst->kwargs > 0)) {
            // No re-order, but gotta strip out kw arg name indexes.
            int64_t base = (
                stack_args_bottom + inst->posargs +
                stack->current_func_floor
            );
            int64_t top = stack->entry_count;
            int64_t i = 0;
            while (i < inst->kwargs) {
                DELREF_NONHEAP(&stack->entry[base + i]);
                valuecontent_Free(&stack->entry[base + i]);
                memmove(
                    &stack->entry[base + i],
                    &stack->entry[base + i + 1],
                    top - (i + base) - 1
                );
                stack->entry_count--;
                i++;
            }
        }
        // Ok, now we resize the stack to be what is actually needed:
        {
            // Increase to total amount required for target func:
            int result = 1;
            if (stack_args_bottom +
                    pr->func[target_func_id].input_stack_size +
                    pr->func[target_func_id].inner_stack_size +
                    stack->current_func_floor > STACK_TOTALSIZE(stack)
                    ) {
                result = stack_ToSize(
                    stack, stack_args_bottom +
                    pr->func[target_func_id].input_stack_size +
                    pr->func[target_func_id].inner_stack_size +
                    stack->current_func_floor, 0
                );
            }
            if (unlikely(!result)) {
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
                if (!vmthread_ResetCallTempStack(vmthread)) {
                    if (returneduncaughterror)
                        *returneduncaughterror = 0;
                    return 0;
                }
                goto triggeroom;
            }
        }
        // Copy in stuff from our previous temporary reorder & closure args:
        if (unlikely(!noargreorder || closure_arg_count > 0)) {
            // Place reordered positional args on stack as needed:
            if (unlikely(!noargreorder)) {
                int stackslot = stack_args_bottom + leftalone_args;
                int reorderslot = 0;
                int posarg = leftalone_args;
                while (posarg < func_posargs) {
                    assert(posarg < inst_posargs || _unpacklastposarg);
                    assert(
                        stackslot - (stack_args_bottom) < func_posargs
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
                assert(posarg == func_posargs && posarg >= inst_posargs);
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

            // Add closure args on top:
            int i = stack_args_bottom + (
                func_posargs +
                pr->func[target_func_id].kwarg_count
            );
            if (cinfo && cinfo->closure_self) {
                // Add self argument:
                assert(STACK_ENTRY(stack, i)->type == H64VALTYPE_NONE);
                valuecontent *closurearg = (
                    STACK_ENTRY(stack, i)
                );
                closurearg->type = H64VALTYPE_GCVAL;
                closurearg->ptr_value =
                    poolalloc_malloc(
                        heap, 0
                    );
                if (!closurearg->ptr_value) {
                    if (!vmthread_ResetCallTempStack(vmthread)) {
                        if (returneduncaughterror)
                            *returneduncaughterror = 0;
                        return 0;
                    }
                    goto triggeroom;
                }
                memcpy(
                    closurearg->ptr_value,
                    cinfo->closure_self,
                    sizeof(*cinfo->closure_self)
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
                if (!closurearg->ptr_value) {
                    if (!vmthread_ResetCallTempStack(vmthread)) {
                        if (returneduncaughterror)
                            *returneduncaughterror = 0;
                        return 0;
                    }
                    goto triggeroom;
                }
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
            // Prepare call:
            int (*cfunc)(h64vmthread *vmthread) = (
                (int (*)(h64vmthread *vmthread))
                pr->func[target_func_id].cfunc_ptr
            );
            assert(cfunc != NULL);
            int64_t old_floor = stack->current_func_floor;
            stack->current_func_floor = new_func_floor;
            #ifndef NDEBUG
            if (vmthread->vmexec_owner->moptions.vmexec_debug)
                h64fprintf(
                    stderr, "horsevm: debug: vmexec jump into cfunc "
                    "%" PRId64 "/addr=%p with floor %" PRId64
                    " (via call)\n",
                    target_func_id, cfunc, new_func_floor
                );
            #endif
            int64_t oldtop = vmthread->call_settop_reverse;

            // For calls with async progress, pre-allocate data if
            // required:
            if (vmthread->foreground_async_work_funcid != target_func_id) {
                vmthread_ClearAsyncForegroundWork(vmthread);
                vmthread->foreground_async_work_funcid = target_func_id;
            }
            assert(vmthread->foreground_async_work_funcid == target_func_id);
            if (!vmthread->foreground_async_work_dataptr &&
                    pr->func[target_func_id].async_progress_struct_size > 0) {
                if (!vmthread->cfunc_asyncdata_pile) {
                    vmthread->cfunc_asyncdata_pile = poolalloc_New(
                        CFUNC_ASYNCDATA_DEFAULTITEMSIZE
                    );
                    if (!vmthread->cfunc_asyncdata_pile) {
                        goto triggeroom;
                    }
                }
                if (pr->func[target_func_id].
                        async_progress_struct_size <=
                        CFUNC_ASYNCDATA_DEFAULTITEMSIZE) {
                    vmthread->foreground_async_work_dataptr = (
                        poolalloc_malloc(
                            vmthread->cfunc_asyncdata_pile, 0
                        )
                    );
                } else {
                    vmthread->foreground_async_work_dataptr = (
                        malloc(pr->func[target_func_id].
                            async_progress_struct_size));
                }
                if (!vmthread->foreground_async_work_dataptr)
                    goto triggeroom;
            }
            assert(
                pr->func[target_func_id].async_progress_struct_size > 0 ||
                vmthread->foreground_async_work_dataptr == NULL
            );

            // Call:
            int result = cfunc(vmthread);  // DO ACTUAL CALL

            // See if we have unfinished async work, post call:
            int unfinished_async_work = (
                vmthread->foreground_async_work_dataptr != NULL
            );
            assert(!unfinished_async_work || !result);

            // Extract return value:
            int64_t return_value_gslot = new_func_floor + 0LL;
            valuecontent retval = {0};
            if (return_value_gslot >= 0 &&
                    return_value_gslot < STACK_TOTALSIZE(stack)
                    ) {
                memcpy(&retval, &stack->entry[return_value_gslot],
                       sizeof(retval));
                ADDREF_NONHEAP(&retval);
                assert(
                    !result || (
                    retval.type < H64VALTYPE_TOTAL &&
                    retval.type != H64VALTYPE_UNSPECIFIED_KWARG &&
                    retval.type != H64VALTYPE_CONSTPREALLOCSTR &&
                    retval.type != H64VALTYPE_CONSTPREALLOCBYTES
                    )
                );
            }
            if (unfinished_async_work &&
                    retval.type != H64VALTYPE_SUSPENDINFO) {
                // Probably an error that prevented proper suspending,
                // -> clear async data
                vmthread_ClearAsyncForegroundWork(vmthread);
                unfinished_async_work = 0;
            }
            // Reset stack floor and size:
            if (!unfinished_async_work) {
                vmthread->call_settop_reverse = oldtop;
                assert(vmthread->stack == stack);
                stack->current_func_floor = old_floor;
                if (!vmthread_ResetCallTempStack(vmthread)) {
                    if (returneduncaughterror)
                        *returneduncaughterror = 0;
                    DELREF_NONHEAP(&retval);
                    valuecontent_Free(&retval);
                    goto triggeroom;
                }
            } else {
                #ifndef NDEBUG
                if (vmthread->vmexec_owner->moptions.vmscheduler_debug)
                    h64fprintf(
                        stderr, "horsevm: debug: vmschedule.c: "
                        "[t%p:%s] cfunc %" PRId64 " is ASYNC/UNFINISHED,"
                        " suspending with re-call\n",
                        vmthread,
                        (vmthread->is_on_main_thread ?
                         "nonparallel" : "parallel"),
                        target_func_id
                    );
                #endif
            }
            // Check if result is error, suspend, or regular return:
            if (!result) {
                if (retval.type == H64VALTYPE_SUSPENDINFO) {
                    // Handle suspend:
                    if (!unfinished_async_work) {
                        // Nothing unfinished, advance past call:
                        p += sizeof(h64instruction_call);
                    }
                    SUSPEND_VM((&retval));
                    return 1;
                }
                // Handle function call error
                vmthread_ClearAsyncForegroundWork(vmthread);
                valuecontent oome = {0};
                oome.type = H64VALTYPE_ERROR;
                oome.error_class_id = H64STDERROR_OUTOFMEMORYERROR;
                valuecontent *eobj = &oome;
                if (retval.type == H64VALTYPE_ERROR)
                    eobj = &retval;
                RAISE_ERROR_U32(
                    eobj->error_class_id,
                    (const char *)(
                        eobj->einfo ? eobj->einfo->msg : (h64wchar *)NULL
                    ),
                    (eobj->einfo ? (int)eobj->einfo->msglen : 0)
                );  // FIXME: carry over inner stack trace
                DELREF_NONHEAP(&retval);
                valuecontent_Free(&retval);
                goto *jumptable[((h64instructionany *)p)->type];
            } else {
                vmthread_ClearAsyncForegroundWork(vmthread);
                // Copy return value:
                if (return_value_gslot != stack->current_func_floor +
                        (int64_t)inst->returnto &&
                        inst->returnto >= 0) {
                    assert(inst->returnto < STACK_TOP(stack));
                    DELREF_NONHEAP(STACK_ENTRY(stack, inst->returnto));
                    valuecontent_Free(STACK_ENTRY(stack, inst->returnto));
                    memcpy(
                        STACK_ENTRY(stack, inst->returnto),
                        &retval,
                        sizeof(valuecontent)
                    );
                    ADDREF_NONHEAP(STACK_ENTRY(stack, inst->returnto));
                }
                DELREF_NONHEAP(&retval);
                valuecontent_Free(&retval);
            }
            p += sizeof(h64instruction_call);
            goto *jumptable[((h64instructionany *)p)->type];
        } else {
            if ((inst->flags & CALLFLAG_ASYNC) != 0) {
                // Async call. Need to set it up as separate execution
                // thread instead.
                if ((inst->flags & CALLFLAG_PARALLELASYNC) &&
                        !vmexec->program->func[target_func_id].
                        is_threadable) {
                    // Abort, this is not allowed (due to heap separation)
                    if (!vmthread_ResetCallTempStack(vmthread)) {
                        goto triggeroom;
                    }
                    RAISE_ERROR(
                        H64STDERROR_TYPEERROR,
                        "cannot call nonparallel func via async parallel"
                    );
                    goto *jumptable[((h64instructionany *)p)->type];
                }
                // Schedule call:
                int result = vmschedule_AsyncScheduleFunc(
                    vmexec, vmthread,
                    new_func_floor, target_func_id,
                    (inst->flags & CALLFLAG_PARALLELASYNC)
                );
                if (!result) {
                    vmthread_ResetCallTempStack(vmthread);
                    goto triggeroom;
                }
                // Reset stack size again:
                if (!vmthread_ResetCallTempStack(vmthread)) {
                    goto triggeroom;
                }
                // Advance past call:
                p += sizeof(h64instruction_call);
                goto *jumptable[((h64instructionany *)p)->type];
            }

            // Set execution to the new function:
            int64_t return_offset = (ptrdiff_t)(
                p - pr->func[func_id].instructions
            ) + sizeof(h64instruction_call);
            if (!pushfuncframe(vmthread, target_func_id,
                    inst->returnto, func_id, return_offset,
                    new_func_floor
                    )) {
                goto triggeroom;
            }
            funcnestdepth++;
            #ifndef NDEBUG
            if (vmthread->vmexec_owner->moptions.vmexec_debug)
                h64fprintf(
                    stderr, "horsevm: debug: vmexec jump into "
                    "h64 func %" PRId64 " (via call)\n",
                    (int64_t)target_func_id
                );
            #endif
            func_id = target_func_id;
            vmthread->call_settop_reverse = -1;
            p = pr->func[func_id].instructions;
            goto *jumptable[((h64instructionany *)p)->type];
        }
    }
    inst_settop: {
        h64instruction_settop *inst = (h64instruction_settop *)p;
        #ifndef NDEBUG
        if (vmthread->vmexec_owner->moptions.vmexec_debug &&
                !vmthread_PrintExec(vmthread, func_id, (void*)inst))
            goto triggeroom;
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
    inst_callsettop: {
        h64instruction_callsettop *inst = (h64instruction_callsettop *)p;
        #ifndef NDEBUG
        if (vmthread->vmexec_owner->moptions.vmexec_debug &&
                !vmthread_PrintExec(vmthread, func_id, (void*)inst))
            goto triggeroom;
        #endif

        if (!vmthread_ResetCallTempStack(vmthread)) {
            if (returneduncaughterror)
                *returneduncaughterror = 0;
            return 0;
        }

        assert(inst->topto >= 0);
        int64_t oldtop = stack->entry_count;
        int64_t newtop = (int64_t)inst->topto + stack->current_func_floor;
        if (newtop != stack->entry_count) {
            if (stack_ToSize(
                    stack, newtop, 0
                    ) < 0) {
                goto triggeroom;
            }
        }
        vmthread->call_settop_reverse = oldtop;

        p += sizeof(h64instruction_settop);
        goto *jumptable[((h64instructionany *)p)->type];
    }
    inst_returnvalue: {
        h64instruction_returnvalue *inst = (h64instruction_returnvalue *)p;
        #ifndef NDEBUG
        if (vmthread->vmexec_owner->moptions.vmexec_debug &&
                !vmthread_PrintExec(vmthread, func_id, (void*)inst))
            goto triggeroom;
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
            h64fprintf(
                stderr, "horsevm: error: "
                "stack total count %" PRId64 " before return, "
                "current func's input+inner stack %d+%d, "
                "unwound last function should return this to 0 "
                "but instead unrolling stack would result "
                "in size %" PRId64 "\n",
                (int64_t)stack->entry_count,
                (int)pr->func[func_id].input_stack_size,
                (int)pr->func[func_id].inner_stack_size,
                (int64_t)stack->entry_count -
                    (int64_t)current_stack_size
            );
        }
        #endif
        assert(stack->entry_count >= current_stack_size);
        funcnestdepth--;
        if (funcnestdepth <= 0) {
            int result = popfuncframe(
                vmthread, &vmthread->vmexec_owner->moptions, 1
            );
                    // ^ pop frame but leave stack!
            assert(result != 0);
            func_id = -1;
            assert(stack->entry_count - current_stack_size == 0);
            assert(original_stack_size >= 0);
            if (!stack_ToSize(
                    stack, original_stack_size + 1, 0
                    )) {
                DELREF_NONHEAP(&vccopy);

                // Need to "manually" raise error since we're outside of any
                // function at this point:
                if (returneduncaughterror)
                    *returneduncaughterror = 1;
                memset(einfo, 0, sizeof(*einfo));
                einfo->error_class_id = H64STDERROR_OUTOFMEMORYERROR;
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
            vmthread->funcframe[vmthread->funcframe_count - 1].
            return_slot
        );
        int returnfuncid = (
            vmthread->funcframe[vmthread->funcframe_count - 1].
            return_to_func_id
        );
        ptrdiff_t returnoffset = (
            vmthread->funcframe[vmthread->funcframe_count - 1].
            return_to_execution_offset
        );
        if (!popfuncframe(
                vmthread,
                &vmthread->vmexec_owner->moptions,
                0)) {
            // We cannot really recover from this.
            if (returneduncaughterror)
                *returneduncaughterror = 0;
            h64fprintf(
                stderr,
                "horsevm: error: unexpectedly failed to remove "
                "function frame - out of memory?\n"
            );
            return 0;
        }

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
        funcid_t oldfuncid = func_id;
        func_id = returnfuncid;
        assert(func_id >= 0);
        p = pr->func[func_id].instructions + returnoffset;
        pend = pr->func[func_id].instructions + (
            (ptrdiff_t)pr->func[func_id].instructions_bytes
        );
        #ifndef NDEBUG
        if (vmthread->vmexec_owner->moptions.vmexec_debug) {
            h64fprintf(
                stderr, "horsevm: debug: vmexec "
                "left func %" PRId64
                " -> to func %" PRId64 " with stack size %" PRId64 " "
                "(via return)\n",
                (int64_t)oldfuncid,
                (int64_t)func_id,
                (int64_t)STACK_TOTALSIZE(stack)
            );
        }
        #endif
        goto *jumptable[((h64instructionany *)p)->type];
    }
    inst_jumptarget: {
        h64fprintf(stderr, "jumptarget instruction "
            "not valid in final bytecode\n");
        return 0;
    }
    inst_condjump: {
        h64instruction_condjump *inst = (h64instruction_condjump *)p;
        #ifndef NDEBUG
        if (vmthread->vmexec_owner->moptions.vmexec_debug &&
                !vmthread_PrintExec(vmthread, func_id, (void*)inst))
            goto triggeroom;
        #endif

        int jumpevalvalue = 1;
        valuecontent *vc = STACK_ENTRY(stack, inst->conditionalslot);
        if (vc->type == H64VALTYPE_INT64 ||
                vc->type == H64VALTYPE_BOOL) {
            jumpevalvalue = (vc->int_value != 0);
        } else if (vc->type == H64VALTYPE_FLOAT64) {
            jumpevalvalue = fabs(vc->float_value - 0) != 0;
        } else if (vc->type == H64VALTYPE_NONE ||
                vc->type == H64VALTYPE_UNSPECIFIED_KWARG) {
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
        if (vmthread->vmexec_owner->moptions.vmexec_debug &&
                !vmthread_PrintExec(vmthread, func_id, (void*)inst))
            goto triggeroom;
        #endif

        p += (
            (ptrdiff_t)inst->jumpbytesoffset
        );
        assert(p >= pr->func[func_id].instructions &&
               p < pend);
        goto *jumptable[((h64instructionany *)p)->type];
    }
    inst_newiterator: {
        h64fprintf(stderr, "newiterator not implemented\n");
        return 0;
    }
    inst_iterate: {
        h64fprintf(stderr, "iterate not implemented\n");
        return 0;
    }
    inst_pushrescueframe: {
        h64instruction_pushrescueframe *inst = (
            (h64instruction_pushrescueframe *)p
        );
        #ifndef NDEBUG
        if (vmthread->vmexec_owner->moptions.vmexec_debug &&
                !vmthread_PrintExec(vmthread, func_id, (void*)inst))
            goto triggeroom;
        #endif

        #ifndef NDEBUG
        int previous_count = vmthread->errorframe_count;
        #endif
        if (!pusherrorframe(
                vmthread, inst->frameid,
                ((inst->mode & RESCUEMODE_JUMPONRESCUE) != 0 ?
                 (p - pr->func[func_id].instructions) +
                 (int64_t)inst->jumponrescue : -1),
                ((inst->mode & RESCUEMODE_JUMPONFINALLY) != 0 ?
                 (p - pr->func[func_id].instructions) +
                 (int64_t)inst->jumponfinally : -1),
                inst->sloterrorto)) {
            goto triggeroom;
        }
        #ifndef NDEBUG
        assert(vmthread->errorframe_count > 0 &&
               vmthread->errorframe_count > previous_count);
        #endif

        p += sizeof(h64instruction_pushrescueframe);
        while (((h64instructionany *)p)->type == H64INST_ADDRESCUETYPE ||
                ((h64instructionany *)p)->type == H64INST_ADDRESCUETYPEBYREF
                ) {
            #ifndef NDEBUG
            if (vmthread->vmexec_owner->moptions.vmexec_debug &&
                    !vmthread_PrintExec(vmthread, func_id, (void*)p))
                goto triggeroom;
            #endif
            int64_t class_id = -1;
            if (((h64instructionany *)p)->type == H64INST_ADDRESCUETYPE) {
                assert(((h64instruction_addrescuetype *)p)->frameid ==
                       inst->frameid);
                class_id = ((h64instruction_addrescuetype *)p)->classid;
            } else {
                int16_t slotfrom = (
                    ((h64instruction_addrescuetypebyref *)p)->slotfrom
                );
                assert(
                    ((h64instruction_addrescuetypebyref *)p)->frameid ==
                    inst->frameid
                );
                valuecontent *vc = STACK_ENTRY(stack, slotfrom);
                if (vc->type == H64VALTYPE_CLASSREF) {
                    int64_t _class_id = vc->int_value;
                    assert(_class_id >= 0 &&
                           _class_id < pr->classes_count);
                    if (pr->classes[_class_id].is_error) {
                        // is Error-derived!
                        class_id = _class_id;
                    }
                }
            }
            if (class_id < 0) {
                RAISE_ERROR(H64STDERROR_TYPEERROR,
                                "catch on non-Error type");
                goto *jumptable[((h64instructionany *)p)->type];
            }
            assert(vmthread->errorframe_count > 0);
            h64vmrescueframe *topframe = &(vmthread->
                errorframe[vmthread->errorframe_count - 1]);
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
            if (((h64instructionany *)p)->type == H64INST_ADDRESCUETYPE) {
                p += sizeof(h64instruction_addrescuetype);
            } else {
                p += sizeof(h64instruction_addrescuetypebyref);
            }
        }
        goto *jumptable[((h64instructionany *)p)->type];
    }
    inst_addrescuetypebyref: {
        h64fprintf(stderr, "INVALID isolated addrescuetypebyref!!\n");
        return 0;
    }
    inst_addrescuetype: {
        h64fprintf(stderr, "INVALID isolated addrescuetype!!\n");
        return 0;
    }
    inst_poprescueframe: {
        h64instruction_poprescueframe *inst = (
            (h64instruction_poprescueframe *)p
        );
        #ifndef NDEBUG
        if (vmthread->vmexec_owner->moptions.vmexec_debug &&
                !vmthread_PrintExec(vmthread, func_id, (void*)inst))
            goto triggeroom;
        #endif

        // See if we got a finally block to terminate:
        assert(vmthread->errorframe_count > 0);
        {
            int64_t offset = (p - pr->func[func_id].instructions);
            int64_t oldoffset = offset;
            int exitwitherror = 0;
            h64errorinfo e = {0};
            vmthread_errors_EndFinally(
                vmthread, inst->frameid,
                &func_id, &offset,
                &exitwitherror, &e
            );  // calls poperrorframe
            if (exitwitherror) {
                int result = stack_ToSize(stack, 0, 0);
                stack->current_func_floor = 0;
                assert(result != 0);
                *returneduncaughterror = 1;
                memcpy(einfo, &e, sizeof(e));
                return 1;
            }
            if (offset == oldoffset) {
                offset += sizeof(h64instruction_poprescueframe);
            }
            p = (pr->func[func_id].instructions + offset);
        }

        goto *jumptable[((h64instructionany *)p)->type];
    }
    inst_getattributebyname: {
        h64instruction_getattributebyname *inst = (
            (h64instruction_getattributebyname *)p
        );
        #ifndef NDEBUG
        if (vmthread->vmexec_owner->moptions.vmexec_debug &&
                !vmthread_PrintExec(vmthread, func_id, (void*)inst))
            goto triggeroom;
        #endif

        // Prepare target:
        valuecontent _tmpbuf;
        assert(
            stack != NULL && inst->slotto >= 0 &&
            (int64_t)inst->slotto < stack->entry_count -
            stack->current_func_floor &&
            stack->alloc_count >= stack->entry_count
        );
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
        attridx_t attr_index = -1;
        int64_t nameidx = inst->nameidx;
        if (nameidx >= 0 &&
                nameidx == vmexec->program->as_str_name_index
                ) {  // .as_str
            // See what this actually is as a string with .as_str:
            h64wchar strvalue[128];
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
                        (h64wchar)(uint8_t)intvalue[i]
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
                        (h64wchar)(uint8_t)floatvalue[i]
                    );
                    i++;
                }
                strvaluelen = len;
            } else if (vc->type == H64VALTYPE_BOOL) {
                if (vc->int_value != 0) {
                    strvalue[0] = (h64wchar)'t';
                    strvalue[1] = (h64wchar)'r';
                    strvalue[2] = (h64wchar)'u';
                    strvalue[3] = (h64wchar)'e';
                    strvaluelen = 4;
                } else {
                    strvalue[0] = (h64wchar)'f';
                    strvalue[1] = (h64wchar)'a';
                    strvalue[2] = (h64wchar)'l';
                    strvalue[3] = (h64wchar)'s';
                    strvalue[4] = (h64wchar)'e';
                    strvaluelen = 5;
                }
            } else if (vc->type == H64VALTYPE_NONE) {
                strvalue[0] = (h64wchar)'n';
                strvalue[1] = (h64wchar)'o';
                strvalue[2] = (h64wchar)'n';
                strvalue[3] = (h64wchar)'e';
                strvaluelen = 4;
            }
            // If .as_str failed to run, abort with an error:
            if (strvaluelen < 0) {
                RAISE_ERROR(
                    H64STDERROR_RUNTIMEERROR,
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
            if (!target->ptr_value)
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
                strvalue, strvaluelen * sizeof(h64wchar)
            );
            assert((unsigned int)gcval->str_val.len ==
                   (unsigned int)strvaluelen);
            ADDREF_NONHEAP(target);
        } else if (nameidx >= 0 &&
                nameidx == vmexec->program->len_name_index
                ) {  // .len
            int64_t len = -1;
            if (vc->type == H64VALTYPE_GCVAL) {
                if (((h64gcvalue *)vc->ptr_value)->type ==
                        H64GCVALUETYPE_STRING) {
                    vmstrings_RequireLetterLen(
                        &((h64gcvalue *)vc->ptr_value)->str_val
                    );
                    len = ((h64gcvalue *)vc->ptr_value)->str_val.
                        letterlen;
                } else if (((h64gcvalue *)vc->ptr_value)->type ==
                        H64GCVALUETYPE_BYTES) {
                    len = ((h64gcvalue *)vc->ptr_value)->
                        bytes_val.len;
                }
            } else if (vc->type == H64VALTYPE_SHORTSTR) {
                len = utf32_letters_count(
                    vc->shortstr_value, vc->shortstr_len
                );
            } else if (vc->type == H64VALTYPE_SHORTBYTES) {
                len = vc->shortbytes_len;
            }
            if (len < 0) {
                RAISE_ERROR(
                    H64STDERROR_ATTRIBUTEERROR,
                    "given attribute not present on this value"
                );
                goto *jumptable[((h64instructionany *)p)->type];
            }
            target->type = H64VALTYPE_INT64;
            target->int_value = len;
        } else if (nameidx >= 0 &&
                (nameidx == vmexec->program->init_name_index ||
                 nameidx == vmexec->program->to_str_name_index ||
                 nameidx == vmexec->program->on_destroy_name_index ||
                 nameidx == vmexec->program->equals_name_index ||
                 nameidx == vmexec->program->to_hash_name_index
                ) &&
                vc->type == H64VALTYPE_GCVAL &&
                ((h64gcvalue *)vc->ptr_value)->type ==
                H64GCVALUETYPE_OBJINSTANCE
                ) {  // .init/.to_str/.destroyed/.cloned/.equals/.to_hash
                     // (on a class object instance)
            // This is an internal function that is supposed to be
            // inaccessible from the outside.
            RAISE_ERROR(
                H64STDERROR_ATTRIBUTEERROR,
                "this special func attribute is not "
                "directly accessible"
            );
            goto *jumptable[((h64instructionany *)p)->type];
        } else if (nameidx >= 0 &&
                nameidx == vmexec->program->add_name_index &&
                (vc->type != H64VALTYPE_GCVAL ||
                 ((h64gcvalue *)vc->ptr_value)->type !=
                 H64GCVALUETYPE_OBJINSTANCE)
                ) {  // .add, not on class object
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
                    vmexec->program->containeradd_func_index
                );
                gcval->closure_info->closure_self = (
                    (h64gcvalue *)vc->ptr_value
                );
                ((h64gcvalue *)vc->ptr_value)->heapreferencecount++;
            } else {
                RAISE_ERROR(
                    H64STDERROR_ATTRIBUTEERROR,
                    "given attribute not present on this value"
                );
                goto *jumptable[((h64instructionany *)p)->type];
            }
        } else if (nameidx >= 0 &&
                vc->type == H64VALTYPE_GCVAL &&
                ((h64gcvalue *)vc->ptr_value)->type ==
                H64GCVALUETYPE_OBJINSTANCE &&
                ((attr_index = h64program_LookupClassAttribute(
                    pr, ((h64gcvalue *)vc->ptr_value)->class_id,
                    nameidx
                    )) >= 0)) {
            if (attr_index < H64CLASS_METHOD_OFFSET) {
                // A varattr, just copy the contents:
                h64gcvalue *gcv = ((h64gcvalue *)vc->ptr_value);
                assert(attr_index >= 0 &&
                       gcv->varattr_count);
                memcpy(target, &gcv->varattr[attr_index],
                       sizeof(*target));
                ADDREF_NONHEAP(target);
            } else {
                // It's a function attribute, return closure:
                classid_t class_id = (
                    ((h64gcvalue *)vc->ptr_value)->classid
                );
                assert(
                    attr_index - H64CLASS_METHOD_OFFSET <
                    pr->classes[class_id].funcattr_count
                );
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
                    pr->classes[class_id].funcattr_func_idx[
                        (attr_index - H64CLASS_METHOD_OFFSET)
                    ]
                );
                gcval->closure_info->closure_self = (
                    (h64gcvalue *)vc->ptr_value
                );
            }
        } else {
            RAISE_ERROR(
                H64STDERROR_ATTRIBUTEERROR,
                "given attribute not present on this value"
            );
            goto *jumptable[((h64instructionany *)p)->type];
        }
        if (copyatend) {
            DELREF_NONHEAP(STACK_ENTRY(stack, inst->slotto));
            valuecontent_Free(STACK_ENTRY(stack, inst->slotto));
            memcpy(STACK_ENTRY(stack, inst->slotto),
                   target, sizeof(*target));
        }
        p += sizeof(*inst);
        goto *jumptable[((h64instructionany *)p)->type];
    }
    inst_jumptofinally: {
        h64instruction_jumptofinally *inst = (
            (h64instruction_jumptofinally *)p
        );
        #ifndef NDEBUG
        if (vmthread->vmexec_owner->moptions.vmexec_debug &&
                !vmthread_PrintExec(vmthread, func_id, (void*)inst))
            goto triggeroom;
        #endif

        int64_t offset = (p - pr->func[func_id].instructions);
        vmthread_errors_ProceedToFinally(
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
        if (vmthread->vmexec_owner->moptions.vmexec_debug &&
                !vmthread_PrintExec(vmthread, func_id, (void*)inst))
            goto triggeroom;
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
        h64fprintf(stderr, "newset not implemented\n");
        return 0;
    }
    inst_newmap: {
        h64fprintf(stderr, "newmap not implemented\n");
        return 0;
    }
    inst_newvector: {
        h64fprintf(stderr, "newvector not implemented\n");
        return 0;
    }
    {
        // WARNING: ALL UNINITIALIZED, since goto jumps over them:
        classid_t class_id;
        #ifndef NDEBUG
        int wasbyref;
        #endif
        int16_t slot_to;
        int skipbytes;
        inst_newinstancebyref: {
            h64instruction_newinstancebyref *inst = (
                (h64instruction_newinstancebyref *)p
            );
            #ifndef NDEBUG
            if (vmthread->vmexec_owner->moptions.vmexec_debug &&
                    !vmthread_PrintExec(vmthread, func_id, (void*)inst))
                goto triggeroom;
            #endif

            #ifndef NDEBUG
            wasbyref = 1;
            #endif
            skipbytes = sizeof(*inst);
            slot_to = inst->slotto;
            valuecontent *vcfrom = STACK_ENTRY(
                stack, inst->classtypeslotfrom
            );
            if (vcfrom->type != H64VALTYPE_CLASSREF) {
                RAISE_ERROR(
                    H64STDERROR_TYPEERROR,
                    "new must be called on a class type"
                );
                goto *jumptable[((h64instructionany *)p)->type];
            }
            class_id = vcfrom->int_value;
            goto sharedending_newinstance_newinstancebyref;
        }
        inst_newinstance: {
            h64instruction_newinstance *inst = (
                (h64instruction_newinstance *)p
            );
            #ifndef NDEBUG
            if (vmthread->vmexec_owner->moptions.vmexec_debug &&
                    !vmthread_PrintExec(vmthread, func_id, (void*)inst))
                 goto triggeroom;
            #endif

            #ifndef NDEBUG
            wasbyref = 0;
            #endif
            skipbytes = sizeof(*inst);
            slot_to = inst->slotto;
            class_id = inst->classidcreatefrom;
            goto sharedending_newinstance_newinstancebyref;
        }
        sharedending_newinstance_newinstancebyref: {
            valuecontent *vctarget = STACK_ENTRY(stack, slot_to);
            DELREF_NONHEAP(vctarget);
            valuecontent_Free(vctarget);
            memset(vctarget, 0, sizeof(*vctarget));
            assert(class_id >= 0 && class_id < vmexec->program->classes_count);
            vctarget->type = H64VALTYPE_GCVAL;
            vctarget->ptr_value = poolalloc_malloc(heap, 0);
            if (!vctarget->ptr_value)
                goto triggeroom;
            h64gcvalue *gcval = (h64gcvalue *)vctarget->ptr_value;
            gcval->type = H64GCVALUETYPE_OBJINSTANCE;
            gcval->heapreferencecount = 0;
            gcval->externalreferencecount = 1;
            gcval->class_id = class_id;
            gcval->varattr_count = (
                vmexec->program->classes[class_id].varattr_count
            );
            gcval->varattr = malloc(
                sizeof(valuecontent) * gcval->varattr_count
            );
            if (!gcval->varattr) {
                gcval->type = H64VALTYPE_NONE;
                goto triggeroom;
            }
            memset(gcval->varattr, 0, sizeof(*gcval->varattr) *
                   gcval->varattr_count);

            // Call into $$varinit if it exists:
            if (pr->classes[class_id].varinitfuncidx >= 0) {
                funcid_t varinit_func_id = pr->classes[class_id].
                    varinitfuncidx;
                assert(varinit_func_id >= 0 &&
                       varinit_func_id < pr->func_count);

                // Make sure the function properties are correct:
                assert(!pr->func[varinit_func_id].iscfunc);
                assert(pr->func[varinit_func_id].input_stack_size == 1);
                assert(pr->func[varinit_func_id].kwarg_count == 0);

                // Push function frame:
                int64_t new_func_floor = (
                    STACK_TOTALSIZE(stack) + 1
                );
                int64_t offset = (ptrdiff_t)(
                    p - pr->func[func_id].instructions
                ) + skipbytes;
                if (!pushfuncframe(vmthread, varinit_func_id,
                        new_func_floor - 1, func_id, offset,
                        new_func_floor
                        )) {
                    goto triggeroom;
                }
                funcnestdepth++;
                assert(STACK_TOTALSIZE(stack) >= new_func_floor +
                       pr->func[varinit_func_id].input_stack_size +
                       pr->func[varinit_func_id].inner_stack_size);

                // Push self reference into closure argument:
                valuecontent *closurearg = (
                    &stack->entry[(int64_t)new_func_floor]
                );
                assert(closurearg->type ==
                       H64VALTYPE_NONE);  // stack was grown, must be none
                closurearg->type = H64VALTYPE_GCVAL;
                closurearg->ptr_value = gcval;
                ADDREF_NONHEAP(closurearg);

                // Enter function:
                #ifndef NDEBUG
                if (vmthread->vmexec_owner->moptions.vmexec_debug)
                    h64fprintf(
                        stderr, "horsevm: debug: vmexec jump into "
                        "h64 func %" PRId64 " (via %s/$$varinit)\n",
                        (int64_t)varinit_func_id,
                        (wasbyref ? "newinstancebyref" : "newinstance")
                    );
                #endif
                func_id = varinit_func_id;
                vmthread->call_settop_reverse = -1;
                p = pr->func[func_id].instructions;
                goto *jumptable[((h64instructionany *)p)->type];
            } else {
                // No $$varinit
                p += skipbytes;
                goto *jumptable[((h64instructionany *)p)->type];
            }
        }
    }
    inst_getconstructor: {
        h64instruction_getconstructor *inst = (
            (h64instruction_getconstructor *)p
        );
        #ifndef NDEBUG
        if (vmthread->vmexec_owner->moptions.vmexec_debug &&
                !vmthread_PrintExec(vmthread, func_id, (void*)inst))
                goto triggeroom;
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
        attridx_t attr_index = -1;
        int64_t nameidx = pr->init_name_index;
        if (nameidx < 0) {
            target->type = H64VALTYPE_NONE;
        } else {
            classid_t class_id = ((h64gcvalue *)vc->ptr_value)->class_id;
            attridx_t idx = (
                attr_index = h64program_LookupClassAttribute(
                    pr, class_id, nameidx
                ));
            if (idx < 0) {
                target->type = H64VALTYPE_NONE;
            } else {
                assert(idx >= H64CLASS_METHOD_OFFSET);
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
                    pr->classes[class_id].funcattr_func_idx[
                        (idx - H64CLASS_METHOD_OFFSET)
                    ]
                );
                gcval->closure_info->closure_self = (
                    (h64gcvalue *)vc->ptr_value
                );
            }
        }
        if (copyatend) {
            DELREF_NONHEAP(STACK_ENTRY(stack, inst->slotto));
            valuecontent_Free(STACK_ENTRY(stack, inst->slotto));
            memcpy(STACK_ENTRY(stack, inst->slotto),
                   target, sizeof(*target));
        }
        p += sizeof(*inst);
        goto *jumptable[((h64instructionany *)p)->type];
    }
    inst_awaititem: {
        h64fprintf(stderr, "awaititem not implemented\n");
        return 0;
    }
    inst_hasattrjump: {
        h64instruction_hasattrjump *inst = (
            (h64instruction_hasattrjump *)p
        );
        #ifndef NDEBUG
        if (vmthread->vmexec_owner->moptions.vmexec_debug &&
                !vmthread_PrintExec(vmthread, func_id, (void*)inst))
                goto triggeroom;
        #endif

        valuecontent *vc = STACK_ENTRY(stack, inst->slotvaluecheck);
        int64_t nameidx = inst->nameidxcheck;
        int found = 0;
        if (nameidx >= 0 &&
                vc->type == H64VALTYPE_GCVAL &&
                ((h64gcvalue *)vc->ptr_value)->type ==
                H64GCVALUETYPE_OBJINSTANCE &&
                (h64program_LookupClassAttribute(
                    pr, ((h64gcvalue *)vc->ptr_value)->class_id,
                    nameidx
                    ) >= 0)) {
            found = 1;
        } else if (nameidx >= 0 &&
                vc->type == H64VALTYPE_GCVAL &&
                ((h64gcvalue *)vc->ptr_value)->type ==
                H64GCVALUETYPE_OBJINSTANCE &&
                (nameidx == vmexec->program->is_a_name_index
                )) {
            found = 1;
        } else if (nameidx >= 0 &&
                nameidx == vmexec->program->as_str_name_index) {
            found = 1;
        } else if (nameidx >= 0 &&
                nameidx == vmexec->program->add_name_index &&
                vc->type == H64VALTYPE_GCVAL && (
                ((h64gcvalue *)vc->ptr_value)->type ==
                H64GCVALUETYPE_SET ||
                ((h64gcvalue *)vc->ptr_value)->type ==
                H64GCVALUETYPE_LIST)) {
            found = 1;
        } else if (nameidx >= 0 &&
                nameidx == vmexec->program->len_name_index &&
                (vc->type == H64VALTYPE_SHORTSTR ||
                 vc->type == H64VALTYPE_SHORTBYTES ||
                 (vc->type == H64VALTYPE_GCVAL && (
                 ((h64gcvalue *)vc->ptr_value)->type ==
                 H64GCVALUETYPE_SET ||
                 ((h64gcvalue *)vc->ptr_value)->type ==
                 H64GCVALUETYPE_LIST ||
                 ((h64gcvalue *)vc->ptr_value)->type ==
                 H64GCVALUETYPE_MAP ||
                 ((h64gcvalue *)vc->ptr_value)->type ==
                 H64GCVALUETYPE_STRING ||
                 ((h64gcvalue *)vc->ptr_value)->type ==
                 H64GCVALUETYPE_BYTES
                )))) {
            found = 1;
        }

        if (!found) {
            p += (int64_t)inst->jumpbytesoffset;
        } else {
            p += sizeof(*inst);
        }
        goto *jumptable[((h64instructionany *)p)->type];
    }
    inst_raise: {
        h64instruction_raise *inst = (
            (h64instruction_raise *)p
        );
        #ifndef NDEBUG
        if (vmthread->vmexec_owner->moptions.vmexec_debug &&
                !vmthread_PrintExec(vmthread, func_id, (void*)inst))
                goto triggeroom;
        #endif
        assert(
            inst->error_class_id >= 0 &&
            inst->error_class_id < vmexec->program->classes_count
        );

        _raise_error_class_id = inst->error_class_id;
        _raise_msg_stack_slot = inst->sloterrormsgobj;
        goto inst_raise_do;
    }
    inst_raisebyref: {
        h64instruction_raisebyref *inst = (
            (h64instruction_raisebyref *)p
        );
        #ifndef NDEBUG
        if (vmthread->vmexec_owner->moptions.vmexec_debug &&
                !vmthread_PrintExec(vmthread, func_id, (void*)inst))
                goto triggeroom;
        #endif
        assert(
            inst->sloterrorobj >= 0 &&
            inst->sloterrorobj < (int64_t)vmthread->stack->entry_count -
                (int64_t)vmthread->stack->current_func_floor
        );

        valuecontent *vobj = STACK_ENTRY(stack, inst->sloterrorobj);
        if (vobj->type != H64VALTYPE_ERROR) {
            RAISE_ERROR(
                H64STDERROR_TYPEERROR,
                "cannot raise from a non-error class"
            );
            goto *jumptable[((h64instructionany *)p)->type];
        }
        _raise_error_class_id = (
            vobj->error_class_id
        );
        _raise_msg_stack_slot = inst->sloterrormsgobj;
        goto inst_raise_do;
    }
    inst_raise_do: {
        assert(
            _raise_msg_stack_slot >= 0 &&
            _raise_msg_stack_slot < (int64_t)vmthread->stack->entry_count -
                (int64_t)vmthread->stack->current_func_floor
        );
        valuecontent *vmsg = STACK_ENTRY(stack, _raise_msg_stack_slot);
        if (vmsg->type != H64VALTYPE_CONSTPREALLOCSTR &&
                (vmsg->type != H64VALTYPE_GCVAL ||
                 ((h64gcvalue *)vmsg->ptr_value)->type !=
                    H64GCVALUETYPE_STRING)) {
            RAISE_ERROR(
                H64STDERROR_TYPEERROR,
                "error message must be a string"
            );
            goto *jumptable[((h64instructionany *)p)->type];
        }

        // Extract error message:
        char *errmsgbuf = NULL;
        int64_t errmsglen = 0;
        if (vmsg->type == H64VALTYPE_CONSTPREALLOCSTR) {
            errmsgbuf = (char *)vmsg->constpreallocstr_value;
            errmsglen = vmsg->constpreallocstr_len;
        } else if (vmsg->type == H64VALTYPE_SHORTBYTES) {
            errmsgbuf = (char *)vmsg->shortstr_value;
            errmsglen = vmsg->shortstr_len;
        } else if (vmsg->type == H64VALTYPE_GCVAL &&
                ((h64gcvalue *)vmsg->ptr_value)->type ==
                    H64GCVALUETYPE_STRING) {
            errmsgbuf = (char *)(
                ((h64gcvalue *)vmsg->ptr_value)->str_val.s
            );
            errmsglen = (
                ((h64gcvalue *)vmsg->ptr_value)->str_val.len
            );
        }

        // Do error raise as instructed:
        RAISE_ERROR_U32(
            _raise_error_class_id,
            errmsgbuf, errmsglen
        );
        goto *jumptable[((h64instructionany *)p)->type];
    }

    setupinterpreter:
    jumptable[H64INST_INVALID] = &&inst_invalid;
    jumptable[H64INST_SETCONST] = &&inst_setconst;
    jumptable[H64INST_SETGLOBAL] = &&inst_setglobal;
    jumptable[H64INST_GETGLOBAL] = &&inst_getglobal;
    jumptable[H64INST_SETBYINDEXEXPR] = &&inst_setbyindexexpr;
    jumptable[H64INST_SETBYATTRIBUTENAME] = &&inst_setbyattributename;
    jumptable[H64INST_SETBYATTRIBUTEIDX] = &&inst_setbyattributeidx;
    jumptable[H64INST_GETFUNC] = &&inst_getfunc;
    jumptable[H64INST_GETCLASS] = &&inst_getclass;
    jumptable[H64INST_VALUECOPY] = &&inst_valuecopy;
    jumptable[H64INST_BINOP] = &&inst_binop;
    jumptable[H64INST_UNOP] = &&inst_unop;
    jumptable[H64INST_CALL] = &&inst_call;
    jumptable[H64INST_CALLIGNOREIFNONE] = &&inst_callignoreifnone;
    jumptable[H64INST_SETTOP] = &&inst_settop;
    jumptable[H64INST_CALLSETTOP] = &&inst_callsettop;
    jumptable[H64INST_RETURNVALUE] = &&inst_returnvalue;
    jumptable[H64INST_JUMPTARGET] = &&inst_jumptarget;
    jumptable[H64INST_CONDJUMP] = &&inst_condjump;
    jumptable[H64INST_JUMP] = &&inst_jump;
    jumptable[H64INST_NEWITERATOR] = &&inst_newiterator;
    jumptable[H64INST_ITERATE] = &&inst_iterate;
    jumptable[H64INST_PUSHRESCUEFRAME] = &&inst_pushrescueframe;
    jumptable[H64INST_ADDRESCUETYPEBYREF] = &&inst_addrescuetypebyref;
    jumptable[H64INST_ADDRESCUETYPE] = &&inst_addrescuetype;
    jumptable[H64INST_POPRESCUEFRAME] = &&inst_poprescueframe;
    jumptable[H64INST_GETATTRIBUTEBYNAME] = &&inst_getattributebyname;
    jumptable[H64INST_JUMPTOFINALLY] = &&inst_jumptofinally;
    jumptable[H64INST_NEWLIST] = &&inst_newlist;
    jumptable[H64INST_NEWSET] = &&inst_newset;
    jumptable[H64INST_NEWMAP] = &&inst_newmap;
    jumptable[H64INST_NEWVECTOR] = &&inst_newvector;
    jumptable[H64INST_NEWINSTANCEBYREF] = &&inst_newinstancebyref;
    jumptable[H64INST_NEWINSTANCE] = &&inst_newinstance;
    jumptable[H64INST_GETCONSTRUCTOR] = &&inst_getconstructor;
    jumptable[H64INST_AWAITITEM] = &&inst_awaititem;
    jumptable[H64INST_HASATTRJUMP] = &&inst_hasattrjump;
    jumptable[H64INST_RAISE] = &&inst_raise;
    jumptable[H64INST_RAISEBYREF] = &&inst_raisebyref;
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
    op_jumptable[H64OP_INDEXBYEXPR] = &&binop_indexbyexpr;
    assert(stack != NULL);
    if (!isresume) {
        // Final set-up before we go:
        if (!pushfuncframe(vmthread, func_id, -1, -1, 0, 0))
            goto triggeroom;
        vmthread->funcframe[vmthread->funcframe_count - 1].
            stack_func_floor = (
                original_stack_size
            );
        funcnestdepth++;
    }

    goto *jumptable[((h64instructionany *)p)->type];
}

void vmthread_ClearAsyncForegroundWork(h64vmthread *vt) {
    // FIXME
    
}

int vmthread_RunFunction(
        h64vmexec *vmexec, h64vmthread *start_thread,
        vmthreadresumeinfo *rinfo, int worker_no,
        int *returnedsuspend,
        vmthreadsuspendinfo *suspendinfo,
        int *returneduncaughterror,
        h64errorinfo *einfo
        ) {
    // Remember func frames & old stack we had before, then launch:
    int isresume = (
        !rinfo->run_from_start
    );
    int64_t func_id = rinfo->func_id;
    int64_t old_stack = -1;
    int64_t old_floor = -1;
    int funcframesbefore = -1;
    int errorframesbefore = -1;
    assert(func_id >= 0 && func_id < vmexec->program->func_count);
    if (!isresume) {
        old_stack = start_thread->stack->entry_count - (
            vmexec->program->func[func_id].input_stack_size
        );
        old_floor = start_thread->stack->current_func_floor;
        funcframesbefore = start_thread->funcframe_count;
        errorframesbefore = start_thread->errorframe_count;
    } else {
        old_stack = rinfo->precall_old_stack;
        old_floor = rinfo->precall_old_floor;
        funcframesbefore = (
            rinfo->precall_funcframesbefore
        );
        errorframesbefore = (
            rinfo->precall_errorframesbefore
        );
    }
    assert(old_stack >= 0);
    start_thread->upcoming_resume_info->precall_old_stack = old_stack;
    start_thread->upcoming_resume_info->precall_old_floor = old_floor;
    start_thread->upcoming_resume_info->precall_funcframesbefore = (
        funcframesbefore
    );
    start_thread->upcoming_resume_info->precall_errorframesbefore = (
        errorframesbefore
    );
    start_thread->upcoming_resume_info->func_id = -1;

    int inneruncaughterror = 0;
    int innersuspend = 0;
    int result = _vmthread_RunFunction_NoPopFuncFrames(
        vmexec, start_thread, rinfo, worker_no,
        &inneruncaughterror, einfo,
        &innersuspend, suspendinfo
    );  // ^ run actual function
    if (!innersuspend) {
        // This must be cleared for any exit path except suspend:
        vmthread_ClearAsyncForegroundWork(start_thread);
    }
    if (!inneruncaughterror && innersuspend) {
        // Special: we need to leave things as they are for resume.
        if (returnedsuspend)
            *returnedsuspend = 1;
        if (returneduncaughterror)
            *returneduncaughterror = 0;
        return result;
    }

    // Make sure we don't leave excess func frames behind:
    assert(start_thread->funcframe_count >= funcframesbefore);
    assert(start_thread->errorframe_count >= errorframesbefore);
    int i = start_thread->funcframe_count;
    while (i > funcframesbefore) {
        assert(inneruncaughterror);  // only allow unclean frames if error
        int r = popfuncframe(
            start_thread, &vmexec->moptions, 1
        );  // don't clean stack ...
            // ... since func stack bottoms might be nonsense, and this
            // might assert. We'll just wipe it manually later.
        assert(r != 0);
        i--;
    }
    i = start_thread->errorframe_count;
    while (i > errorframesbefore) {
        assert(inneruncaughterror);  // only allow unclean frames if error
        poperrorframe(start_thread);
        i--;
    }
    // Stack clean-up:
    assert(start_thread->stack->entry_count >= old_stack);
    if (inneruncaughterror) {
        // An error, so we need to do manual stack cleaning:
        if (start_thread->stack->entry_count > old_stack) {
            int _sizing_worked = stack_ToSize(
                start_thread->stack, old_stack, 0
            );
            assert(_sizing_worked);
        }
        assert(start_thread->stack->entry_count == old_stack);
        assert(old_floor <= start_thread->stack->current_func_floor);
        start_thread->stack->current_func_floor = old_floor;
    } else {
        // No error, so make sure stack was properly cleared:
        assert(start_thread->stack->entry_count == old_stack + 1);
        //   (old stack + return value on top)
        // Set old function floor:
        start_thread->stack->current_func_floor = old_floor;
    }
    if (returneduncaughterror)
        *returneduncaughterror = inneruncaughterror;
    return result;
}

int vmthread_RunFunctionWithReturnInt(
        h64vmworker *worker,
        h64vmthread *start_thread,
        int already_locked_in,
        int64_t func_id, int worker_no,
        int *returnedsuspend,
        vmthreadsuspendinfo *suspendinfo,
        int *returneduncaughterror,
        h64errorinfo *einfo, int *out_returnint
        ) {
    assert(worker);
    h64vmexec *vmexec = worker->vmexec;
    assert(
        vmexec && start_thread && einfo && returneduncaughterror &&
        out_returnint && suspendinfo && returnedsuspend
    );
    if (!already_locked_in) {
        mutex_Lock(vmexec->worker_overview->worker_mutex);
        if (!vmschedule_CanThreadResume_UnguardedCheck(
                start_thread, datetime_Ticks()
                )) {
            mutex_Release(vmexec->worker_overview->worker_mutex);
            *returneduncaughterror = 1;
            *returnedsuspend = 0;
            memset(einfo, 0, sizeof(*einfo));
            einfo->error_class_id = H64STDERROR_RUNTIMEERROR;
            char msgbuf[] = "internal error: VM scheduler bug, "
                "trying to resume unresumable VM thread";
            int64_t outlen = 0;
            h64wchar *outbuf = utf8_to_utf32(
                msgbuf, strlen(msgbuf),
                NULL, NULL, &outlen
            );
            if (outbuf) {
                einfo->msg = outbuf;
                einfo->msglen = outlen;
            }
            einfo->refcount = 1;
            return 0;
        }
    }
    start_thread->run_by_worker = worker;
    vmthreadresumeinfo storedresumeinfo;
    memcpy(
        &storedresumeinfo, start_thread->upcoming_resume_info,
        sizeof(storedresumeinfo)
    );
    vmthread_SetSuspendState(  // will clear upcoming_resume_info
        start_thread, SUSPENDTYPE_NONE, 0
    );

    mutex_Release(vmexec->worker_overview->worker_mutex);
    int innerreturnedsuspend = 0;
    int innerreturneduncaughterror = 0;
    int64_t old_stack_size = start_thread->stack->entry_count;
    if (storedresumeinfo.func_id >= 0) {
        old_stack_size = (
            storedresumeinfo.precall_old_stack
        );
    }
    assert(
        (func_id >= 0 && storedresumeinfo.func_id < 0) ||
        (func_id < 0 && storedresumeinfo.func_id >= 0) ||
        (func_id >= 0 && storedresumeinfo.func_id == func_id)
    );
    if (storedresumeinfo.func_id < 0 && func_id >= 0) {
        storedresumeinfo.func_id = func_id;
        storedresumeinfo.run_from_start = 1;
    } else {
        #ifndef NDEBUG
        assert(storedresumeinfo.func_id == func_id || func_id < 0);
        #endif
    }
    assert(storedresumeinfo.run_from_start ||
           storedresumeinfo.precall_old_stack >= 0);
    int result = vmthread_RunFunction(
        vmexec, start_thread, &storedresumeinfo, worker_no,
        &innerreturnedsuspend, suspendinfo,
        &innerreturneduncaughterror, einfo
    );
    mutex_Lock(vmexec->worker_overview->worker_mutex);
    assert(
        ((start_thread->stack->entry_count <= old_stack_size + 1) ||
        !result || innerreturnedsuspend) &&
        start_thread->stack->entry_count >= old_stack_size
    );
    if (innerreturneduncaughterror) {
        *returneduncaughterror = 1;
        *returnedsuspend = 0;
        vmthread_SetSuspendState(
            start_thread, SUSPENDTYPE_DONE, 0
        );
        return 1;
    } else if (innerreturnedsuspend) {
        *returneduncaughterror = 0;
        *returnedsuspend = 1;
        vmthread_SetSuspendState(
            start_thread, suspendinfo->suspendtype,
            suspendinfo->suspendarg
        );  // mutex was locked, so this is fine
        return 1;
    }
    vmthread_SetSuspendState(
        start_thread, SUSPENDTYPE_DONE, 0
    );
    *returneduncaughterror = 0;
    *returnedsuspend = 0;
    if (!result || start_thread->stack->
            entry_count <= old_stack_size) {
        *out_returnint = 0;
    } else {
        valuecontent *vc = STACK_ENTRY(
            start_thread->stack, old_stack_size
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
        } else if (vc->type == H64VALTYPE_NONE) {
            *out_returnint = 0;
            return result;
        }
        *out_returnint = 0;
    }
    return result;
}

int vmexec_ReturnFuncError(
        h64vmthread *vmthread, int64_t error_id,
        const char *msg, ...
        ) {
    if (STACK_TOP(vmthread->stack) == 0) {
        if (!stack_ToSize(
                vmthread->stack,
                STACK_TOTALSIZE(vmthread->stack) + 1, 1
                ))
            return 0;
    }
    valuecontent *vc = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vc);
    valuecontent_Free(vc);
    memset(vc, 0, sizeof(*vc));
    vc->type = H64VALTYPE_ERROR;
    vc->error_class_id = error_id;
    vc->einfo = malloc(sizeof(*vc->einfo));
    if (vc->einfo) {
        memset(vc->einfo, 0, sizeof(*vc->einfo));
        vc->einfo->error_class_id = error_id;
        char buf[4096];
        va_list args;
        va_start(args, msg);
        vsnprintf(buf, sizeof(buf) - 1, msg, args);
        va_end(args);
        int wasinvalid = 0;
        int wasoom = 0;
        vc->einfo->msg = utf8_to_utf32_ex(
            buf, strlen(buf), NULL, 0,
            NULL, NULL, &vc->einfo->msglen,
            1, 0, &wasinvalid, &wasoom
        );
        if (!vc->einfo->msg)
            vc->einfo->msglen = 0;
    }
    ADDREF_NONHEAP(vc);
    return 0;
}
