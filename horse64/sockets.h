// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_SOCKETS_H_
#define HORSE64_SOCKETS_H_

#include "compileconfig.h"

#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#endif
#include <assert.h>
#include <openssl/ssl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "widechar.h"

#define SOCKFLAG_TLS 0x1
#define SOCKFLAG_SERVER 0x2
#define _SOCKFLAG_CONNECTCALLED 0x4
#define _SOCKFLAG_KNOWNCONNECTED 0x8
#define _SOCKFLAG_NOOUTSTANDINGTLSCONNECT 0x10
#define SOCKFLAG_IPV6CAPABLE 0x20
#define _SOCKFLAG_ISINSENDLIST 0x40
#define _SOCKFLAG_SENDWAITSFORREAD 0x80


#if defined(USE_POLL_ON_UNIX) && USE_POLL_ON_UNIX != 0
#define CANUSEPOLL
#else
#ifdef CANUSEPOLL
#undef CANUSEPOLL
#endif
#endif

#if defined(_WIN32) || defined(_WIN64)
typedef uintptr_t h64sockfd_t;
#else
typedef int32_t h64sockfd_t;
#endif

typedef struct h64socket {
    h64sockfd_t fd;
    uint32_t flags;
    SSL *sslobj;
#if defined(_WIN32) || defined(_WIN64)
    HANDLE sock_event_read, sock_event_write;
#endif
    char *sendbuf;
    size_t sendbufsize, sendbuffill;
    size_t _resent_attempt_fill, _ssl_repeat_errortype,
        _receive_reattempt_usedsize;
} h64socket;

typedef struct h64threadevent h64threadevent;

#define _pollsmallsetsize 12

typedef struct h64sockset {
    #if defined(_WIN32) || defined(_WIN64) || !defined(CANUSEPOLL)
    fd_set readset;
    fd_set errorset;
    fd_set writeset;
    h64sockfd_t *fds;
    int fds_count;
    #else
    struct pollfd smallset[_pollsmallsetsize];
    struct pollfd *set;
    int size, fill;
    struct pollfd smallresult[_pollsmallsetsize];
    struct pollfd *result;
    int resultfill;
    #endif
} h64sockset;

ATTR_UNUSED static inline void sockset_Init(h64sockset *set) {
    memset(set, 0, sizeof(*set));
    #if defined(_WIN32) || defined(_WIN64) || !defined(CANUSEPOLL)
    FD_ZERO(&set->readset);
    FD_ZERO(&set->writeset);
    FD_ZERO(&set->errorset);
    #endif
}

#if defined(_WIN32) || defined(_WIN64)
#define H64SOCKSET_WAITREAD 0x1
#define H64SOCKSET_WAITWRITE 0x2
#define H64SOCKSET_WAITERROR 0x4
#else
#define H64SOCKSET_WAITREAD POLLIN
#define H64SOCKSET_WAITWRITE POLLOUT
#define H64SOCKSET_WAITERROR (POLLERR | POLLHUP)
#endif
#define H64SOCKSET_WAITALL (\
    H64SOCKSET_WAITREAD|H64SOCKSET_WAITWRITE|H64SOCKSET_WAITERROR\
    )

int _sockset_Expand(
    ATTR_UNUSED h64sockset *set
);

typedef enum h64sockerror {
    H64SOCKERROR_SUCCESS = 0,
    H64SOCKERROR_CONNECTIONDISCONNECTED = -1,
    H64SOCKERROR_NEEDTOREAD = -2,
    H64SOCKERROR_NEEDTOWRITE = -3,
    H64SOCKERROR_OUTOFMEMORY = -4,
    H64SOCKERROR_OPERATIONFAILED = -5
} h64sockerror;

int sockets_ConnectClient(
    h64socket *sock, const h64wchar *ip,
    int64_t iplen, int port
);

int sockets_WasEverConnected(h64socket *sock);

ATTR_UNUSED static inline int sockset_GetResult(
        h64sockset *set, h64sockfd_t fd, int waittypes
        ) {
    #if defined(_WIN32) || defined(_WIN64) || !defined(CANUSEPOLL)
    int result = 0;
    if ((waittypes & H64SOCKSET_WAITREAD) != 0) {
        if (FD_ISSET(fd, &set->readset))
            result |= H64SOCKSET_WAITREAD;
    }
    if ((waittypes & H64SOCKSET_WAITWRITE) != 0) {
        if (FD_ISSET(fd, &set->writeset))
            result |= H64SOCKSET_WAITWRITE;
    }
    if ((waittypes & H64SOCKSET_WAITERROR) != 0) {
        if (FD_ISSET(fd, &set->errorset))
            result |= H64SOCKSET_WAITERROR;
    }
    return result;
    #else
    int i = 0;
    const int count = set->resultfill;
    struct pollfd *checkset = (
        set->size == 0 ? (struct pollfd*)set->smallresult : set->result
    );
    while (i < count) {
        if (checkset[i].fd == fd) {
            return (checkset[i].revents & waittypes);
        }
        i++;
    }
    return 0;
    #endif
}

