// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_HOSTLOOKUP_H_
#define HORSE64_HOSTLOOKUP_H_

typedef struct h64vmthread h64vmthread;

int hostlookup_RequestAsync(
    h64vmthread *request_thread,
    const char *requested_host,
    int *fail_nosuchhost,
    int *fail_outofmemory,
    char **resulting_host
);

#endif  // HORSE64_HOSTLOOKUP_H_