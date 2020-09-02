// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#if defined(_WIN32) || defined(_WIN64)
#include <malloc.h>
#else
#include <alloca.h>
#endif
#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "bytecode.h"
#include "corelib/errors.h"
#include "corelib/io.h"
#include "gcvalue.h"
#include "stack.h"
#include "unicode.h"
#include "vmexec.h"


int iolib_open(
        h64vmthread *vmthread
        ) {
    /**
     *
     * @func io.open
     * @param path the path of the file to be opened
     * @param read=true
     * @param write=false
     * @param append=io.APPEND_DEFAULT
     */
    assert(STACK_TOP(vmthread->stack) >= 3);

    valuecontent *vcpath = STACK_ENTRY(vmthread->stack, 0);
    char *pathstr = NULL;
    int64_t pathlen = 0;
    int pathu32 = 0;
    if (vcpath->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue *)vcpath)->type == H64GCVALUETYPE_STRING) {
        pathstr = (char *)((h64gcvalue *)vcpath)->str_val.s;
        pathlen = ((h64gcvalue *)vcpath)->str_val.len;
        pathu32 = 1;
    } else if (vcpath->type == H64VALTYPE_SHORTSTR) {
        pathstr = (char *)vcpath->shortstr_value;
        pathlen = vcpath->shortstr_len;
        pathu32 = 1;
    } else {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "path must be a string or bytes"
        );
    }

    #if defined(_WIN32) || defined(_WIN64)

    #else
    char _utf8bufstack[1024];
    char *utf8buf = _utf8bufstack;
    int64_t utf8bufsize = 1024;
    int freeutf8buf = 0;
    int64_t wantbufsize = pathlen * 5 + 1;
    if (wantbufsize > utf8bufsize) {
        utf8buf = malloc(wantbufsize);
        if (!utf8buf) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_OUTOFMEMORYERROR,
                "alloc failure printing list"
            );
        }
    }
    #endif

    return 1;
}


int iolib_fileread(
        h64vmthread *vmthread
        ) {
    /**
     * Read from the given file.
     *
     * @funcattr io.file read
     * @param amount=-1
     */
    assert(STACK_TOP(vmthread->stack) >= 2);

    valuecontent *vc = STACK_ENTRY(vmthread->stack, 0);
    assert(vc->type == H64VALTYPE_GCVAL);
    h64gcvalue *gcvalue = (h64gcvalue *)vc->ptr_value;
    assert(gcvalue->type == H64GCVALUETYPE_OBJINSTANCE);

    return 1;
}

int iolib_RegisterFuncs(h64program *p) {
    /*int64_t idx;
    idx = h64program_RegisterCFunction(
        p, "print", &corelib_print,
        NULL, 1, NULL, 1, NULL, NULL, 1, -1
    );
    if (idx < 0)
        return 0;
    p->print_func_index = idx;
    idx = h64program_RegisterCFunction(
        p, "$$containeradd", &corelib_containeradd,
        NULL, 1, NULL, 0, NULL, NULL, 1, -1
    );
    if (idx < 0)
        return 0;
    p->func[idx].input_stack_size++;  // for 'self'
    p->containeradd_func_index = idx;*/
    return 1;
}
