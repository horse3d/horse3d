// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_BYTECODESERIALIZE_H_
#define HORSE64_BYTECODESERIALIZE_H_

#include <stdint.h>

typedef struct h64program h64program;

int h64program_Dump(h64program *p, char **out, int64_t *out_len);

int h64program_Restore(
    h64program **write_to_ptr, const char *in, int64_t in_len
);

#endif  // HORSE64_BYTECODESERIALIZE_H_