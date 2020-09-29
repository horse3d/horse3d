// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#if defined(_WIN32) || defined(_WIN64)
#include <malloc.h>
#else
#include <alloca.h>
#include <errno.h>
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
#include "poolalloc.h"
#include "stack.h"
#include "unicode.h"
#include "vmexec.h"

#define FILEOBJ_FLAGS_APPEND 0x1
#define FILEOBJ_FLAGS_LASTWASWRITE 0x2

typedef struct _fileobj_cdata {
    FILE *file_handle;
    uint8_t flags;
} __attribute__((packed)) _fileobj_cdata;

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
    assert(STACK_TOP(vmthread->stack) >= 4);

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

    uint8_t flags = 0;
    int mode_read = 1;
    int mode_write = 0;
    int mode_append = 2;
    {
        valuecontent *vcarg = STACK_ENTRY(vmthread->stack, 1);
        if (vcarg->type != H64VALTYPE_BOOLEAN) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_TYPEERROR,
                "read option must be a boolean"
            );
        }
        mode_read = (vcarg->int_value != 0);
    }
    {
        valuecontent *vcarg = STACK_ENTRY(vmthread->stack, 2);
        if (vcarg->type != H64VALTYPE_BOOLEAN) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_TYPEERROR,
                "read option must be a boolean"
            );
        }
        mode_write = (vcarg->int_value != 0);
    }
    {
        valuecontent *vcarg = STACK_ENTRY(vmthread->stack, 3);
        if (vcarg->type != H64VALTYPE_BOOLEAN && (
                vcarg->type != H64VALTYPE_INT64 ||
                vcarg->int_val != 2)) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_TYPEERROR,
                "append option must be a boolean or io.APPEND_DEFAULT"
            );
        }
        if (vcarg->type == H64VALTYPE_BOOLEAN)
            mode_append = (vcarg->int_value != 0);
        else
            mode_append = 2;
    }
    if (!mode_read && !mode_write) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_ARGUMENTERROR,
            "must open for either reading or writing"
        );
    } else if (!mode_write && mode_append) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_ARGUMENTERROR,
            "cannot append without writing"
        );
    }
    char modestr[5] = "rb";
    if (mode_write && mode_read) {
        if (mode_append)
            memcpy(modestr, "a+b", strlen("a+b"));
        else
            memcpy(modestr, "r+b", strlen("r+b"));
        if (mode_append)
            flags |= FILEOBJ_FLAGS_APPEND;
    } else if (mode_write) {
        if (mode_append)
            memcpy(modestr, "ab", strlen("ab"));
        else
            memcpy(modestr, "wb", strlen("wb"));
        if (mode_append)
            flags |= FILEOBJ_FLAGS_APPEND;
    }

    #if defined(_WIN32) || defined(_WIN64)
    FILE *f = NULL;
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
    if (!result || outlen >= utf8bufsize) {
        if (freeutf8buf)
            free(utf8buf);
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_RUNTIMEERROR,
            "internal unexpected error in unicode "
            "conversion"
        );
    }
    utf8buf[outlen] = '\0';
    int i = 0;
    while (i < utf8bufsize) {
        if (utf8buf[i] == '\0') {
            if (freeutf8buf)
                free(utf8buf);
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "no such file or directory"
            );
        }
        i++;
    }
    errno = 0;
    FILE *f = fopen(
        utf8buf, modestr
    );
    if (!f) {
        if (freeutf8buf)
            free(utf8buf);
        if (errno == EACCES)
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_PERMISSIONERROR,
                "permission denied"
            );
        else if (errno == EDQUOT || errno == ENOSPC)
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "no disk space left"
            );
        else if (errno == ENOENT || errno == ENOTDIR)
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "no such file or directory"
            );
        else if (errno == ENFILE)
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_OSERROR,
                "no free file handles left"
            );
        else if (errno == ENAMETOOLONG)
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "given path name too long"
            );
        else if (errno == ENOMEM)
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_OUTOFMEMORYERROR,
                "out of memory opening file"
            );
        else if (errno == ENXIO)
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "special file that doesn't support i/o"
            );
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OSERROR,
            "unhandled general resource error"
        );
    }
    if (freeutf8buf)
        free(utf8buf);
    #endif
    h64gcvalue *fileobj = poolalloc_malloc(
        vmthread->heap, 0
    );
    if (!fileobj) {
        fclose(f);
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OUTOFMEMORYERROR,
            "out of memory allocating file object"
        );
    }
    fileobj->type = H64GCVALUETYPE_OBJINSTANCE;
    fileobj->heapreferencecount = 0;
    fileobj->externalreferencecount = 1;
    fileobj->classid = vmthread->vmexec_owner->program->_io_file_class_idx;
    fileobj->cdata = malloc(sizeof(_fileobj_cdata));
    if (!fileobj->cdata) {
        fclose(f);
        poolalloc_free(vmthread->heap, fileobj);
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OUTOFMEMORYERROR,
            "out of memory allocating file object"
        );
    }
    ((_fileobj_cdata*)fileobj->cdata)->file_handle = f;
    ((_fileobj_cdata*)fileobj->cdata)->flags = flags;
    assert(STACK_TOP(vmthread->stack) >= 1);
    valuecontent *vc = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vc);
    valuecontent_Free(vc);
    memset(vc, 0, sizeof(*vc));
    vc->type = H64VALTYPE_GCVAL;
    vc->ptr_value = fileobj;
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
