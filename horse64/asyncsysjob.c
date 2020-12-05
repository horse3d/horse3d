// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <stdint.h>

#include "asyncsysjob.h"
#include "processrun.h"
#include "threading.h"
#include "vmexec.h"


int asyncjob_RequestAsync(
        h64vmthread *request_thread,
        h64asyncsysjob *job,
        int *failurewasoom
        ) {
    if (!request_thread || !job) {
        if (failurewasoom) *failurewasoom = 1;
        return 0;
    }
}