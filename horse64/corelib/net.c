// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>

#include "corelib/errors.h"
#include "corelib/net.h"
#include "sockets.h"
#include "stack.h"
#include "vmexec.h"
#include "widechar.h"

/// @module net Networking streams to access the local network and internet.

struct netlib_connect_asyncprogress {
    char *utf8host;
    int utf8hostlen;
};

int netlib_connect(
        h64vmthread *vmthread
        ) {
    /**
     * Connect to a different host using a TCP/IP connection, optionally
     * using a TLS/SSL encryption.
     *
     * @func connect
     * @param host the host to connect to, either by name or by ip
     * @param port the port of the host to connect to
     * @param encrypt=false whether to connect using TLS/SSL encryption
     * @returns a @see{network stream|net.connect}
     */
    assert(STACK_TOP(vmthread->stack) >= 3);

    valuecontent *vcpath = STACK_ENTRY(vmthread->stack, 0);
    char *pathstr = NULL;
    int64_t pathlen = 0;
    int pathu32 = 0;
    if (vcpath->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue *)vcpath->ptr_value)->type == H64GCVALUETYPE_STRING) {
        pathstr = (char *)((h64gcvalue *)vcpath->ptr_value)->str_val.s;
        pathlen = ((h64gcvalue *)vcpath->ptr_value)->str_val.len;
        pathu32 = 1;
    } else if (vcpath->type == H64VALTYPE_SHORTSTR) {
        pathstr = (char *)vcpath->shortstr_value;
        pathlen = vcpath->shortstr_len;
        pathu32 = 1;
    } else {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "host or ip must be a string"
        );
    }
    char *utf8host = malloc(pathlen * 6 + 1);
    if (!utf8host) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OUTOFMEMORYERROR,
            "out of memory on host utf32 to utf8 conversion"
        );
    }
    int64_t utf8hostbuflen = pathlen * 6 + 1;
    int64_t utf8hostlen = 0;
    if (!utf32_to_utf8(
            (h64wchar *)pathstr, pathlen,
            utf8host, utf8hostbuflen,
            &utf8hostlen, 1
            )) {
        free(utf8host);
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OUTOFMEMORYERROR,
            "out of memory on host utf32 to utf8 conversion"
        );
    }
    if (utf8hostlen >= utf8hostbuflen) {
        free(utf8host);
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OUTOFMEMORYERROR,
            "out of memory on host utf32 to utf8 conversion"
        );
    }

    h64socket *sock = sockets_New(1, 1);
    if (!sock) {
        free(utf8host);
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OUTOFMEMORYERROR,
            "out of memory during socket creation"
        );
    }

    return 1;
}

int netlib_RegisterFuncsAndModules(h64program *p) {
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
    p->func[idx].async_progress_struct_size = (
        sizeof(struct netlib_connect_asyncprogress)
    );

    return 1;
}