// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <errno.h>
#include <math.h>
#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#endif
#include <openssl/ssl.h>

#include "asyncsysjob.h"
#include "corelib/errors.h"
#include "corelib/net.h"
#include "gcvalue.h"
#include "nonlocale.h"
#include "poolalloc.h"
#include "sockets.h"
#include "stack.h"
#include "valuecontentstruct.h"
#include "vmexec.h"
#include "vmschedule.h"
#include "widechar.h"

/// @module net Networking streams to access the local network and internet.


typedef struct _connectionobj_cdata {
    h64socket *connection;
} __attribute__((packed)) _connectionobj_cdata;

struct netlib_connect_asyncprogress {
    void (*abortfunc)(void *dataptr);
    h64asyncsysjob *resolve_job;
    h64socket *connection;
    uint8_t failedv6connect;
};

struct netlib_read_asyncprogress {
    void (*abortfunc)(void *dataptr);
    uint8_t startedread;
    char *readbuf;
    int64_t readbytes, readalloc;
    int64_t wantamount;
    int wantendsequencelen;
    char *wantendsequence;
    h64socket *connection;
};

void _netlib_connect_abort(void *dataptr) {
    struct netlib_connect_asyncprogress *adata = dataptr;
    if (adata->resolve_job) {
        asyncjob_AbandonJob(adata->resolve_job);
        adata->resolve_job = NULL;
    }
    if (adata->connection) {
        sockets_Destroy(adata->connection);
        adata->connection = NULL;
    }
}

int netlib_isip(h64vmthread *vmthread) {
    /**
     * Check if a given @see(string) refers to an IPv4 or IPv6 address,
     * rather than some other form of hostname. Using an IP for
     * @see(net.connect) will ensure that no DNS lookup is required.
     *
     * @func isip
     * @param hostorip the string for which to check whether it's an IP
     * @returns `yes` if string is an IP, otherwise `no`
     */
    assert(STACK_TOP(vmthread->stack) >= 1);

    valuecontent *vcpath = STACK_ENTRY(vmthread->stack, 0);
    char *hoststr = NULL;
    int64_t hostlen = 0;
    if (vcpath->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue *)vcpath->ptr_value)->type ==
            H64GCVALUETYPE_STRING) {
        hoststr = (char *)((h64gcvalue *)vcpath->ptr_value)->str_val.s;
        hostlen = ((h64gcvalue *)vcpath->ptr_value)->str_val.len;
    } else if (vcpath->type == H64VALTYPE_SHORTSTR) {
        hoststr = (char *)vcpath->shortstr_value;
        hostlen = vcpath->shortstr_len;
    } else {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "host or ip must be a string"
        );
    }

    valuecontent *vc = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vc);
    valuecontent_Free(vc);
    memset(vc, 0, sizeof(*vc));
    vc->type = H64VALTYPE_BOOL;
    vc->int_value = (
        sockets_IsIPv4((h64wchar *)hoststr, hostlen) ||
        sockets_IsIPv6((h64wchar *)hoststr, hostlen)
    );
    return 1;
}

/**
 * A network stream class, returned from @see{net.connect}.
 *
 * @class stream
 */

int netlib_stream_write(h64vmthread *vmthread) {  // net.stream.write()
    /**
     * Write to a @see{network stream|net.stream}
     *
     * @funcattr stream connect
     * @param data the data to write to the stream,
     *  which must be a @see{bytes} or @see{string} value.
     */
    assert(STACK_TOP(vmthread->stack) >= 2);

}

