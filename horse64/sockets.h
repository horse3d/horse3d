// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_SOCKETS_H_
#define HORSE64_SOCKETS_H_

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif
#include <stdint.h>
#include <stdlib.h>

#define SOCKFLAG_TLS 0x1
#define SOCKFLAG_SERVER 0x2

typedef struct h64socket {
    int fd;
    uint8_t flags;
#if defined(_WIN32) || defined(_WIN64)
    HANDLE sock_event_read, sock_event_write;
#endif
    char *sendbuf;
    size_t sendbufsize, sendbuffill;
} h64socket;

h64socket *sockets_New(int tls);

int sockets_Send(
    h64socket *s, const uint8_t *bytes, size_t byteslen
);

void sockets_Destroy(h64socket *sock);

#endif  // HORSE64_SOCKETS_H_