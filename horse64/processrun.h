// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_PROCESSRUN_H_
#define HORSE64_PROCESSRUN_H_

#include <stdint.h>

#include "widechar.h"

typedef struct processrun processrun;

processrun *processrun_Launch(
    const h64wchar *path, int64_t path_len,
    int arg_count, const h64wchar **arg_s, const int64_t *arg_len
);

#endif  // HORSE64_PROCESSRUN_H_
