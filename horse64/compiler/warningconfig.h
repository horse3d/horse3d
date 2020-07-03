// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_COMPILER_WARNINGCONFIG_H_
#define HORSE64_COMPILER_WARNINGCONFIG_H_


typedef struct h64compilewarnconfig {
    int warn_shadowing_direct_locals;
    int warn_shadowing_parent_func_locals;
    int warn_shadowing_globals;
    int warn_unrecognized_escape_sequences;
} h64compilewarnconfig;


void warningconfig_Init(h64compilewarnconfig *wconfig);

int warningconfig_CheckOption(
    h64compilewarnconfig *wconfig, const char *option
);

#endif  // HORSE64_COMPILER_WARNINGCONFIG_H_