int netlib_stream_read(h64vmthread *vmthread) {  // net.stream.read()
    /**
     * Read from a @see{network stream|net.stream}
     *
     * @funcattr stream connect
     * @param len=-1 the amount of bytes to read
     * @param upto=none if set to a bytes value,
     *   when encountering the given `upto` bytes
     *   sequence reading will stop and return only
     *   data up to and including the sequence. If
     *   `len` is also set, then reading will occur
     *   to either the first `upto` occurrence or
     *   to the maximum `len`, whichever is first.
     * @returns a @see{network stream|net.connect}
     */
    assert(STACK_TOP(vmthread->stack) >= 3);

    struct netlib_read_asyncprogress *asprogress = (
        vmthread->foreground_async_work_dataptr
    );
    assert(asprogress != NULL);

    valuecontent *vstream = STACK_ENTRY(vmthread->stack, 2);
    assert(
        vstream->type == H64VALTYPE_GCVAL ||
        ((h64gcvalue *)vstream->ptr_value)->type ==
            H64GCVALUETYPE_OBJINSTANCE
    );
    _connectionobj_cdata *cdata = (
        ((h64gcvalue *)vstream->ptr_value)->cdata
    );

    if (!asprogress->startedread) {
        valuecontent *vclen = STACK_ENTRY(vmthread->stack, 0);
        int64_t len = -1;
        if (likely(vclen->type == H64VALTYPE_INT64)) {
            if (vclen->int_value >= 0)
                len = vclen->int_value;
        } else if (vclen->type == H64VALTYPE_FLOAT64) {
            int64_t flen = clamped_round(vclen->float_value);
            if (flen >= 0)
                len = flen;
        } else if (vclen->type == H64VALTYPE_UNSPECIFIED_KWARG) {
            len = -1;
        } else {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_TYPEERROR,
                "len must be a number"
            );
        }

        char *upto = NULL;
        int64_t upto_len = -1;
        valuecontent *vcupto = STACK_ENTRY(vmthread->stack, 1);
        if (likely(vcupto->type == H64VALTYPE_UNSPECIFIED_KWARG ||
                vcupto->type == H64VALTYPE_NONE)) {
            // Nothing to do.
        } else if (vcupto->type == H64VALTYPE_GCVAL &&
                ((h64gcvalue *)vcupto->ptr_value)->type ==
                    H64GCVALUETYPE_BYTES) {
            upto = ((h64gcvalue *)vcupto->ptr_value)->bytes_val.s;
            upto_len = (
                ((h64gcvalue *)vcupto->ptr_value)->bytes_val.len
            );
        } else if (vcupto->type == H64VALTYPE_SHORTBYTES) {
            upto = vcupto->shortbytes_value;
            upto_len = vcupto->shortbytes_len;
        } else {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_TYPEERROR,
                "upto must be none or bytes"
            );
        }
        assert(!asprogress->wantendsequence);
        if (upto && upto_len > 0) {
            asprogress->wantendsequence = malloc(
                upto_len
            );
            if (!asprogress->wantendsequence) {
                return vmexec_ReturnFuncError(
                    vmthread, H64STDERROR_OUTOFMEMORYERROR,
                    "out of memory allocating upto copy"
                );
            }
            memcpy(
                asprogress->wantendsequence, upto,
                upto_len
            );
            asprogress->wantendsequencelen = upto_len;
        }
        asprogress->wantamount = len;
    }
    // (Re?) attempt read:
    if (asprogress->readbytes < asprogress->wantamount ||
            asprogress->wantamount < 0) {
        int64_t readnow = -1;
        if (asprogress->wantamount >= 0)
            readnow = asprogress->wantamount - asprogress->readbytes;
        if (readnow < 0 || readnow > 1024)
            readnow = 1024;
        if (readnow + asprogress->readbytes > asprogress->readalloc) {
            int64_t new_alloc = asprogress->readalloc * 2;
            if (new_alloc < 1024)
                new_alloc = 1024;
            char *newbuf = realloc(
                asprogress->readbuf, new_alloc
            );
            if (!newbuf) {
                sockets_Close(cdata->connection);
                return vmexec_ReturnFuncError(
                    vmthread, H64STDERROR_OUTOFMEMORYERROR,
                    "alloc failure resizing receive buffer"
                );
            }
        }
        int result = sockets_Receive(
            cdata->connection,
            asprogress->readbuf + asprogress->readbytes,
            readnow
        );
        if (result < 0) {
            if (result == H64SOCKERROR_NEEDTOWRITE) {

            }
        }
    }
}

