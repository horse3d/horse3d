// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>

#include "corelib/errors.h"
#include "corelib/process.h"
#include "gcvalue.h"
#include "stack.h"
#include "valuecontentstruct.h"
#include "vmexec.h"

/// @module process Run or interact with other processes on the same machine.

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
     * @param system_commands=yes whether to search for command names in
     *    system-wide folders, other than just the local folder and/or the
     *    exact binary path
     * @returns a @see{process object|process.process} if background=yes,
     *     otherwise returns the exit code of the process as @see{number}.
     */
    assert(STACK_TOP(vmthread->stack) >= 3);

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
    valuecontent *vcbackground = STACK_ENTRY(vmthread->stack, 0);
    if (vcbackground->type != H64VALTYPE_BOOL &&
            vcbackground->type != H64VALTYPE_UNSPECIFIED_KWARG) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "background must be a boolean"
        );
    }
    valuecontent *vcsystemcmds = STACK_ENTRY(vmthread->stack, 0);
    if (vcsystemcmds->type != H64VALTYPE_BOOL &&
            vcsystemcmds->type != H64VALTYPE_UNSPECIFIED_KWARG) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "system_commands must be a boolean"
        );
    }
    return 1;
}

int processlib_RegisterFuncsAndModules(h64program *p) {
    // process.run:
    const char *process_run_kw_arg_name[] = {
        NULL, "arguments", "background", "system_commands"
    };
    int64_t idx;
    idx = h64program_RegisterCFunction(
        p, "run", &processlib_run,
        NULL, 3, process_run_kw_arg_name,  // fileuri, args
        "process", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    return 1;
}