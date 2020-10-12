// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "process.h"
#include "stack.h"
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
     * @param background=false whether to run the process in the background
     *    (true), or whether to wait until it terminates before resuming
     *    (the default, false).
     * @returns a @see{process object|process.process} if background=true,
     *     otherwise returns the exit code of the process as @see{number}.
     */
    assert(STACK_TOP(vmthread->stack) >= 3);

    return 1;
}

int processlib_RegisterFuncsAndModules(h64program *p) {
    // process.run:
    const char *process_run_kw_arg_name[] = {
        NULL, "arguments", "background"
    };
    int64_t idx;
    idx = h64program_RegisterCFunction(
        p, "run", &processlib_run,
        NULL, 3, process_run_kw_arg_name, 0,  // fileuri, args
        "process", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    return 1;
}