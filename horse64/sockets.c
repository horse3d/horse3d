// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"
#include "secrandom.h"
#include "threading.h"

#include <assert.h>
#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <errno.h>
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

h64socket *sockets_NewBlockingRaw(int v6capable) {
    #if defined(_WIN32) || defined(_WIN64)
    _winsockinit();
    #endif
    h64socket *sock = malloc(sizeof(*sock));
    if (!sock)
        return NULL;
    sock->fd = socket(
        (v6capable ? AF_INET6 : AF_INET), SOCK_STREAM, IPPROTO_TCP
    );
    if (sock->fd < 0) {
        free(sock);
        return NULL;
    }
    #if defined(_WIN32) || defined(_WIN64)
    // SECURITY RELEVANT, default Windows sockets to be address exclusive
    // to match the Linux/BSD/macOS defaults:
    int val = 1;
    if (setsockopt(sock->fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
            (char *)&val, sizeof(val)) != 0) {
        closesocket(sock->fd);
        free(sock);
        return NULL;
    }
    // Enable dual stack:
    if (v6capable) {
        val = 0;
        if (setsockopt(sock->fd, SOL_SOCKET, IPV6_V6ONLY,
                (char *)&val, sizeof(val)) != 0) {
            closesocket(sock->fd);
            free(sock);
            return NULL;
        }
    }
    #endif
    return sock;
}

int sockets_SetNonblocking(h64socket *sock, int nonblocking) {
    #if defined(_WIN32) || defined(_WIN64)
    unsigned long mode = (nonblocking != 0);
    if (ioctlsocket(sock->fd, FIONBIO, &mode) != NO_ERROR) {
        closesocket(sock->fd);
        free(sock);
        return 0;
    }
    #else
    int _flags = fcntl(sock->fd, F_GETFL, 0);
    if (nonblocking)
        fcntl(sock->fd, F_SETFL, _flags | O_NONBLOCK);
    else
        fcntl(sock->fd, F_SETFL, _flags & ~((int)O_NONBLOCK));
    #endif
    return 1;
}

h64socket *sockets_New(int ipv6capable, int tls) {
    h64socket *sock = sockets_NewBlockingRaw(ipv6capable);
    if (!sockets_SetNonblocking(sock, 1)) {
        sockets_Destroy(sock);
        return NULL;
    }
    if (tls != 0)
        sock->flags |= SOCKFLAG_TLS;
    return sock;
}

