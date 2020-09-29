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
                "alloc failure converting file path"
            );
        }
    }
    int64_t outlen = 0;
    int result = utf32_to_utf8(
        (unicodechar*)pathstr, pathlen,
        utf8buf, utf8bufsize,
        &outlen, 1
    );
    if (!result) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_RUNTIMEERROR,
            "internal unexpected error in unicode "
            "conversion"
        );
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
    assert(gcvalue->classid ==
           vmthread->vmexec_owner->program->_io_file_class_idx);

    return 1;
}

int iolib_fileclose(
        h64vmthread *vmthread
        ) {
    /**
     * Read from the given file.
     *
     * @funcattr io.file close
     */
    assert(STACK_TOP(vmthread->stack) >= 1);

    valuecontent *vc = STACK_ENTRY(vmthread->stack, 0);
    assert(vc->type == H64VALTYPE_GCVAL);
    h64gcvalue *gcvalue = (h64gcvalue *)vc->ptr_value;
    assert(gcvalue->type == H64GCVALUETYPE_OBJINSTANCE);
    assert(gcvalue->classid ==
           vmthread->vmexec_owner->program->_io_file_class_idx);

    return 1;
}

int iolib_RegisterFuncs(h64program *p) {
    // io.open:
    const char *io_open_kw_arg_name[] = {
        NULL, "read", "write", "append"
    };
    int64_t idx;
    idx = h64program_RegisterCFunction(
        p, "open", &iolib_open,
        NULL, 4, io_open_kw_arg_name, 0,  // fileuri, args
        "io", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    // file class:
    p->_io_file_class_idx = h64program_AddClass(
        p, "file", NULL, "io", "core.horse64.org"
    );
    if (p->_io_file_class_idx < 0)
        return 0;

    // file.read method:
    const char *io_fileread_kw_arg_name[] = {"amount"};
    idx = h64program_RegisterCFunction(
        p, "read", &iolib_fileread,
        NULL, 1, io_fileread_kw_arg_name, 0,  // fileuri, args
        "io", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    // file.close method:
    idx = h64program_RegisterCFunction(
        p, "close", &iolib_fileclose,
        NULL, 0, NULL, 0,  // fileuri, args
        "io", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    return 1;
}
