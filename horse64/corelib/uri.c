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
#include "stack.h"
#include "vmexec.h"
#include "widechar.h"


/// @module uri Parse and process URIs of any kind.

typedef struct _uriobj_cdata {

} __attribute__((packed)) _uriobj_cdata;


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
     * @raises ValueError when the given URI could not be parsed
     * @returns a @see{URI object|uri.uri}
     */
    assert(STACK_TOP(vmthread->stack) >= 1);

    assert(0);  // FIXME: implement this
}

int urilib_RegisterFuncsAndModules(h64program *p) {
    // uri.parse() method:
    const char *uri_parse_kw_arg_name[] = {"amount"};
    int64_t idx = h64program_RegisterCFunction(
        p, "read", &urilib_parse,
        NULL, 1, uri_parse_kw_arg_name,  // fileuri, args
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

    return 1;
}
