// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sockets.h"

volatile _Atomic int winsockinitdone = 0;

__attribute__((constructor)) void _winsockinit() {
    #if defined(_WIN32) || defined(_WIN64)
    if (winsockinitdone)
        return;
    winsockinitdone = 1;
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        fprintf(stderr, "horsevm: error: WSAStartup() failed\n");
        exit(1);
    }
    #endif
}

h64socket *sockets_New(int tls) {
    #if defined(_WIN32) || defined(_WIN64)
    _winsockinit();
    #endif
    h64socket *sock = malloc(sizeof(*sock));
    if (!sock)
        return NULL;
    if (tls != 0)
        sock->flags |= SOCKFLAG_TLS;
    sock->fd = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (sock->fd < 0) {
        free(sock);
        return NULL;
    }
    #if defined(_WIN32) || defined(_WIN64)
    unsigned long mode = 1;
    if (ioctlsocket(sock->fd, FIONBIO, &mode) != NO_ERROR) {
        closesocket(sock->fd);
        free(sock);
        return NULL;
    }
    #else
    int _flags = fcntl(sock->fd, F_GETFL, 0);
    fcntl(sock->fd, F_SETFL, _flags | O_NONBLOCK);
    #endif
    return sock;
}

void sockets_Destroy(h64socket *sock) {
    #if defined(_WIN32) || defined(_WIN64)
    closesocket(sock->fd);
    #else
    close(sock->fd);
    #endif
    free(sock);
}

int sockets_Send(
        h64socket *s, const uint8_t *bytes, size_t byteslen
        ) {
    if (s->sendbufsize < s->sendbuffill + byteslen) {
        char *newsendbuf = realloc(
            s->sendbuf,
            sizeof(*newsendbuf) * (byteslen + s->sendbuffill)
        );
        if (!newsendbuf)
            return 0;
        s->sendbuf = newsendbuf;
        s->sendbufsize = s->sendbuffill + byteslen;
    }
    memcpy(s->sendbuf + s->sendbuffill, bytes, byteslen);
    return 1;
}

