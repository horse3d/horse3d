// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_SECRANDOM_H_
#define HORSE64_SECRANDOM_H_

#include "compileconfig.h"

#include <stdint.h>
#include <stdlib.h>  // for size_t

int secrandom_GetBytes(char *ptr, size_t amount);

int64_t secrandom_RandIntRange(int64_t min, int64_t max);

int64_t secrandom_RandInt();

#endif  // HORSE64_SECRANDOM_H_
