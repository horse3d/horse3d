// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_VFSPARTIALFILEIO_H_
#define HORSE64_VFSPARTIALFILEIO_H_

#include <stdint.h>
#include <stdio.h>

void *_PhysFS_Io_partialFileReadOnlyStruct(
    FILE *f, uint64_t start, uint64_t len
);

#endif  // HORSE64_VFSPARTIALFILEIO_H_