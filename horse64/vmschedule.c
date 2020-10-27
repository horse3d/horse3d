// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bytecode.h"
#include "debugsymbols.h"
#include "pipe.h"
#include "stack.h"
#include "valuecontentstruct.h"
#include "vmexec.h"
#include "vmschedule.h"


static char _unexpectedlookupfail[] = "<unexpected lookup fail>";

static const char *_classnamelookup(h64program *pr, int64_t classid) {
    h64classsymbol *csymbol = h64debugsymbols_GetClassSymbolById(
        pr->symbols, classid
    );
    if (!csymbol)
        return _unexpectedlookupfail;
    return csymbol->name;
}

static void _printuncaughterror(
        h64program *pr, h64errorinfo *einfo
        ) {
    fprintf(stderr, "Uncaught %s: ",
        (pr->symbols ?
         _classnamelookup(pr, einfo->error_class_id) :
         "Error"));
    if (einfo->msg) {
        char *buf = malloc(einfo->msglen * 5 + 2);
        if (!buf) {
            fprintf(stderr, "<utf8 buf alloc failure>");
        } else {
            int64_t outlen = 0;
            int result = utf32_to_utf8(
                einfo->msg, einfo->msglen,
                buf, einfo->msglen * 5 + 1,
                &outlen, 1
            );
            if (!result) {
                fprintf(stderr, "<utf8 conversion failure>");
            } else {
                buf[outlen] = '\0';
                fprintf(stderr, "%s", buf);
            }
            free(buf);
        }
    } else {
        fprintf(stderr, "<no message>");
    }
    fprintf(stderr, "\n");
}

int vmschedule_AsyncScheduleFunc(
        h64vmexec *vmexec, h64vmthread *vmthread,
        int64_t new_func_floor, int64_t func_id
        ) {
    h64vmthread *newthread = vmthread_New(vmexec);
    if (!newthread) {
        return 0;
    }
    int64_t func_slots = STACK_TOTALSIZE(vmthread->stack) - new_func_floor;
    if (!stack_ToSize(
            newthread->stack, func_slots, 0)) {
        vmthread_Free(newthread);
        return 0;
    }

    // Pipe through the stack contents:
    h64gcvalue _stackbuf[64];
    h64gcvalue *object_instances_transferlist = _stackbuf;
    int object_instances_transferlist_count = 0;
    int object_instances_transferlist_alloc = 64;
    int object_instances_transferlist_onheap = 0;
    int64_t i = 0;
    while (i < func_slots) {
        int result = _pipe_DoPipeObject(
            vmthread, newthread,
            i + new_func_floor, i,
            &object_instances_transferlist,
            &object_instances_transferlist_count,
            &object_instances_transferlist_alloc,
            &object_instances_transferlist_onheap
        );
        if (!result) {
            if (object_instances_transferlist_onheap)
                free(object_instances_transferlist);
            vmthread_Free(newthread);
            return 0;
        }
        i++;
    }

    // Set up calls to .on_piped() later, for all piped object instances:
    assert(
        object_instances_transferlist_count == 0
    ); // ^ FIXME, implement this

    // Set suspend state to be resumed once we get to run this:
    assert(0);  // FIXME: implement

    return 1;
}

int vmschedule_ExecuteProgram(
        h64program *pr, h64misccompileroptions *moptions
        ) {
    h64vmexec *mainexec = vmexec_New();
    if (!mainexec) {
        fprintf(stderr, "vmexec.c: out of memory during setup\n");
        return -1;
    }

    h64vmthread *mainthread = vmthread_New(mainexec);
    if (!mainthread) {
        fprintf(stderr, "vmexec.c: out of memory during setup\n");
        return -1;
    }
    mainexec->program = pr;

    assert(pr->main_func_index >= 0);
    memcpy(&mainexec->moptions, moptions, sizeof(*moptions));
    h64errorinfo einfo = {0};
    int haduncaughterror = 0;
    int rval = 0;
    if (pr->globalinit_func_index >= 0) {
        if (!vmthread_RunFunctionWithReturnInt(
                mainexec, mainthread, pr->globalinit_func_index,
                &haduncaughterror, &einfo, &rval
                )) {
            fprintf(stderr, "vmexec.c: fatal error in $$globalinit, "
                "out of memory?\n");
            vmthread_Free(mainthread);
            return -1;
        }
        if (haduncaughterror) {
            assert(einfo.error_class_id >= 0);
            _printuncaughterror(pr, &einfo);
            vmthread_Free(mainthread);
            return -1;
        }
        int result = stack_ToSize(mainthread->stack, 0, 0);
        assert(result != 0);
    }
    haduncaughterror = 0;
    rval = 0;
    if (!vmthread_RunFunctionWithReturnInt(
            mainexec, mainthread, pr->main_func_index,
            &haduncaughterror, &einfo, &rval
            )) {
        fprintf(stderr, "vmexec.c: fatal error in main, "
            "out of memory?\n");
        vmthread_Free(mainthread);
        vmexec_Free(mainexec);
        return -1;
    }
    if (haduncaughterror) {
        assert(einfo.error_class_id >= 0);
        _printuncaughterror(pr, &einfo);
        vmthread_Free(mainthread);
        vmexec_Free(mainexec);
        return -1;
    }
    vmthread_Free(mainthread);
    vmexec_Free(mainexec);
    return rval;
}

int vmschedule_SuspendFunc(
        h64vmthread *vmthread, suspendtype suspend_type,
        int64_t suspend_intarg
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
    vc->type = H64VALTYPE_THREADSUSPENDINFO;
    vc->suspend_type = suspend_type;
    vc->suspend_intarg = suspend_intarg;
    return 0;
}
