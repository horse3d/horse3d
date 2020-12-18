// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <openssl/ssl.h>
#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#else
#include <arpa/inet.h>
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

#include "nonlocale.h"
#include "secrandom.h"
#include "sockets.h"
#include "threading.h"
#include "widechar.h"


static volatile _Atomic int _sockinitdone = 0;
static SSL_CTX *ssl_ctx = NULL;

__attribute__((constructor)) void _sockinit() {
    if (_sockinitdone)
        return;
    _sockinitdone = 1;
    #if defined(_WIN32) || defined(_WIN64)
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        h64fprintf(stderr, "horsevm: error: WSAStartup() failed\n");
        exit(1);
    }
    #endif
    const SSL_METHOD *m = TLS_method();
    if (m)
        ssl_ctx = SSL_CTX_new(m);
    if (!m || !ssl_ctx ||
            !SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_2_VERSION)) {
        h64fprintf(stderr, "horsevm: error: OpenSSL init failed\n");
        exit(1);
    }

    // Configure pre-TLS 1.3 ciphers, and manually exclude a few:
    STACK_OF(SSL_CIPHER) *stack;
    if (!SSL_CTX_set_cipher_list(
            ssl_ctx, "HIGH:!aNULL:!MD5:!SEED:!RC2:!RC4:!SHA1:!DES:!3DES"
            )) {
        h64fprintf(stderr, "horsevm: error: "
            "OpenSSL SSL_CTX_set_cipher_list() failed\n");
        exit(1);
    }
    char *filtered_cipher_list = NULL;
    stack = SSL_CTX_get_ciphers(ssl_ctx);
    int count = sk_SSL_CIPHER_num(stack);
    int i = 0;
    while (i < count) {
        const SSL_CIPHER *ci = sk_SSL_CIPHER_value(stack, i);
        const char *name = SSL_CIPHER_get_name(ci);
        if ((strstr(name, "GCM") || strstr(name, "CCM")) &&
                !strstr(name, "128")) {
            int oldlen = (
                filtered_cipher_list ? strlen(filtered_cipher_list) : 0
            );
            char *new_cipher_list = realloc(
                filtered_cipher_list,
                (filtered_cipher_list ?
                 strlen(filtered_cipher_list) + 2 + strlen(name) :
                 strlen(name) + 1)
            );
            if (!new_cipher_list) {
                h64fprintf(stderr, "horsevm: error: OOM setting "
                    "OpenSSL cipher list\n");
                exit(1);
            }
            filtered_cipher_list = new_cipher_list;
            if (oldlen > 0) {
                filtered_cipher_list[oldlen] = ':';
                oldlen++;
            }
            memcpy(
                filtered_cipher_list + oldlen,
                name, strlen(name) + 1
            );
        }
        i++;
    }
    if (filtered_cipher_list) {
        if (!SSL_CTX_set_cipher_list(
                ssl_ctx, filtered_cipher_list
                )) {
            h64fprintf(stderr, "horsevm: error: "
            "OpenSSL SSL_CTX_set_cipher_list() failed\n");
            exit(1);
        }
    }

    // Configure TLS 1.3 ciphers:
}

