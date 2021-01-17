// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "bytecode.h"
#include "corelib/errors.h"
#include "corelib/uri.h"
#include "gcvalue.h"
#include "poolalloc.h"
#include "stack.h"
#include "uri32.h"
#include "valuecontentstruct.h"
#include "vmexec.h"
#include "widechar.h"


/// @module uri Parse and process URIs of any kind.

typedef struct _uriobj_cdata {
    void (*on_destroy)(h64gcvalue *uriobj);
    uri32info *uinfo;
} __attribute__((packed)) _uriobj_cdata;

static const char *urimembernames[] = {
    "path", "port", "host", "protocol"
};

typedef enum h64urimember {
    H64URIMEMBER_PATH = 0,
    H64URIMEMBER_PORT,
    H64URIMEMBER_HOST,
    H64URIMEMBER_PROTOCOL,
    H64URIMEMBER_TOTAL
} h64urimember;


int urilib_parse(
        h64vmthread *vmthread
        ) {
    /**
     * Parse the given URI, and return a @see{URI object|uri.uri} that
     * can be used to access the individual components like path,
     * port, and so on.
     *
     * @func parse
     * @param uri the URI to be parsed as a @see{string}
     * @param default_protocol="https" the default protocol to assume
     *     when the URI looks like a remote target, but has no protocol
     *     specified
     * @param guess_port=true whether to insert commonly known protocol
     *     ports if missing
     * @returns a @see{URI object|uri.uri}
     */
    assert(STACK_TOP(vmthread->stack) >= 3);

    valuecontent *vcpath = STACK_ENTRY(vmthread->stack, 0);
    char *pathstr = NULL;
    int64_t pathlen = 0;
    if (vcpath->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue *)vcpath->ptr_value)->type ==
            H64GCVALUETYPE_STRING) {
        pathstr = (char *)((h64gcvalue *)vcpath->ptr_value)->str_val.s;
        pathlen = ((h64gcvalue *)vcpath->ptr_value)->str_val.len;
    } else if (vcpath->type == H64VALTYPE_SHORTSTR) {
        pathstr = (char *)vcpath->shortstr_value;
        pathlen = vcpath->shortstr_len;
    } else {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "URI must be a string"
        );
    }

    static h64wchar protodefaultbuf[] = {'h', 't', 't', 'p', 's'};
    valuecontent *vcproto = STACK_ENTRY(vmthread->stack, 1);
    char *protostr = NULL;
    int64_t protolen = 0;
    if (vcproto->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue *)vcproto->ptr_value)->type == H64GCVALUETYPE_STRING) {
        protostr = (char *)((h64gcvalue *)vcproto->ptr_value)->str_val.s;
        protolen = ((h64gcvalue *)vcproto->ptr_value)->str_val.len;
    } else if (vcproto->type == H64VALTYPE_SHORTSTR) {
        protostr = (char *)vcproto->shortstr_value;
        protolen = vcproto->shortstr_len;
    } else if (vcproto->type == H64VALTYPE_UNSPECIFIED_KWARG) {
        protostr = (char *)protodefaultbuf;
        protolen = sizeof(protodefaultbuf) / sizeof(protodefaultbuf[0]);
    } else {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "URI must be a string"
        );
    }

    valuecontent *vcguessport = STACK_ENTRY(vmthread->stack, 2);
    int guessport = 0;
    if (vcguessport->type == H64VALTYPE_UNSPECIFIED_KWARG) {
        guessport = 1;
    } else if (vcguessport->type == H64VALTYPE_BOOL) {
        guessport = (vcguessport->int_value != 0);
    } else {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "guess_port must be a boolean"
        );
    }

    uri32info *uinfo = uri32_ParseEx(
        (h64wchar *)pathstr, pathlen,
        (h64wchar *)protostr, protolen,
        0 | (guessport ? URI32_PARSEEX_FLAG_GUESSPORT : 0)
    );
    if (!uinfo) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OUTOFMEMORYERROR,
            "out of memory parsing URI"
        );
    }

    h64gcvalue *uriobj = NULL;
    uriobj = poolalloc_malloc(
        vmthread->heap, 0
    );
    if (!uriobj)
        goto oom;
    memset(uriobj, 0, sizeof(*uriobj));
    uriobj->type = H64GCVALUETYPE_OBJINSTANCE;
    uriobj->heapreferencecount = 0;
    uriobj->externalreferencecount = 1;
    uriobj->class_id = (
        vmthread->vmexec_owner->program->_urilib_uri_class_idx
    );
    uriobj->cdata = malloc(sizeof(_uriobj_cdata));
    if (!uriobj->cdata)
        goto oom;
    memset(uriobj->cdata, 0, sizeof(_uriobj_cdata));
    uriobj->varattr = malloc(
        sizeof(*uriobj->varattr) * H64URIMEMBER_TOTAL
    );
    if (!uriobj->varattr) {
        goto oom;
    }
    memset(
        uriobj->varattr, 0,
        sizeof(*uriobj->varattr) * H64URIMEMBER_TOTAL
    );
    if (uinfo->path &&
            !valuecontent_SetStringU32(
            vmthread, &uriobj->varattr[H64URIMEMBER_PATH],
            uinfo->path, uinfo->pathlen
            )) {
        goto oom;
    }
    ADDREF_HEAP(&uriobj->varattr[H64URIMEMBER_PATH]);
    if (uinfo->host &&
            !valuecontent_SetStringU32(
            vmthread, &uriobj->varattr[H64URIMEMBER_HOST],
            uinfo->host, uinfo->hostlen
            )) {
        goto oom;
    }
    ADDREF_HEAP(&uriobj->varattr[H64URIMEMBER_HOST]);
    if (uinfo->protocol &&
            !valuecontent_SetStringU32(
            vmthread, &uriobj->varattr[H64URIMEMBER_PROTOCOL],
            uinfo->protocol, uinfo->protocollen
            )) {
        oom: ;
        if (uriobj) {
            if (uriobj->varattr) {
                int i = 0;
                while (i < H64URIMEMBER_TOTAL) {
                    DELREF_HEAP(&uriobj->varattr[i]);
                    valuecontent_Free(&uriobj->varattr[i]);
                    i++;
                }
                free(uriobj->varattr);
            }
            free(uriobj->cdata);
        }
        poolalloc_free(vmthread->heap, uriobj);
        uri32_Free(uinfo);
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OUTOFMEMORYERROR,
            "out of memory allocating URI object"
        );
    }
    ADDREF_HEAP(&uriobj->varattr[H64URIMEMBER_PROTOCOL]);
    if (uinfo->port >= 0) {
        uriobj->varattr[H64URIMEMBER_PORT].type = (
            H64VALTYPE_INT64
        );
        uriobj->varattr[H64URIMEMBER_PORT].int_value = (
            uinfo->port
        );
    }
    ((_uriobj_cdata *)uriobj->cdata)->uinfo = uinfo;

    valuecontent *vresult = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vresult);
    memset(vresult, 0, sizeof(*vresult));
    vresult->type = H64VALTYPE_GCVAL;
    vresult->ptr_value = uriobj;

    return 1;
}

int urilib_RegisterFuncsAndModules(h64program *p) {
    // uri.parse() method:
    const char *uri_parse_kw_arg_name[] = {
        NULL, "default_protocol", "guess_port"
    };
    int64_t idx = h64program_RegisterCFunction(
        p, "parse", &urilib_parse,
        NULL, 3, uri_parse_kw_arg_name,  // fileuri, args, kw arg names
        "uri", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    // uri class:
    p->_urilib_uri_class_idx = h64program_AddClass(
        p, "uri", NULL, "uri", "core.horse64.org"
    );
    if (p->_urilib_uri_class_idx < 0)
        return 0;
    {
        int i = 0;
        while (i < H64URIMEMBER_TOTAL) {
            attridx_t aidx = h64program_RegisterClassVariable(
                p, p->_urilib_uri_class_idx, urimembernames[i],
                NULL
            );
            assert(p->classes != NULL);
            assert(
                p->classes[p->_urilib_uri_class_idx].varattr_flags != NULL
            );
            p->classes[p->_urilib_uri_class_idx].varattr_flags[
                aidx
            ] |= VARATTR_FLAGS_CONST;
            if (aidx < 0) {
                return 0;
            }
            i++;
        }
    }

    return 1;
}
