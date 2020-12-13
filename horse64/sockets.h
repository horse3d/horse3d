// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SOCKFLAG_TLS 0x1
#define SOCKFLAG_SERVER 0x2

#if defined(USE_POLL_ON_UNIX) && USE_POLL_ON_UNIX != 0
#define CANUSEPOLL
#else
#ifdef CANUSEPOLL
#undef CANUSEPOLL
#endif
#endif

typedef struct h64socket {
    int fd;
    uint8_t flags;
#if defined(_WIN32) || defined(_WIN64)
    HANDLE sock_event_read, sock_event_write;
#endif
    char *sendbuf;
    size_t sendbufsize, sendbuffill;
} h64socket;

typedef struct h64threadevent h64threadevent;

#define _pollsmallsetsize 12

typedef struct h64sockset {
    #if defined(_WIN32) || defined(_WIN64) || !defined(CANUSEPOLL)
    fd_set readset;
    fd_set errorset;
    fd_set writeset;
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
#define H64SOCKSET_WAITERR 0x4
#else
#define H64SOCKSET_WAITREAD POLLIN
#define H64SOCKSET_WAITWRITE POLLOUT
#define H64SOCKSET_WAITERROR (POLLERR | POLLHUP)

#endif

ATTR_UNUSED static inline int sockset_Expand(
        ATTR_UNUSED h64sockset *set
        ) {
    #if defined(_WIN32) || defined(_WIN64) || !defined(CANUSEPOLL)
    return 0;
    #else
    int oldsize = _pollsmallsetsize;
    if (set->size != 0)
        oldsize = set->size;
    int newsize = set->fill + 16;
    if (newsize < set->size * 2)
        newsize = set->size * 2;
    if (set->size == 0) {
        set->set = malloc(
            sizeof(*set->set) * newsize
        );
        if (set->set) {
            memcpy(
                set->set, set->smallset,
                sizeof(*set->set) * _pollsmallsetsize
            );
        } else {
            return 0;
        }
    } else {
        struct pollfd *newresult = malloc(
            sizeof(*set->result) * newsize
        );
        if (!newresult)
            return 0;
        free(set->result);
        set->result = newresult;
        struct pollfd *newset = realloc(
            set->set, sizeof(*set->set) * newsize
        );
        if (!newset) {
            if (set->set == NULL) {
                free(set->result);
                set->result = NULL;
            }
            return 0;
        }
        set->set = newset;
    }
    set->size = newsize;
    memset(
        &set->set[oldsize], 0,
        sizeof(*set->set) * (newsize - oldsize)
    );
    #endif
    return 1;
}

ATTR_UNUSED static inline int sockset_GetResult(
        h64sockset *set, int fd, int waittypes
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
            return (checkset[i].events & waittypes);
        }
        i++;
    }
    return 0;
    #endif
}

ATTR_UNUSED static inline int sockset_Add(
        h64sockset *set, int fd, int waittypes
        ) {
    #if defined(_WIN32) || defined(_WIN64) || !defined(CANUSEPOLL)
    if ((waittypes & H64SOCKSET_WAITREAD) != 0) {
        FD_SET(fd, &set->readset);
        return 1;
    }
    if ((waittypes & H64SOCKSET_WAITWRITE) != 0) {
        FD_SET(fd, &set->writeset);
        return 1;
    }
    if ((waittypes & H64SOCKSET_WAITERROR) != 0) {
        FD_SET(fd, &set->errorset);
        return 1;
    }
    #else
    if (set->size == 0)
        if (set->fill + 1 > _pollsmallsetsize)
            sockset_Expand(set);
    if (set->size == 0) {
        set->smallset[set->fill].fd = fd;
        set->smallset[set->fill].events = waittypes;
    } else {
        set->set[set->fill].fd = fd;
        set->set[set->fill].revents = waittypes;
    }
    set->fill++;

    #endif
    return 0;
}

ATTR_UNUSED static void sockset_Remove(
        h64sockset *set, int fd
        ) {
    #if defined(_WIN32) || defined(_WIN64) || !defined(CANUSEPOLL)
    FD_CLR(fd, &set->readset);
    FD_CLR(fd, &set->writeset);
    FD_CLR(fd, &set->errorset);
    return;
    #else
    int i = 0;
    const int count = set->fill;
    struct pollfd *delset = (
        set->size == 0 ? (struct pollfd*)set->smallset : set->set
    );
    while (i < count) {
        if (delset[i].fd == fd) {
            if (i + 1 < count)
                memcpy(
                    &delset[i],
                    &delset[i + 1],
                    sizeof(*set->set) * (count - i - 1)
                );
            set->fill--;
            return;
        }
        i++;
    }
    #endif
}

ATTR_UNUSED static inline void sockset_Clear(
        h64sockset *set
        ) {
    #if defined(_WIN32) || defined(_WIN64) || !defined(CANUSEPOLL)
    FD_ZERO(&set->readset);
    FD_ZERO(&set->writeset);
    FD_ZERO(&set->errorset);
    #else
    set->fill = 0;
    #endif
}

ATTR_UNUSED static inline void sockset_Uninit(
        ATTR_UNUSED h64sockset *set
        ) {
    #if defined(_WIN32) || defined(_WIN64) || !defined(CANUSEPOLL)
    return;
    #else
    if (set->size != 0) {
        assert(set->set != NULL);
        free(set->set);
        set->set = NULL;
        set->size = 0;
    }
    #endif
}

ATTR_UNUSED static inline int sockset_Wait(
        h64sockset *set, int64_t timeout_ms
        ) {
    #if defined(_WIN32) || defined(_WIN64) || !defined(CANUSEPOLL)
    struct timeval ts = {0};
    if (timeout_ms != 0) {
        ts.tv_sec = (timeout_ms / 1000LL);
        ts.tv_usec = (timeout_ms % 1000LL) * 10000000LL;
    }
    int result = select(
        FD_SETSIZE, &set->readset, &set->writeset,
        &set->errorset, (timeout_ms != 0 ? &ts : NULL)
    );
    return (result > 0 ? result : 0);
    #else
    set->resultfill = 0;
    struct pollfd *pollset = (
        set->size == 0 ? (struct pollfd*)set->smallset : set->set
    );
    struct pollfd *resultset = (
        set->size == 0 ? (struct pollfd*)set->smallresult : set->result
    );
    int result = poll(
        pollset, set->fill, (
            (int64_t)timeout_ms > (int64_t)INT32_MAX ?
            (int32_t)INT32_MAX : (int32_t)timeout_ms
        )
    );
    set->resultfill = 0;
    if (result > 0) {
        int i = 0;
        while (i < set->fill) {
            if (pollset[i].revents != 0) {
                memcpy(
                    &resultset[set->resultfill],
                    &pollset[i],
                    sizeof(*set->set)
                );
                set->resultfill++;
            }
            i++;
        }
    }
    return (result > 0 ? result : 0);
    #endif
}

h64socket *sockets_New(int ipv6capable, int tls);

int sockets_Send(
    h64socket *s, const uint8_t *bytes, size_t byteslen
);

void sockets_Destroy(h64socket *sock);

int sockets_NewPair(h64socket **s1, h64socket **s2);

int sockets_SetNonblocking(h64socket *sock, int nonblocking);

#endif  // HORSE64_SOCKETS_H_