int netlib_connect(h64vmthread *vmthread) {
    /**
     * Get a network stream, which behind the scenes
     * connects to a target host using a TCP/IP connection and
     * optionally using a TLS/SSL encryption.
     *
     * @func connect
     * @param host the host to connect to, either by name or by ip
     * @param port the port of the host to connect to
     * @param encrypt=false whether to connect using TLS/SSL encryption
     * @raises ResourcError raised when host name resolution or connection
     *    fails, for whatever reason (other than local out of memory).
     * @returns a @see{network stream|net.stream}
     */
    assert(STACK_TOP(vmthread->stack) >= 3);

    struct netlib_connect_asyncprogress *asprogress = (
        vmthread->foreground_async_work_dataptr
    );
    assert(asprogress != NULL);

    valuecontent *vcpath = STACK_ENTRY(vmthread->stack, 0);
    char *hoststr = NULL;
    int64_t hostlen = 0;
    if (vcpath->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue *)vcpath->ptr_value)->type ==
            H64GCVALUETYPE_STRING) {
        hoststr = (char *)((h64gcvalue *)vcpath->ptr_value)->str_val.s;
        hostlen = ((h64gcvalue *)vcpath->ptr_value)->str_val.len;
    } else if (vcpath->type == H64VALTYPE_SHORTSTR) {
        hoststr = (char *)vcpath->shortstr_value;
        hostlen = vcpath->shortstr_len;
    } else {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "host or ip must be a string"
        );
    }
    valuecontent *vcport = STACK_ENTRY(vmthread->stack, 1);
    int32_t port = 0;
    if (vcport->type == H64VALTYPE_INT64) {
        int64_t no = vcport->int_value;
        if (no < 1 || no > INT16_MAX) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_TYPEERROR,
                "port number is out of range"
            );
        }
        port = no;
    } else if (vcport->type == H64VALTYPE_FLOAT64) {
        int64_t no = round(vcport->int_value);
        if (no < 1 || no > INT16_MAX) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_TYPEERROR,
                "port number is out of range"
            );
        }
        port = no;
    } else {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "port must be a number"
        );
    }
    valuecontent *vcencrypt = STACK_ENTRY(vmthread->stack, 2);
    int32_t encrypt = 0;
    if (vcencrypt->type == H64VALTYPE_BOOL) {
        encrypt = (vcencrypt->int_value != 0);
    } else if (vcencrypt->type == H64VALTYPE_UNSPECIFIED_KWARG) {
        encrypt = 0;
    } else {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "encrypted must be a boolean"
        );
    }

    if (asprogress->resolve_job == NULL) {
        #ifndef NDEBUG
        if (_vmsockets_debug) {
            char *hosttmp = malloc(hostlen * 5 + 2);
            if (hosttmp) {
                int64_t hosttmpoutlen = 0;
                int result = utf32_to_utf8(
                    (h64wchar *)hoststr, hostlen,
                    hosttmp, hostlen * 5 + 2,
                    &hosttmpoutlen, 0, 0
                );
                if (!result) {
                    hosttmp[0] = '\0';
                } else {
                    hosttmp[hosttmpoutlen] = '\0';
                }
            }
            h64fprintf(stderr, "horsevm: debug: "
                "net.connect: host %s port %d encrypted %d\n",
                hosttmp, port, encrypt);
            if (hosttmp)
                free(hosttmp);
        }
        #endif
        asprogress->resolve_job = (
            asyncjob_CreateEmpty()
        );
        if (!asprogress->resolve_job) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_OUTOFMEMORYERROR,
                "out of memory during host lookup"
            );
        }
        asprogress->resolve_job->type = (
            ASYNCSYSJOB_HOSTLOOKUP
        );
        asprogress->resolve_job->hostlookup.host = (
            malloc(hostlen * sizeof(h64wchar))
        );
        if (!asprogress->resolve_job->hostlookup.host) {
            asyncjob_Free(asprogress->resolve_job);  /// still owned by us
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_OUTOFMEMORYERROR,
                "out of memory during host lookup"
            );
        }
        memcpy(
            asprogress->resolve_job->hostlookup.host,
            hoststr, hostlen * sizeof(h64wchar)
        );
        asprogress->resolve_job->hostlookup.hostlen = hostlen;
        int result = asyncjob_RequestAsync(
            vmthread, asprogress->resolve_job
        );
        #ifndef NDEBUG
        if (_vmsockets_debug)
            h64fprintf(stderr, "horsevm: debug: "
                "net.connect: posted resolve job -> result: %d\n",
                result);
        #endif
        if (!result) {
            asyncjob_Free(asprogress->resolve_job);  /// still owned by us
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_OUTOFMEMORYERROR,
                "out of memory during host lookup"
            );
        }
        // Note: async job handling owns asprogress->resolved_job now.
        return vmschedule_SuspendFunc(
            vmthread, SUSPENDTYPE_ASYNCSYSJOBWAIT,
            (uintptr_t)(asprogress->resolve_job)
        );
    } else if (asprogress->resolve_job != NULL &&
            !asyncjob_IsDone(asprogress->resolve_job)) {
        return vmschedule_SuspendFunc(
            vmthread, SUSPENDTYPE_ASYNCSYSJOBWAIT,
            (uintptr_t)(asprogress->resolve_job)
        );
    }

    assert(asprogress->resolve_job != NULL &&
            asprogress->resolve_job->done);
    if (asprogress->resolve_job->failed_external) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_RESOURCEERROR,
            "host name resolution failed"
        );
    }

    if (!asprogress->connection) {
        asprogress->connection = sockets_New(1, encrypt);
        if (!asprogress->connection) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_OUTOFMEMORYERROR,
                "out of memory during socket creation"
            );
        }
        #ifndef NDEBUG
        if (_vmsockets_debug)
            h64fprintf(stderr, "horsevm: debug: "
                "net.connect: created socket -> fd %d\n",
                asprogress->connection->fd);
        #endif
    }

    if (!asprogress->failedv6connect &&
            asprogress->resolve_job->hostlookup.resultip6len > 0) {
        #ifndef NDEBUG
        if (_vmsockets_debug)
            h64fprintf(stderr, "horsevm: debug: "
                "net.connect: fd %d connecting via IPv6...\n",
                asprogress->connection->fd);
        #endif
        int result = sockets_ConnectClient(
            asprogress->connection,
            asprogress->resolve_job->hostlookup.resultip6,
            asprogress->resolve_job->hostlookup.resultip6len,
            port
        );
        if (result == H64SOCKERROR_NEEDTOWRITE) {
            return vmschedule_SuspendFunc(
                vmthread, SUSPENDTYPE_SOCKWAIT_WRITABLEORERROR,
                (uintptr_t)(asprogress->connection->fd)
            );
        } else if (result == H64SOCKERROR_NEEDTOREAD) {
            return vmschedule_SuspendFunc(
                vmthread, SUSPENDTYPE_SOCKWAIT_READABLEORERROR,
                (uintptr_t)(asprogress->connection->fd)
            );
        } else if (result == H64SOCKERROR_OUTOFMEMORY) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_OUTOFMEMORYERROR,
                "out of memory during net.connect()"
            );
        } else if (result != H64SOCKERROR_SUCCESS) {
            if (asprogress->resolve_job->hostlookup.resultip4len <= 0)
                return vmexec_ReturnFuncError(
                    vmthread, H64STDERROR_RESOURCEERROR,
                    "connection failed"
                );
            asprogress->failedv6connect = 1;
        } else {
            goto connectionisdone;
        }
    }
    if ((asprogress->failedv6connect ||
            asprogress->resolve_job->hostlookup.resultip6len == 0) &&
            asprogress->resolve_job->hostlookup.resultip4len > 0) {
        #ifndef NDEBUG
        if (_vmsockets_debug)
            h64fprintf(stderr, "horsevm: debug: "
                "net.connect: fd %d connecting via IPv4...\n",
                asprogress->connection->fd);
        #endif
        int result = sockets_ConnectClient(
            asprogress->connection,
            asprogress->resolve_job->hostlookup.resultip4,
            asprogress->resolve_job->hostlookup.resultip4len,
            port
        );
        if (result == H64SOCKERROR_NEEDTOWRITE) {
            return vmschedule_SuspendFunc(
                vmthread, SUSPENDTYPE_SOCKWAIT_WRITABLEORERROR,
                (uintptr_t)(asprogress->connection->fd)
            );
        } else if (result == H64SOCKERROR_NEEDTOREAD) {
            return vmschedule_SuspendFunc(
                vmthread, SUSPENDTYPE_SOCKWAIT_READABLEORERROR,
                (uintptr_t)(asprogress->connection->fd)
            );
        } else if (result == H64SOCKERROR_OUTOFMEMORY) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_OUTOFMEMORYERROR,
                "out of memory during net.connect()"
            );
        } else if (result != H64SOCKERROR_SUCCESS) {
            if (asprogress->resolve_job->hostlookup.resultip4len <= 0)
                return vmexec_ReturnFuncError(
                    vmthread, H64STDERROR_RESOURCEERROR,
                    "connection failed"
                );
            asprogress->failedv6connect = 1;
        } else {
            connectionisdone: ;
            #ifndef NDEBUG
            if (_vmsockets_debug)
                h64fprintf(stderr, "horsevm: debug: "
                    "net.connect: fd %d connected!\n",
                    asprogress->connection->fd);
            #endif
            // Return connection:
            valuecontent *vc = STACK_ENTRY(vmthread->stack, 0);
            DELREF_NONHEAP(vc);
            valuecontent_Free(vc);
            memset(vc, 0, sizeof(*vc));
            vc->type = H64VALTYPE_GCVAL;
            vc->ptr_value = (
                (h64gcvalue *)poolalloc_malloc(
                    vmthread->heap, 0
                )
            );
            if (!vc->ptr_value) {
                return vmexec_ReturnFuncError(
                    vmthread, H64STDERROR_OUTOFMEMORYERROR,
                    "out of memory allocating stream"
                );
            }
            memset(vc->ptr_value, 0, sizeof(h64gcvalue));
            ((h64gcvalue *)vc->ptr_value)->type = (
                H64GCVALUETYPE_OBJINSTANCE
            );
            ((h64gcvalue *)vc->ptr_value)->class_id = (
                vmthread->vmexec_owner->program->_net_stream_class_idx
            );
            _connectionobj_cdata *cdata = malloc(sizeof(*cdata));
            if (!cdata) {
                poolalloc_free(vmthread->heap, vc->ptr_value);
                vc->ptr_value = NULL;
                return vmexec_ReturnFuncError(
                    vmthread, H64STDERROR_OUTOFMEMORYERROR,
                    "out of memory allocating stream"
                );
            }
            memset(cdata, 0, sizeof(*cdata));
            cdata->connection = asprogress->connection;
            asprogress->connection = NULL;
            if (asprogress->resolve_job) {
                asyncjob_AbandonJob(asprogress->resolve_job);
                asprogress->resolve_job = NULL;
            }
            vmthread_FreeAsyncForegroundWorkWithoutAbort(vmthread);
            return 1;
        }
    }
    return vmexec_ReturnFuncError(
        vmthread, H64STDERROR_RESOURCEERROR,
        "connection failed"
    );
}

