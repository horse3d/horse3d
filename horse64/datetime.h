// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_DATIME_H_
#define HORSE64_DATIME_H_

#include <stdint.h>


uint64_t datetime_Ticks();

uint64_t datetime_TicksNoSuspendJump();

void datetime_Sleep(uint64_t ms);

#endif  // HORSE64_DATIME_H_
