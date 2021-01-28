// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>

#include "asyncsysjob.h"
#include "corelib/errors.h"
#include "corelib/process.h"
#include "gcvalue.h"
#include "stack.h"
#include "valuecontentstruct.h"
#include "vmexec.h"
#include "vmlist.h"

/// @module process Run or interact with other processes on the same machine.


struct processlib_run_asyncprogress {
    void (*abortfunc)(void *dataptr);
    h64asyncsysjob *run_job;
    uint8_t did_attempt_launch;
};

void _processlib_run_abort(void *dataptr) {
    struct processlib_run_asyncprogress *adata = dataptr;
    if (adata->run_job) {
        asyncjob_AbandonJob(adata->run_job);
        adata->run_job = NULL;
    }
}


int processlib_run(
        h64vmthread *vmthread
        ) {
    /**
     * Run an external executable as a separate process.
     *
     * @func run
     * @param path the path of the executable file to be run
     * @param arguments=[] the arguments to be passed to the process
     * @param background=no whether to run the process in the background
     *    (=yes), or whether to wait until it terminates before resuming
     *    (=no, the default).
     * @param search_system=yes whether to search for command names in
     *    system-wide folders, other than just the local folder and/or the
     *    exact binary path
     * @returns a @see{process object|process.process} if background=yes,
     *     otherwise returns the exit code of the process as @see{number}.
     */
    assert(STACK_TOP(vmthread->stack) >= 3);

    struct processlib_run_asyncprogress *asprogress = (
        vmthread->foreground_async_work_dataptr
    );
    assert(asprogress != NULL);
    asprogress->abortfunc = &_processlib_run_abort;

    valuecontent *vcpath = STACK_ENTRY(vmthread->stack, 0);
    if (vcpath->type != H64VALTYPE_SHORTSTR &&
            (vcpath->type != H64VALTYPE_GCVAL ||
            ((h64gcvalue*)(vcpath->ptr_value))->type !=
                H64GCVALUETYPE_STRING)) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "path must be a string"
        );
    }
    valuecontent *vcargs = STACK_ENTRY(vmthread->stack, 0);
    if ((vcargs->type != H64VALTYPE_GCVAL ||
            ((h64gcvalue*)(vcargs->ptr_value))->type !=
                H64GCVALUETYPE_LIST) &&
            vcargs->type != H64VALTYPE_UNSPECIFIED_KWARG) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "arguments must be a list"
        );
    }
    genericlist *arglist = (
        ((h64gcvalue *)vcargs->ptr_value)->list_values
    );
    int64_t argcount = 0;
    {
        const int64_t listcount = vmlist_Count(arglist);
        int64_t i = 0;
        while (i < listcount) {
            valuecontent *item = vmlist_Get(arglist, i);
            if (item->type != H64VALTYPE_SHORTSTR &&
                    (item->type != H64VALTYPE_GCVAL ||
                     ((h64gcvalue *)item->ptr_value)->type ==
                         H64GCVALUETYPE_STRING)) {
                return vmexec_ReturnFuncError(
                    vmthread, H64STDERROR_TYPEERROR,
                    "each argument in arguments list must be a string"
                );
            }
            i++;
        }
        argcount = listcount;
    }
    valuecontent *vcbackground = STACK_ENTRY(vmthread->stack, 0);
    if (vcbackground->type != H64VALTYPE_BOOL &&
            vcbackground->type != H64VALTYPE_UNSPECIFIED_KWARG) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "background must be a boolean"
        );
    }
    valuecontent *vcsearchsystem = STACK_ENTRY(vmthread->stack, 0);
    if (vcsearchsystem->type != H64VALTYPE_BOOL &&
            vcsearchsystem->type != H64VALTYPE_UNSPECIFIED_KWARG) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "system_commands must be a boolean"
        );
    }
    int inbackground = (
        (vcbackground->type == H64VALTYPE_BOOL ?
         (vcbackground->int_value != 0) : 0));
    int searchsystem = (
        (vcsearchsystem->type == H64VALTYPE_BOOL ?
         (vcsearchsystem->int_value != 0) : 1));
    h64wchar *runcmd = NULL;
    int64_t runcmdlen = 0;
    if (vcpath->type == H64VALTYPE_SHORTSTR) {
        runcmd = vcpath->shortstr_value;
        runcmdlen = vcpath->shortstr_len;
    } else if (vcpath->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue *)vcpath->ptr_value)->type ==
                H64GCVALUETYPE_STRING) {
        runcmd = (
            ((h64gcvalue *)vcpath->ptr_value)->str_val.s
        );
        runcmdlen = (
            ((h64gcvalue *)vcpath->ptr_value)->str_val.len
        );
    }

    if (!asprogress->did_attempt_launch && inbackground) {
        asprogress->did_attempt_launch = 1;
        asprogress->run_job = (
            asyncjob_CreateEmpty()
        );
        if (!asprogress->run_job) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_OUTOFMEMORYERROR,
                "out of memory during process launch"
            );
        }

        asprogress->run_job->type = (
            ASYNCSYSJOB_RUNCMD
        );
        asprogress->run_job->runcmd.cmd = malloc(
            sizeof(*asprogress->run_job->runcmd.cmd) *
                runcmdlen
        );
        if (!asprogress->run_job->runcmd.cmd) {
            goto jobcreateoom;
        }
        memcpy(
            asprogress->run_job->runcmd.cmd,
            runcmd, sizeof(*runcmd) * runcmdlen
        );
        asprogress->run_job->runcmd.cmdlen = runcmdlen;
        int argcount = asprogress->run_job->runcmd.argcount;
        asprogress->run_job->runcmd.arg = malloc(
            sizeof(*asprogress->run_job->runcmd.arg) * argcount
        );
        if (!asprogress->run_job->runcmd.arg)
            goto jobcreateoom;
        memset(
            asprogress->run_job->runcmd.arg, 0,
            sizeof(*asprogress->run_job->runcmd.arg) * argcount
        );
        asprogress->run_job->runcmd.argcount = argcount;
        int64_t i = 0;
        while (i < asprogress->run_job->runcmd.argcount) {
            valuecontent *item = vmlist_Get(arglist, i);
            h64wchar *args = NULL;
            int64_t arglen = 0;
            if (item->type == H64VALTYPE_SHORTSTR) {
                args = item->shortstr_value;
                arglen = item->shortstr_len;
            } else {
                assert(item->type == H64VALTYPE_GCVAL &&
                    ((h64gcvalue *)item->ptr_value)->type ==
                        H64GCVALUETYPE_STRING);
                args = (
                    ((h64gcvalue *)item->ptr_value)->str_val.s
                );
                arglen = (
                    ((h64gcvalue *)item->ptr_value)->str_val.len
                );
            }
            asprogress->run_job->runcmd.arg[i] = malloc(
                sizeof(*args) * (arglen > 0 ? arglen : 1)
            );
            if (asprogress->run_job->runcmd.arg[i]) {
                goto jobcreateoom;
            }
            i++;
        }

        int result = asyncjob_RequestAsync(
            vmthread, asprogress->run_job
        );
        if (!result) {
            jobcreateoom: ;
            asyncjob_Free(asprogress->run_job);
            // ^ still owned by us on failure
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_OUTOFMEMORYERROR,
                "out of memory during host lookup"
            );
        }
        return vmschedule_SuspendFunc(
            vmthread, SUSPENDTYPE_ASYNCSYSJOBWAIT,
            (uintptr_t)(asprogress->run_job)
        );
    }
    if (asprogress->did_attempt_launch) {
        assert(asprogress->run_job != NULL);
        assert(asprogress->run_job->done);
        valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
        DELREF_NONHEAP(vcresult);
        valuecontent_Free(vcresult);
        vcresult->type = H64VALTYPE_NONE;
        return 1;
    }

    return vmexec_ReturnFuncError(
        vmthread, H64STDERROR_NOTIMPLEMENTEDERROR,
        "internal error: reached unimplemented state"
    );
}

int processlib_RegisterFuncsAndModules(h64program *p) {
    int64_t idx;

    // process.run:
    const char *process_run_kw_arg_name[] = {
        NULL, "arguments", "background", "system_commands"
    };
    idx = h64program_RegisterCFunction(
        p, "run", &processlib_run,
        NULL, 3, process_run_kw_arg_name,  // fileuri, args
        "process", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    return 1;
}