void sockets_Destroy(h64socket *sock) {
    if (!sock)
        return;
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

typedef struct _h64socketpairsetup {
    h64socket *recv_server, *trigger_client;
    char connectkey[256];
    int port;
    int resultconnfd;
    _Atomic volatile uint8_t connected, failure;
} _h64socketpairsetup;


void sockets_FreeSocketPairSetupData(_h64socketpairsetup *te) {
    if (!te)
        return;
    sockets_Destroy(te->recv_server);
    sockets_Destroy(te->trigger_client);
    if (te->resultconnfd >= 0) {
        #if defined(_WIN32) || defined(_WIN64)
        closesocket(te->resultconnfd);
        #else
        close(te->resultconnfd);
        #endif
    }
    memset(te, 0, sizeof(*te));
    te->resultconnfd = -1;
}

#define _PAIRKEYSIZE 256

typedef struct _h64socketpairsetup_conn {
    char recvbuf[_PAIRKEYSIZE];
    int fd;
    int recvbuffill;
} _h64socketpairsetup_conn;

static void _threadEventAccepter(void *userdata) {
    _h64socketpairsetup *te = (
        (_h64socketpairsetup *)userdata
    );
    if (!sockets_SetNonblocking(te->recv_server, 1)) {
        te->failure = 1;
        return;
    }
    _h64socketpairsetup_conn connsbuf[16];
    _h64socketpairsetup_conn *conns = (
        (_h64socketpairsetup_conn*) &connsbuf
    );
    int conns_alloc = 16;
    int conns_count = 0;
    int conns_onheap = 0;
    while (!te->failure) {
        struct sockaddr_in acceptaddr;
        int acceptfd = -1;
        socklen_t size = sizeof(acceptaddr);
        if ((acceptfd = accept(
                te->recv_server->fd,
                (struct sockaddr*)&acceptaddr,
                &size
                )) >= 0) {
            if (conns_count + 1 > conns_alloc) {
                if (conns_onheap) {
                    _h64socketpairsetup_conn *connsnew = realloc(
                        conns,
                        conns_alloc * 2 * sizeof(*conns)
                    );
                    if (!connsnew)
                        goto failure;
                    conns = connsnew;
                    conns_alloc *= 2;
                } else {
                    _h64socketpairsetup_conn *connsnew = malloc(
                        conns_alloc * 2 * sizeof(*conns)
                    );
                    if (!connsnew) {
                        failure:
                        if (conns_onheap)
                            free(conns);
                        te->failure = 1;
                        return;
                    }
                    memcpy(connsnew, conns,
                        conns_alloc * sizeof(*conns));
                    conns = connsnew;
                    conns_alloc *= 2;
                    conns_onheap = 1;
                }
            }
            _h64socketpairsetup_conn *c = &conns[conns_count];
            memset(c, 0, sizeof(*c));
            conns_count++;
            c->fd = acceptfd;
        } else {
            #if defined(_WIN32) || defined(_WIN64)

            #else
            if (errno == EINVAL || errno == ENOMEM ||
                    errno == ENOBUFS || errno == ENOTSOCK) {
                goto failure;
            }
            #endif
        }
        int i = 0;
        while (i < conns_count) {
            int readbytes = (
                _PAIRKEYSIZE - conns[i].recvbuffill
            );
            if (readbytes <= 0) {
                i++;
                continue;
            }
            ssize_t read = recv(
                conns[i].fd, conns[i].recvbuf + conns[i].recvbuffill,
                readbytes, 0
            );
            if (read < 0) {
                #if defined(_WIN32) || defined(_WIN64)
                uint32_t errc = GetLastError();
                if (errc != WSA_IO_INCOMPLETE &&
                        errc != WSA_IO_PENDING &&
                        errc != WSAEINTR &&
                        errc != WSAEWOULDBLOCK &&
                        errc != WSAEINPROGRESS &&
                        errc != WSAEALREADY) {
                #else
                if (errno != EAGAIN && errno != EWOULDBLOCK &&
                        errno != EPIPE) {
                #endif
                    closeconn:
                    #if defined(_WIN32) || defined(_WIN64)
                    closesocket(conns[i].fd);
                    #else
                    close(conns[i].fd);
                    #endif
                    if (i + 1 < conns_count) {
                        memcpy(
                            &conns[i], &conns[i + 1],
                            (conns_count - i - 1) *
                                sizeof(*conns)
                        );
                    }
                    conns_count--;
                    continue;
                }
            } else {
                conns[i].recvbuffill += read;
                if (conns[i].recvbuffill >= _PAIRKEYSIZE) {
                    if (memcmp(
                            conns[i].recvbuf,
                            te->connectkey, _PAIRKEYSIZE) == 0) {
                        te->resultconnfd = conns[i].fd;
                        break;
                    } else {
                        goto closeconn;
                    }
                }
            }
            i++;
        }
    }
    int k = 0;
    while (k < conns_count) {
        if (conns[k].fd != te->resultconnfd) {
            #if defined(_WIN32) || defined(_WIN64)
            closesocket(conns[k].fd);
            #else
            close(conns[k].fd);
            #endif
        }
        k++;
    }
    if (conns_onheap)
        free(conns);
    sockets_Destroy(te->recv_server);
    te->recv_server = NULL;
    te->connected = 1;
    return;
}

int sockets_NewPair(h64socket **s1, h64socket **s2) {
    _h64socketpairsetup te = {0};
    te.resultconnfd = -1;

    // Get socket pair:
    te.recv_server = sockets_NewBlockingRaw(1);
    if (!te.recv_server) {
        sockets_FreeSocketPairSetupData(&te);
        return 0;
    }
    te.trigger_client = sockets_NewBlockingRaw(1);
    if (!te.trigger_client) {
        sockets_FreeSocketPairSetupData(&te);
        return 0;
    }

    // Generate connect key to ensure we got the right client:
    if (!secrandom_GetBytes(te.connectkey, sizeof(te.connectkey))) {
        sockets_FreeSocketPairSetupData(&te);
        return 0;
    }

    // Connect socket pair:
    struct sockaddr_in6 servaddr = {0};
    servaddr.sin6_family = AF_INET6;
    servaddr.sin6_addr = in6addr_loopback;
    if (bind(te.recv_server->fd, (struct sockaddr *)&servaddr,
             sizeof(servaddr)) < 0) {
        sockets_FreeSocketPairSetupData(&te);
        return 0;
    }
    unsigned int len = sizeof(servaddr);
    if (getsockname(
            te.recv_server->fd,
            (struct sockaddr *)&servaddr,
            &len) != 0 || len != sizeof(servaddr)) {
        sockets_FreeSocketPairSetupData(&te);
        return 0;
    }

    // Connect client and send payload (blocking):
    if (!sockets_SetNonblocking(te.trigger_client, 0)) {
        sockets_FreeSocketPairSetupData(&te);
        return 0;
    }
    te.port = servaddr.sin6_port;
    assert(te.port > 0);
    thread *accept_thread = thread_Spawn(
        _threadEventAccepter, &te
    );
    if (!accept_thread) {
        sockets_FreeSocketPairSetupData(&te);
        return 0;
    }
    if (connect(
            te.trigger_client->fd, (struct sockaddr *)&servaddr,
            sizeof(servaddr)) < 0) {
        te.failure = 1;
        thread_Join(accept_thread);
        sockets_FreeSocketPairSetupData(&te);
        return 0;
    }
    thread_Join(accept_thread);
    accept_thread = NULL;

    // Done with payload handling, set client to non-blocking:
    if (!sockets_SetNonblocking(te.trigger_client, 1)) {
        sockets_FreeSocketPairSetupData(&te);
        return 0;
    }

    // Evaluate result:
    assert(te.resultconnfd >= 0 && te.trigger_client != NULL);
    h64socket *sock_one = te.trigger_client;
    te.trigger_client = NULL;
    h64socket *sock_two = malloc(sizeof(*sock_two));
    if (!sock_two) {
        sockets_Destroy(sock_one);
        sockets_FreeSocketPairSetupData(&te);
        return 0;
    }
    memset(sock_two, 0, sizeof(*sock_two));
    sock_two->fd = te.resultconnfd;
    te.resultconnfd = -1;

    // Also set second socket end to non-blocking and return:
    if (!sockets_SetNonblocking(sock_two, 1)) {
        sockets_Destroy(sock_one);
        sockets_Destroy(sock_two);
        sockets_FreeSocketPairSetupData(&te);
        return 0;
    }
    sockets_FreeSocketPairSetupData(&te);
    *s1 = sock_one;
    *s2 = sock_two;
    return 1;
}