// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include "threading.h"
#include "vmexec.h"


int hostlookup_RequestAsync(
        h64vmthread *request_thread,
        const char *requested_host,
        int *fail_nosuchhost,
        int *fail_outofmemory,
        char **resulting_host
        ) {
    *fail_nosuchhost = 0;
    *fail_outofmemory = 1;
    return 0;
}