#if defined(_WIN32) || defined(_WIN64)
#define IS_VALID_SOCKET(x) (x != INVALID_SOCKET)
#define H64CLOSEDSOCK INVALID_SOCKET
#else
#define IS_VALID_SOCKET(x) (x >= 0)
#define H64CLOSEDSOCK -1
#endif

ATTR_UNUSED static inline int sockset_Add(
        h64sockset *set, h64sockfd_t fd, int waittypes
        ) {
    #if defined(_WIN32) || defined(_WIN64) || !defined(CANUSEPOLL)
    if (waittypes == 0)
        return 1;
    int found = 0;
    int i = 0;
    while (i < set->fds_count) {
        if (set->fds[i] == fd) {
            found = 1;
            break;
        }
        i++;
    }
    if (!found) {
        h64sockfd_t *new_fds = realloc(
            set->fds, sizeof(*set->fds) * (set->fds_count + 1)
        );
        if (!new_fds) {
            return 0;
        }
        set->fds = new_fds;
        set->fds[set->fds_count] = fd;
        set->fds_count++;
    }
    if ((waittypes & H64SOCKSET_WAITREAD) != 0)
        FD_SET(fd, &set->readset);
    if ((waittypes & H64SOCKSET_WAITWRITE) != 0)
        FD_SET(fd, &set->writeset);
    if ((waittypes & H64SOCKSET_WAITERROR) != 0)
        FD_SET(fd, &set->errorset);
    return 1;
    #else
    if (set->size == 0)
        if (set->fill + 1 > _pollsmallsetsize)
            if (!_sockset_Expand(set))
                return 0;
    if (set->size == 0) {
        set->smallset[set->fill].fd = fd;
        set->smallset[set->fill].events = waittypes;
    } else {
        set->set[set->fill].fd = fd;
        set->set[set->fill].events = waittypes;
    }
    set->fill++;
    return 1;

    #endif
    return 0;
}

void sockset_Remove(
    h64sockset *set, h64sockfd_t fd
);

void sockset_RemoveWithMask(
    h64sockset *set, h64sockfd_t fd, int waittypes
);

ATTR_UNUSED static inline void sockset_Clear(
        h64sockset *set
        ) {
    #if defined(_WIN32) || defined(_WIN64) || !defined(CANUSEPOLL)
    FD_ZERO(&set->readset);
    FD_ZERO(&set->writeset);
    FD_ZERO(&set->errorset);
    set->fds_count = 0;
    #else
    set->fill = 0;
    #endif
}

ATTR_UNUSED static inline void sockset_Uninit(
        ATTR_UNUSED h64sockset *set
        ) {
    #if defined(_WIN32) || defined(_WIN64) || !defined(CANUSEPOLL)
    if (set->fds) {
        free(set->fds);
        set->fds = NULL;
    }
    set->fds_count = 0;
    #else
    if (set->size != 0) {
        assert(set->set != NULL);
        free(set->set);
        set->set = NULL;
        set->size = 0;
    } else if (set->set != NULL) {
        free(set->set);
        set->set = NULL;
    }
    #endif
}

h64sockfd_t *sockset_GetResultList(
    h64sockset *set, h64sockfd_t *fdbuf, int fdbufsize,
    int waittypes, int *resultcount
);

int sockset_Wait(
    h64sockset *set, int64_t timeout_ms
);

h64socket *sockets_New(int ipv6capable, int tls);

int sockets_Send(
    h64socket *s, const uint8_t *bytes, size_t byteslen
);

int _internal_sockets_ProcessSend(h64socket *s);

void _internal_sockets_UnregisterFromSend(h64socket *s, int lock);

int _internal_sockets_RegisterForSend(h64socket *s, int lock);

int sockets_NeedSend(h64socket *s);

int sockets_Receive(h64socket *s, char *buf, size_t count);

void sockets_Destroy(h64socket *sock);

int sockets_NewPair(h64socket **s1, h64socket **s2);

int sockets_SetNonblocking(h64socket *sock, int nonblocking);

int sockets_IsIPv4(const h64wchar *s, int slen);

int sockets_IsIPv6(const h64wchar *s, int slen);

void sockets_Close(h64socket *s);

#endif  // HORSE64_SOCKETS_H_