h64socket *sockets_NewBlockingRaw(int v6capable) {
    _sockinit();
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
    {
        int val = 1;
        if (setsockopt(sock->fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                (char *)&val, sizeof(val)) != 0) {
            closesocket(sock->fd);
            free(sock);
            return NULL;
        }
    }
    #endif
    // Enable dual stack:
    if (v6capable) {
        int val = 0;
        if (setsockopt(sock->fd, IPPROTO_IPV6, IPV6_V6ONLY,
                (char *)&val, sizeof(val)) != 0) {
            #if defined(_WIN32) || defined(_WIN64)
            closesocket(sock->fd);
            #else
            close(sock->fd);
            #endif
            free(sock);
            return NULL;
        }
    }
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

int sockets_ConnectClient(
        h64socket *sock, const h64wchar *ip, int64_t iplen, int port
        ) {
    int isip6 = 0;
    if (sockets_IsIPv4(ip, iplen)) {
        isip6 = 0;
        sock->flags &= ~((uint16_t)_SOCKFLAG_ISV6TARGET);
    } else if (sockets_IsIPv6(ip, iplen)) {
        isip6 = 1;
        sock->flags |= _SOCKFLAG_ISV6TARGET;
    } else {
        return H64SOCKERROR_OPERATIONFAILED;
    }
    char *ipu8 = malloc(iplen * 5 + 2);
    if (!ipu8) {
        return H64SOCKERROR_OUTOFMEMORY;
    }
    int64_t ipu8len = 0;
    if (!utf32_to_utf8(
            ip, iplen, ipu8, iplen * 5 + 2,
            &ipu8len, 1
            ) || ipu8len >= iplen * 5 + 2) {
        return H64SOCKERROR_OPERATIONFAILED;
    }
    ipu8[ipu8len] = '\0';
    if ((sock->flags & _SOCKFLAG_CONNECTCALLED) == 0) {
        if (isip6) {
            struct sockaddr_in6 targetaddr = {0};
            targetaddr.sin6_family = AF_INET6;
            targetaddr.sin6_addr = in6addr_loopback;
            #if defined(_WIN32) || defined(_WIN64)
            {
                struct sockaddr_storage addrout = {0};
                int addroutlen = sizeof(addrout);
                char ipinputbuf[INET6_ADDRSTRLEN + 1] = "";
                strncpy(ipinputbuf, ipu8, INET6_ADDRSTRLEN+1);
                ipinputbuf[INET6_ADDRSTRLEN] = 0;
                if (WSAStringToAddress(
                        ipinputbuf, AF_INET6, NULL,
                        (struct sockaddr *)&addrout, &addroutlen)
                        == 0) {
                    memcpy(
                        &targetaddr,
                        &((struct sockaddr_in6 *)&addrout)->sin6_addr,
                        sizeof(targetaddr)
                    );
                } else {
                    return H64SOCKERROR_OPERATIONFAILED;
                }
            }
            #else
            int result = (inet_pton(
                    AF_INET6, ipu8,
                    (struct sockaddr_in6 *) &targetaddr
                ));
            if (result != 1)
                return H64SOCKERROR_OPERATIONFAILED;
            #endif
            sock->flags |= _SOCKFLAG_CONNECTCALLED;
            if (connect(sock->fd,
                    (struct sockaddr *)&targetaddr,
                    sizeof(targetaddr)) < 0) {
                #if defined(_WIN32) || defined(_WIN64)
                if (WSAGetLastError() == WSAEINPROGRESS) {
                    if (WSAGetLastError() == WSAEWOULDBLOCK)
                        sock->flags &= ~(
                            (uint16_t)_SOCKFLAG_CONNECTCALLED
                        );
                    return H64SOCKERROR_INPROGRESS;
                }
                #else
                if (errno == EAGAIN || errno == EINPROGRESS) {
                    if (errno == EAGAIN)  // must call connect again
                        sock->flags &= ~(
                            (uint16_t)_SOCKFLAG_CONNECTCALLED
                        );
                    return H64SOCKERROR_NEEDTOWRITE;
                }
                #endif
                return H64SOCKERROR_OPERATIONFAILED;
            } else {
                goto connectionmustbedone;
            }
        } else {
            struct sockaddr_in targetaddr = {0};
            targetaddr.sin_family = AF_INET;
            targetaddr.sin_addr.s_addr = INADDR_LOOPBACK;
            #if defined(_WIN32) || defined(_WIN64)
            {
                struct sockaddr_storage addrout = {0};
                int addroutlen = sizeof(addrout);
                char ipinputbuf[INET6_ADDRSTRLEN + 1] = "";
                strncpy(ipinputbuf, ipu8, INET6_ADDRSTRLEN+1);
                ipinputbuf[INET6_ADDRSTRLEN] = 0;
                if (WSAStringToAddress(
                        ipinputbuf, AF_INET, NULL,
                        (struct sockaddr *)&addrout, &addroutlen)
                        == 0) {
                    memcpy(
                        &targetaddr,
                        &((struct sockaddr_in *)&addrout)->sin_addr,
                        sizeof(targetaddr)
                    );
                } else {
                    return H64SOCKERROR_OPERATIONFAILED;
                }
            }
            #else
            int result = (inet_pton(
                    AF_INET, ipu8,
                    (struct sockaddr_in *) &targetaddr
                ));
            if (result != 1)
                return H64SOCKERROR_OPERATIONFAILED;
            #endif
            sock->flags |= _SOCKFLAG_CONNECTCALLED;
            if (connect(sock->fd,
                    (struct sockaddr *)&targetaddr,
                    sizeof(targetaddr)) < 0) {
                #if defined(_WIN32) || defined(_WIN64)
                if (WSAGetLastError() == WSAEINPROGRESS) {
                    if (WSAGetLastError() == WSAEWOULDBLOCK)
                        sock->flags &= ~(
                            (uint16_t)_SOCKFLAG_CONNECTCALLED
                        );
                    return H64SOCKERROR_NEEDTOWRITE;
                }
                #else
                if (errno == EAGAIN || errno == EINPROGRESS) {
                    if (errno == EAGAIN)  // must call connect again
                        sock->flags &= ~(
                            (uint16_t)_SOCKFLAG_CONNECTCALLED
                        );
                    return H64SOCKERROR_NEEDTOWRITE;
                }
                #endif
                return H64SOCKERROR_OPERATIONFAILED;
            } else {
                goto connectionmustbedone;
            }
        }
    }
    if ((sock->flags & _SOCKFLAG_CONNECTCALLED) != 0) {
        if ((sock->flags & _SOCKFLAG_KNOWNCONNECTED) != 0) {
            connectionmustbedone: ;
            int connected = 0;
            if ((sock->flags & _SOCKFLAG_ISV6TARGET) == 0) {
                struct sockaddr_in v4addr;
                socklen_t v4size = sizeof(v4addr);
                connected = (getpeername(
                    sock->fd,
                    (struct sockaddr *)&v4addr, &v4size
                ) == 0);
            } else {
                struct sockaddr_in6 v6addr;
                socklen_t v6size = sizeof(v6addr);
                connected = (getpeername(
                    sock->fd,
                    (struct sockaddr *)&v6addr, &v6size
                ) == 0);
            }
            if (!connected) {
                sock->flags &= ~((uint16_t)_SOCKFLAG_CONNECTCALLED);
                return H64SOCKERROR_OPERATIONFAILED;
            }
            sock->flags |= _SOCKFLAG_KNOWNCONNECTED;
        }
        if ((sock->flags & SOCKFLAG_TLS) == 0)
            return H64SOCKERROR_SUCCESS;
        if ((sock->flags & _SOCKFLAG_OUTSTANDINGTLSCONNECT) == 0)
            return H64SOCKERROR_OPERATIONFAILED;
        if (!sock->sslobj) {
            sock->sslobj = SSL_new(ssl_ctx);
            if (!sock->sslobj) {
                return H64SOCKERROR_OUTOFMEMORY;
            }
        }
        if (!SSL_set_fd(sock->sslobj, sock->fd)) {
            return H64SOCKERROR_OUTOFMEMORY;
        }
        int retval = -1;
        if ((retval = SSL_connect(sock->sslobj)) < 0) {
            int err = SSL_get_error(sock->sslobj, retval);
            if (err == SSL_ERROR_WANT_READ)
                return H64SOCKERROR_NEEDTOREAD;
            if (err == SSL_ERROR_WANT_WRITE)
                return H64SOCKERROR_NEEDTOWRITE;
            return H64SOCKERROR_OPERATIONFAILED;
        }
    }
    return H64SOCKERROR_SUCCESS;
}

int sockets_WasEverConnected(h64socket *sock) {
    return ((sock->flags & _SOCKFLAG_KNOWNCONNECTED) != 0);
}

void sockets_Destroy(h64socket *sock) {
    if (!sock)
        return;
    if (sock->sslobj) {
        SSL_free(sock->sslobj);
        sock->sslobj = NULL;
    }
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
        int acceptfd = -1;
        if ((acceptfd = accept(
                te->recv_server->fd, NULL, NULL
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
            uint32_t err = WSAGetLastError();
            if (err == WSAEFAULT || err == WSANOTINITIALISED ||
                    err == WSAEINVAL || err == WSAENOTSOCK ||
                    err == WSAEOPNOTSUPP) {
                goto failure;
            }
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
        if (te->resultconnfd >= 0)
            break;
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
    te.trigger_client = sockets_NewBlockingRaw(1);
    if (!te.recv_server || !te.trigger_client) {
        sockets_FreeSocketPairSetupData(&te);
        #if !defined(NDEBUG) && defined(DEBUG_SOCKETPAIR)
        h64fprintf(stderr,
            "horsevm: warning: sockets_NewPair() failure: "
            "te.recv_server or te.trigger_client creation "
            "failed\n"
        );
        #endif
        return 0;
    }

    // Generate connect key to ensure we got the right client:
    if (!secrandom_GetBytes(te.connectkey, sizeof(te.connectkey))) {
        sockets_FreeSocketPairSetupData(&te);
        #if !defined(NDEBUG) && defined(DEBUG_SOCKETPAIR)
        h64fprintf(stderr,
            "horsevm: warning: sockets_NewPair() failure: "
            "te.connectkey creation failed\n"
        );
        #endif
        return 0;
    }

    // Connect socket pair:
    struct sockaddr_in6 servaddr = {0};
    servaddr.sin6_family = AF_INET6;
    servaddr.sin6_addr = in6addr_loopback;
    struct sockaddr_in servaddr4 = {0};
    servaddr4.sin_family = AF_INET;
    servaddr4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int v4bindused = 0;
    if (bind(te.recv_server->fd, (struct sockaddr *)&servaddr,
             sizeof(servaddr)) < 0) {
        // See if falling back to IPv4 helps with anything.
        // For that, first recreate our sockets for IPv4:
        sockets_Destroy(te.recv_server);
        sockets_Destroy(te.trigger_client);
        te.recv_server = sockets_NewBlockingRaw(0);
        te.trigger_client = sockets_NewBlockingRaw(0);
        if (!te.recv_server || !te.trigger_client) {
            sockets_FreeSocketPairSetupData(&te);
            #if !defined(NDEBUG) && defined(DEBUG_SOCKETPAIR)
            h64fprintf(stderr,
                "horsevm: warning: sockets_NewPair() failure: "
                "te.recv_server or te.trigger_client re-creation "
                "(v4 fallback) failed\n"
            );
            #endif
            return 0;
        }
        v4bindused = 1;
        if (bind(te.recv_server->fd, (struct sockaddr *)&servaddr4,
                 sizeof(servaddr4)) < 0) {
            sockets_FreeSocketPairSetupData(&te);
            #if !defined(NDEBUG) && defined(DEBUG_SOCKETPAIR)
            h64fprintf(stderr,
                "horsevm: warning: sockets_NewPair() failure: "
                "te.recv_server bind() failed%s\n",
                #if defined(__linux__) || defined(__LINUX__)
                (errno == EACCES ? " (EACCES)" :
                 (errno == EADDRINUSE ? " (EADDRINUSE)" :
                 (errno == EBADF ? " (EBADF)" :
                 (errno == EINVAL ? " (EINVAL) " : (
                 (errno == ENOTSOCK ? " (ENOTSOCK)" : (
                 (" (UNKNOWN ERRNO)")
                 )))))))
                #else
                ""
                #endif
            );
            #endif
            return 0;
        }
    }
    if (listen(
            te.recv_server->fd, 2048) < 0
            ) {
        sockets_FreeSocketPairSetupData(&te);
        #if !defined(NDEBUG) && defined(DEBUG_SOCKETPAIR)
        h64fprintf(stderr,
            "horsevm: warning: sockets_NewPair() failure: "
            "te.recv_server listen() failed\n"
        );
        #endif
        return 0;
    }
    #if defined(_WIN32) || defined(_WIN64)
    int len = sizeof(servaddr);
    #else
    socklen_t len = sizeof(servaddr);
    #endif
    if ((v4bindused ||
            getsockname(
            te.recv_server->fd,
            (struct sockaddr *)&servaddr,
            &len) != 0 || (unsigned int)len != sizeof(servaddr)) &&
            (!v4bindused ||
            getsockname(
            te.recv_server->fd,
            (struct sockaddr *)&servaddr4,
            &len) != 0 || (unsigned int)len != sizeof(servaddr4))) {
        sockets_FreeSocketPairSetupData(&te);
        #if !defined(NDEBUG) && defined(DEBUG_SOCKETPAIR)
        h64fprintf(stderr,
            "horsevm: warning: sockets_NewPair() failure: "
            "te.recv_server getsockname() failed\n"
        );
        #endif
        return 0;
    }

    // Connect client and send payload (blocking):
    if (!sockets_SetNonblocking(te.trigger_client, 0)) {
        sockets_FreeSocketPairSetupData(&te);
        #if !defined(NDEBUG) && defined(DEBUG_SOCKETPAIR)
        h64fprintf(stderr,
            "horsevm: warning: sockets_NewPair() failure: "
            "te.triggerclient sockets_SetNonBlocking(0) failed\n"
        );
        #endif
        return 0;
    }
    te.port = (
        v4bindused ? servaddr4.sin_port : servaddr.sin6_port
    );
    assert(te.port > 0);
    thread *accept_thread = thread_Spawn(
        _threadEventAccepter, &te
    );
    if (!accept_thread) {
        sockets_FreeSocketPairSetupData(&te);
        #if !defined(NDEBUG) && defined(DEBUG_SOCKETPAIR)
        h64fprintf(stderr,
            "horsevm: warning: sockets_NewPair() failure: "
            "accept_thread spawn failed\n"
        );
        #endif
        return 0;
    }
    if (connect(
            te.trigger_client->fd, (
                v4bindused ? (struct sockaddr *)&servaddr4 :
                (struct sockaddr *)&servaddr
            ),
            (v4bindused ? sizeof(servaddr4) : sizeof(servaddr))) < 0) {
        te.failure = 1;
        thread_Join(accept_thread);
        sockets_FreeSocketPairSetupData(&te);
        #if !defined(NDEBUG) && defined(DEBUG_SOCKETPAIR)
        h64fprintf(stderr,
            "horsevm: warning: sockets_NewPair() failure: "
            "te.trigger_client connect() failed\n"
        );
        #endif
        return 0;
    }
    if (send(te.trigger_client->fd, te.connectkey,
             sizeof(te.connectkey), 0) < 0) {
        te.failure = 1;
        #if !defined(NDEBUG) && defined(DEBUG_SOCKETPAIR)
        h64fprintf(stderr,
            "horsevm: warning: sockets_NewPair() failure: "
            "te.trigger_client send() failed\n"
        );
        #endif
    }
    thread_Join(accept_thread);
    accept_thread = NULL;
    if (te.failure) {
        sockets_FreeSocketPairSetupData(&te);
        #if !defined(NDEBUG) && defined(DEBUG_SOCKETPAIR)
        h64fprintf(stderr,
            "horsevm: warning: sockets_NewPair() failure: "
            "te.failure = 1, data exchange failed\n"
        );
        #endif
        return 0;
    }

    // Done with payload handling, set client to non-blocking:
    if (!sockets_SetNonblocking(te.trigger_client, 1)) {
        sockets_FreeSocketPairSetupData(&te);
        #if !defined(NDEBUG) && defined(DEBUG_SOCKETPAIR)
        h64fprintf(stderr,
            "horsevm: warning: sockets_NewPair() failure: "
            "te.trigger_client sockets_SetNonblocking(1) failed\n"
        );
        #endif
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
        #if !defined(NDEBUG) && defined(DEBUG_SOCKETPAIR)
        h64fprintf(stderr,
            "horsevm: warning: sockets_NewPair() failure: "
            "sock_two malloc() failed\n"
        );
        #endif
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
        #if !defined(NDEBUG) && defined(DEBUG_SOCKETPAIR)
        h64fprintf(stderr,
            "horsevm: warning: sockets_NewPair() failure: "
            "sock_two sockets_SetNonblocking(1) failed\n"
        );
        #endif
        return 0;
    }
    sockets_FreeSocketPairSetupData(&te);
    *s1 = sock_one;
    *s2 = sock_two;
    return 1;
}

void sockset_Remove(
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

void sockset_RemoveWithMask(
        h64sockset *set, int fd, int waittypes
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
            delset[i].events = (
                (unsigned int)delset[i].events & (~(
                    (unsigned int)waittypes
                ))
            );
            if (delset[i].events == 0) {
                if (i + 1 < count)
                    memcpy(
                        &delset[i],
                        &delset[i + 1],
                        sizeof(*set->set) * (count - i - 1)
                    );
                set->fill--;
            }
            return;
        }
        i++;
    }
    #endif
}

int sockset_Wait(
        h64sockset *set, int64_t timeout_ms
        ) {
    #if defined(_WIN32) || defined(_WIN64) || !defined(CANUSEPOLL)
    struct timeval ts = {0};
    if (timeout_ms > 0) {
        ts.tv_sec = (timeout_ms / 1000LL);
        ts.tv_usec = (timeout_ms % 1000LL) * 1000LL;
    }
    int result = select(
        FD_SETSIZE, &set->readset, &set->writeset,
        &set->errorset, (timeout_ms != 0 ? &ts : NULL)
    );
    return (result > 0 ? result : 0);
    #else
    if (timeout_ms <= 0)
        timeout_ms = -1;
    set->resultfill = 0;
    struct pollfd *pollset = (
        set->size == 0 ? (struct pollfd*)set->smallset : set->set
    );
    struct pollfd *resultset = (
        set->size == 0 ? (struct pollfd*)set->smallresult : set->result
    );
    int timeouti32 = (
        (int64_t)timeout_ms > (int64_t)INT32_MAX ?
        (int32_t)INT32_MAX : (int32_t)timeout_ms
    );
    int result = poll(pollset, set->fill, timeouti32);
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
        assert(set->resultfill > 0);
    }
    return (result > 0 ? result : 0);
    #endif
}

int _sockset_Expand(
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
        if (newsize < _pollsmallsetsize * 2)
            newsize = _pollsmallsetsize * 2;
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


int sockets_IsIPv4(const h64wchar *s, int slen) {
    int dots = 0;
    int currentnumberlen = 0;
    while (1) {
        if (slen <= 0) {
            if (currentnumberlen < 1 || dots != 3)
                return 0;
            return 1;
        }
        if (*s >= '0' && *s <= '9') {
            currentnumberlen++;
            if (currentnumberlen > 3)
                return 0;
        } else if (*s == '.') {
            if (currentnumberlen < 1 || dots >= 3)
                return 0;
            dots++;
            currentnumberlen = 0;
        } else {
            return 0;
        }
        s++;
        slen--;
    }
}

int sockets_IsIPv6(const h64wchar *s, int slen) {
    int lastcolonwasdoublecolon = 0;
    int doublecolons = 0;
    int colons = 0;
    int currentnumberlen = 0;
    while (1) {
        if (slen <= 0) {
            if ((currentnumberlen < 1 && (doublecolons == 0 ||
                    !lastcolonwasdoublecolon)) ||
                    (colons != 7 && doublecolons == 0) ||
                    (colons >= 7 && doublecolons != 0) ||
                    (doublecolons != 0 && doublecolons != 1))
                return 0;
            return 1;
        }
        if ((*s >= '0' && *s <= '9') ||
                (*s >= 'a' && *s <= 'f') ||
                (*s >= 'A' && *s <= 'F')) {
            currentnumberlen++;
            if (currentnumberlen > 3)
                return 0;
        } else if (*s == ':' && slen > 1 && *(s + 1) == ':') {
            if (doublecolons > 0 || colons >= 7)
                return 0;
            lastcolonwasdoublecolon = 1;
            doublecolons++;
            currentnumberlen = 0;
        } else if (*s == ':' && (slen < 2 || *(s + 1) != ':')) {
            if (currentnumberlen < 1 || colons >= 7 ||
                    (colons >= 6 && doublecolons >= 0))
                return 0;
            lastcolonwasdoublecolon = 0;
            colons++;
            currentnumberlen = 0;
        } else {
            return 0;
        }
        s++;
        slen--;
    }
}