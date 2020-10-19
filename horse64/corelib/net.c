// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include <assert.h>

#include "process.h"
#include "stack.h"
#include "vmexec.h"

/// @module net Networking streams to access the local network and internet.

int netlib_connect(
        h64vmthread *vmthread
        ) {
    /**
     * Connect to a different host using a TCP/IP connection, optionally
     * using a TLS/SSL encryption.
     *
     * @func connect
     * @param host the host to connect to, either by name or by ip
     * @param port the port of the host to connect to
     * @param encrypt=false whether to connect using TLS/SSL encryption
     * @returns a @see{network stream|net.connect}
     */
    assert(STACK_TOP(vmthread->stack) >= 3);

    return 1;
}

int netlib_RegisterFuncsAndModules(h64program *p) {
    // process.run:
    const char *net_connect_kw_arg_name[] = {
        NULL, NULL, "encrypt"
    };
    int64_t idx;
    idx = h64program_RegisterCFunction(
        p, "connect", &netlib_connect,
        NULL, 3, net_connect_kw_arg_name, 0,  // fileuri, args
        "net", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    return 1;
}