int netlib_RegisterFuncsAndModules(h64program *p) {
    // stream class:
    p->_net_stream_class_idx = h64program_AddClass(
        p, "stream", NULL, "net", "core.horse64.org"
    );
    if (p->_net_stream_class_idx < 0)
        return 0;

    // net.connect:
    const char *net_connect_kw_arg_name[] = {
        NULL, NULL, "encrypt"
    };
    int64_t idx;
    idx = h64program_RegisterCFunction(
        p, "connect", &netlib_connect,
        NULL, 3, net_connect_kw_arg_name,  // fileuri, args
        "net", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    // net.isip:
    p->func[idx].async_progress_struct_size = (
        sizeof(struct netlib_connect_asyncprogress)
    );
    idx = h64program_RegisterCFunction(
        p, "isip", &netlib_isip,
        NULL, 1, NULL,  // fileuri, args
        "net", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    // net.stream.read method:
    const char *netlib_stream_read_kw_arg_name[] = {
        "len", "upto"
    };
    idx = h64program_RegisterCFunction(
        p, "read", &netlib_stream_read,
        NULL, 2, netlib_stream_read_kw_arg_name,  // fileuri, args
        "net", "core.horse64.org", 1, p->_net_stream_class_idx
    );
    if (idx < 0)
        return 0;

    // net.stream.write method:
    const char *netlib_stream_write_kw_arg_name[] = {
        NULL
    };
    idx = h64program_RegisterCFunction(
        p, "write", &netlib_stream_write,
        NULL, 1, netlib_stream_write_kw_arg_name,  // fileuri, args
        "net", "core.horse64.org", 1, p->_net_stream_class_idx
    );
    if (idx < 0)
        return 0;

    return 1;
}