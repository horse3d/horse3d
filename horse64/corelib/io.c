// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "valuecontentstruct.h"
#define _FILE_OFFSET_BITS 64
#ifndef __USE_LARGEFILE64
#define __USE_LARGEFILE64 1
#endif
#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif
#define _LARGEFILE_SOURCE
#if defined(_WIN32) || defined(_WIN64)
#define fseek64 _fseeki64
#define ftell64 _ftelli64
#else
#define fseek64 fseeko64
#define ftell64 ftello64
#endif

#include "compileconfig.h"

#if defined(_WIN32) || defined(_WIN64)
int _close(
   int fd
);
#define _O_RDONLY 0x0000
#include <malloc.h>
#include <windows.h>
int _open_osfhandle(intptr_t osfhandle, int flags);
#else
#include <alloca.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif
#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "bytecode.h"
#include "corelib/errors.h"
#include "corelib/io.h"
#include "gcvalue.h"
#include "poolalloc.h"
#include "stack.h"
#include "vmexec.h"
#include "vmlist.h"
#include "vmstrings.h"
#include "widechar.h"

#define FILEOBJ_FLAGS_APPEND 0x1
#define FILEOBJ_FLAGS_WRITE 0x2
#define FILEOBJ_FLAGS_READ 0x4
#define FILEOBJ_FLAGS_LASTWASWRITE 0x8
#define FILEOBJ_FLAGS_BINARY 0x10
#define FILEOBJ_FLAGS_CACHEDUNSENTERROR 0x20
#define FILEOBJ_FLAGS_IGNOREENCODINGERROR 0x40

/// @module io Disk access functions, reading and writing to files.

typedef struct _fileobj_cdata {
    void (*on_destroy)(h64gcvalue *fileobj);
    FILE *file_handle;
    uint8_t flags;
    uint64_t text_offset;
} __attribute__((packed)) _fileobj_cdata;


/**
 * A file object class, returned from @see{io.open}.
 *
 * @class file
 */

int iolib_open(
        h64vmthread *vmthread
        ) {
    /**
     * Open a file for reading or writing.
     *
     * @func open
     * @param path the path of the file to be opened
     * @param read=(yes or no) whether the file should be opened for
              @see(reading|io.file.read). Will default to yes if
              all other options like write and append are unset or no.
     * @param write=no whether the file should be opened for @see(
              writing|io.file.write), defaults to no.
     * @param append=(yes or no) whether if opening writing, the
              writing should always be appended to the end.
              Usually, this defaults to no.
              When a file is explicitly opened with BOTH write=yes and
              read=yes, then however this defaults to yes (such that
              the file isn't also wiped).
              When a file isn't opened for writing, this is ignored.
     * @param binary=no whether the file should be read as unchanged
              binary which will return @see(bytes) when reading, or
              otherwise it will be decoded from utf-8 and return a
              @see(string). When decoding to a string, invalid bytes
              will be replaced by code point U+FFFD which is lossy.
     * @param allow_bad_encoding=no when reading in non-binary mode,
              this setting determines whether to throw an EncodingError
              when reading invalid encoding (when this setting is no,
              the default), or whether garbage will be silently surrogate
              escaped and retained (when this setting is set to yes).
     * @returns a @see{file object|io.file}
     * @raises IOError raised when there is a failure that is NOT expected
     *    to go away with retrying, like wrong file type, full disk, path
     *    doesn't point to any existing file, etc.
     * @raises PermissionError raised when there are insufficient permissions
     *    for opening the target, for example when opening a read-only file
     *    for writing, or when opening a file belonging to another user
     *    without permissions for shared access.
     * @raises ResourceError raised when there is unexpected resource
     *    exhaustion that MAY go away when retrying, like running out of
     *    file handles, read timeout, and so on.
     */
    assert(STACK_TOP(vmthread->stack) >= 6);

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
            "path must be a string or bytes"
        );
    }

    valuecontent *vcargread = STACK_ENTRY(vmthread->stack, 1);
    valuecontent *vcargwrite = STACK_ENTRY(vmthread->stack, 2);
    valuecontent *vcargappend = STACK_ENTRY(vmthread->stack, 3);

    uint8_t flags = 0;
    int mode_read = 1;
    int mode_write = 0;
    int mode_append = 2;
    {
        if (vcargread->type != H64VALTYPE_BOOL &&
                vcargread->type != H64VALTYPE_UNSPECIFIED_KWARG) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_TYPEERROR,
                "read option must be a boolean"
            );
        }
        if (vcargread->type == H64VALTYPE_UNSPECIFIED_KWARG) {
            if (vcargwrite->type == H64VALTYPE_UNSPECIFIED_KWARG ||
                    (vcargwrite->type == H64VALTYPE_BOOL &&
                     vcargwrite->int_value == 0)) {
                mode_read = 1;
            } else {
                mode_read = 0;
            }
        } else {
            mode_read = (vcargread->int_value != 0);
        }
    }
    {
        if (vcargwrite->type != H64VALTYPE_BOOL &&
                vcargwrite->type != H64VALTYPE_UNSPECIFIED_KWARG) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_TYPEERROR,
                "write option must be a boolean"
            );
        }
        if (vcargwrite->type == H64VALTYPE_UNSPECIFIED_KWARG)
            mode_write = 0;
        else
            mode_write = (vcargwrite->int_value != 0);
    }
    {
        if (vcargappend->type != H64VALTYPE_BOOL &&
                vcargappend->type != H64VALTYPE_UNSPECIFIED_KWARG) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_TYPEERROR,
                "append option must be a boolean"
            );
        }
        if (vcargappend->type == H64VALTYPE_BOOL)
            mode_append = (vcargappend->int_value != 0);
        else
            mode_append = (mode_write && mode_read);
    }
    {
        valuecontent *vcarg = STACK_ENTRY(vmthread->stack, 4);
        if (vcarg->type != H64VALTYPE_BOOL &&
                vcarg->type != H64VALTYPE_UNSPECIFIED_KWARG) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_TYPEERROR,
                "binary option must be a boolean"
            );
        }
        if (vcarg->type != H64VALTYPE_UNSPECIFIED_KWARG &&
                vcarg->int_value != 0)
            flags |= FILEOBJ_FLAGS_BINARY;
    }
    {
        valuecontent *vcarg = STACK_ENTRY(vmthread->stack, 5);
        if (vcarg->type != H64VALTYPE_BOOL &&
                vcarg->type != H64VALTYPE_UNSPECIFIED_KWARG) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_TYPEERROR,
                "allow_bad_encoding option must be a boolean"
            );
        }
        if (vcarg->type != H64VALTYPE_UNSPECIFIED_KWARG &&
                vcarg->int_value != 0)
            flags |= FILEOBJ_FLAGS_IGNOREENCODINGERROR;
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
    if (mode_append == 2) {
        if (mode_write && mode_read)
            mode_append = 1;
        mode_append = 0;
    }
    char modestr[5] = "rb";
    if (mode_write && mode_read) {
        flags |= FILEOBJ_FLAGS_WRITE;
        flags |= FILEOBJ_FLAGS_READ;
        // Use "r+b" even for append, since we don't want to
        // create a file if it doesn't exist. Instead, if we
        // manage to open it we will change modes later:
        memcpy(modestr, "r+b", strlen("r+b"));
        if (mode_append)
            flags |= FILEOBJ_FLAGS_APPEND;
    } else if (mode_write) {
        flags |= FILEOBJ_FLAGS_WRITE;
        if (mode_append)
            memcpy(modestr, "rb", strlen("ab"));  // See note on r+b above.
        else
            memcpy(modestr, "wb", strlen("wb"));
        if (mode_append)
            flags |= FILEOBJ_FLAGS_APPEND;
    } else {
        flags |= FILEOBJ_FLAGS_READ;
    }

    #if defined(_WIN32) || defined(_WIN64)
    uint16_t *wpath = malloc(
        sizeof(uint16_t) * (pathlen * 2 + 3)
    );
    if (!wpath) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OUTOFMEMORYERROR,
            "alloc failure converting file path"
        );
    }
    int64_t out_len = 0;
    int result = utf32_to_utf16(
        (h64wchar*) pathstr, pathlen, (char*)wpath,
        sizeof(uint16_t) * (pathlen * 2 + 3),
        &out_len, 1
    );
    if (!result || out_len >= (pathlen * 2 + 3)) {
        free(wpath);
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_RUNTIMEERROR,
            "internal unexpected error in widechar "
            "conversion"
        );
    }
    int i = 0;
    while (i < out_len) {  // make sure nullbytes -> no such file
        if (wpath[i] == '\0') {
            free(wpath);
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "no such file or directory"
            );
        }
        i++;
    }
    wpath[out_len] = '\0';
    HANDLE f2 = CreateFileW(
        (LPCWSTR)wpath,
        0 | (mode_read ? GENERIC_READ : 0)
        | (mode_write ? GENERIC_WRITE : 0)
        | (mode_write ? FILE_APPEND_DATA : 0),
        (mode_write ? 0 : FILE_SHARE_READ),
        NULL,
        OPEN_EXISTING | (
            (mode_write && !mode_append) ? CREATE_NEW : 0
        ),
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    free(wpath); wpath = NULL;
    if (f2 == INVALID_HANDLE_VALUE) {
        uint32_t err = (int)GetLastError();
        if (err == ERROR_FILE_NOT_FOUND) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "no such file or directory"
            );
        } else if (err == ERROR_TOO_MANY_OPEN_FILES) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_RESOURCEERROR,
                "too many open files"
            );
        } else if (err == ERROR_ACCESS_DENIED) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_PERMISSIONERROR,
                "permission denied"
            );
        } else if (err == ERROR_SHARING_VIOLATION) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_RESOURCEERROR,
                "file in use by other process"
            );
        } else if (err == ERROR_INVALID_NAME) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "given path name is invalid"
            );
        } else if (err == ERROR_LABEL_TOO_LONG ||
                err == ERROR_FILENAME_EXCED_RANGE
                ) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "given path name too long"
            );
        } else if (err == ERROR_BUFFER_OVERFLOW) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_OUTOFMEMORYERROR,
                "out of memory opening file"
            );
        }
        char buf[256];
        snprintf(buf, sizeof(buf) - 1,
            "unhandled type of operation error: %d",
            (int)err
        );
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_RESOURCEERROR,
            buf
        );
    }
    int filedescr = _open_osfhandle(
        (intptr_t)f2, _O_RDONLY
    );
    if (filedescr < 0) {
        CloseHandle(f2);
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_RESOURCEERROR,
            "conversion to FILE* unexpectly failed"
        );
    }
    f2 = NULL;  // now owned by 'filedescr'
    FILE *f = _fdopen(filedescr, "rb");
    if (!f) {
        _close(filedescr);
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_RESOURCEERROR,
            "conversion to FILE* unexpectly failed"
        );
    }
    filedescr = -1;  // now owned by 'f'/FILE* ptr

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
        (h64wchar*)pathstr, pathlen,
        utf8buf, utf8bufsize,
        &outlen, 1, 1
    );
    if (!result || outlen >= utf8bufsize) {
        if (freeutf8buf)
            free(utf8buf);
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_RUNTIMEERROR,
            "internal unexpected error in widechar "
            "conversion"
        );
    }
    utf8buf[outlen] = '\0';
    int i = 0;
    while (i < outlen) {   // make sure nullbytes -> no such file
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
    if (outlen == 0) {
        assert(utf8bufsize >= 2);
        utf8buf[0] = '.';
        utf8buf[1] = '\0';
    }
    errno = 0;
    FILE *f3 = fopen64(
        utf8buf, modestr
    );
    if (!f3) {
        if (freeutf8buf)
            free(utf8buf);
        handlelinuxfileerror: ;
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
        else if (errno == EISDIR)
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "given path is a directory not a file"
            );
        else if (errno == ENOENT)
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "no such file or directory"
            );
        else if (errno == ENFILE)
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_RESOURCEERROR,
                "too many open files"
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
        else if (errno == EINVAL)
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_OUTOFMEMORYERROR,
                "given path name is invalid"
            );
        else if (errno == ENXIO)
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "special file that doesn't support i/o"
            );
        char buf[256];
        snprintf(buf, sizeof(buf) - 1,
            "unhandled type of operation error: %d",
            (int)errno
        );
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_RESOURCEERROR,
            buf
        );
    }
    if (freeutf8buf)
        free(utf8buf);
    FILE *f = NULL;
    if (mode_append) {
        if (mode_read)
            memcpy(modestr, "a+b", strlen("a+b"));
        else
            memcpy(modestr, "ab", strlen("ab"));
        f = freopen64(NULL, modestr, f3);
        if (!f) {
            goto handlelinuxfileerror;
        }
    } else {
        f = f3;
    }
    // Make sure we didn't open a directory:
    errno = 0;
    struct stat filestatinfo;
    if (fstat(fileno(f), &filestatinfo) != 0) {
        fclose(f);
        if (errno == EACCES)
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_PERMISSIONERROR,
                "permission denied"
            );
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_RESOURCEERROR,
            "fstat() failed on file for unknown reason"
        );
    }
    if (S_ISDIR(filestatinfo.st_mode)) {
        fclose(f);
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_RESOURCEERROR,
            "path refers to a directory, not a file"
        );
    }
    #endif  // end of unix/windows split

    // Return as a new file object:
    h64gcvalue *fileobj = poolalloc_malloc(
        vmthread->heap, 0
    );
    if (!fileobj) {
        #if defined(_WIN32) || defined(_WIN64)
        CloseHandle(f);
        #else
        fclose(f);
        #endif
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OUTOFMEMORYERROR,
            "out of memory allocating file object"
        );
    }
    fileobj->hash = 0;
    fileobj->type = H64GCVALUETYPE_OBJINSTANCE;
    fileobj->heapreferencecount = 0;
    fileobj->externalreferencecount = 1;
    fileobj->class_id = vmthread->vmexec_owner->program->_io_file_class_idx;
    fileobj->cdata = malloc(sizeof(_fileobj_cdata));
    if (!fileobj->cdata) {
        #if defined(_WIN32) || defined(_WIN64)
        CloseHandle(f);
        #else
        fclose(f);
        #endif
        poolalloc_free(vmthread->heap, fileobj);
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OUTOFMEMORYERROR,
            "out of memory allocating file object"
        );
    }
    memset(fileobj->cdata, 0, sizeof(_fileobj_cdata));
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

int iolib_filewrite(
        h64vmthread *vmthread
        ) {
    /**
     * Write to the given file.
     *
     * @funcattr file write
     * @param data the data to write, which must be @see{bytes} if
     *    the file was opened with binary=yes, and otherwise must
     *    be @see{string}.
     * @raises IOError raised when there is a failure that is NOT expected
     *    to go away with retrying, like writing to a file only opened
     *    for reading.
     * @raises ResourceError raised when there is unexpected resource
     *    exhaustion that MAY go away when retrying, like running out of
     *    file handles, read timeout, and so on.
     */
    assert(STACK_TOP(vmthread->stack) >= 1);

    valuecontent *vc = STACK_ENTRY(vmthread->stack, 1);  // first closure arg
    assert(vc->type == H64VALTYPE_GCVAL);
    h64gcvalue *gcvalue = (h64gcvalue *)vc->ptr_value;
    assert(gcvalue->type == H64GCVALUETYPE_OBJINSTANCE);
    assert(gcvalue->class_id ==
           vmthread->vmexec_owner->program->_io_file_class_idx);
    _fileobj_cdata *cdata = (gcvalue->cdata);
    FILE *f = cdata->file_handle;

    if (!f) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_IOERROR,
            "file was closed"
        );
    }
    if ((cdata->flags & FILEOBJ_FLAGS_CACHEDUNSENTERROR) != 0) {
        cdata->flags &= ~((uint8_t)FILEOBJ_FLAGS_CACHEDUNSENTERROR);
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_RESOURCEERROR,
            "unknown I/O error"
        );
    }

    if ((cdata->flags & FILEOBJ_FLAGS_WRITE) == 0) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_IOERROR,
            "not opened for writing"
        );
    }

    valuecontent *vcwriteobj = STACK_ENTRY(vmthread->stack, 0);
    h64wchar *writestr = NULL;
    int64_t writestrlen = 0;
    int64_t writestrletters = 0;
    char *writebytes = NULL;
    int64_t writebyteslen = 0;
    if (vcwriteobj->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue *)vcwriteobj->ptr_value)->type ==
                H64GCVALUETYPE_STRING) {
        writestr = (
            ((h64gcvalue *)vcwriteobj->ptr_value)->str_val.s
        );
        writestrlen = (
            ((h64gcvalue *)vcwriteobj->ptr_value)->str_val.len
        );
        vmstrings_RequireLetterLen(
            &(((h64gcvalue *)vcwriteobj->ptr_value)->str_val)
        );
        writestrletters = (
            ((h64gcvalue *)vcwriteobj->ptr_value)->str_val.letterlen
        );
    } else if (vcwriteobj->type == H64VALTYPE_SHORTSTR) {
        writestr = vcwriteobj->shortstr_value;
        writestrlen = vcwriteobj->shortstr_len;
        writestrletters = (
            utf32_letters_count(writestr, writestrlen)
        );
    } else if (vcwriteobj->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue *)vcwriteobj->ptr_value)->type ==
                H64GCVALUETYPE_BYTES) {
        writebytes = (
            ((h64gcvalue *)vcwriteobj->ptr_value)->bytes_val.s
        );
        writebyteslen = (
            ((h64gcvalue *)vcwriteobj->ptr_value)->bytes_val.len
        );
    } else if (vcwriteobj->type == H64VALTYPE_SHORTBYTES) {
        writebytes = vcwriteobj->shortbytes_value;
        writebyteslen = vcwriteobj->shortbytes_len;
    }
    int readbinary = ((cdata->flags & FILEOBJ_FLAGS_BINARY) != 0);
    if (writestr == NULL && !readbinary) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "data argument must be string for non-binary file"
        );
    }
    if (writebytes == NULL && readbinary) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "data argument must be bytes for binary file"
        );
    }

    if ((cdata->flags & FILEOBJ_FLAGS_LASTWASWRITE) == 0) {
        fflush(f);
        fseek64(f, 0, SEEK_CUR);
        cdata->flags |= ((uint8_t)FILEOBJ_FLAGS_LASTWASWRITE);
    }

    // If we're writing a string, convert to bytes first:
    int64_t writelenresult = writebyteslen;
    int freebytes = 0;
    if (writestr) {
        assert(!writebytes);
        writebytes = malloc(writestrlen * 5 + 1);
        if (!writebytes) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_OUTOFMEMORYERROR,
                "out of memory on write data conversion"
            );
        }
        freebytes = 1;
        int result = utf32_to_utf8(
            writestr, writestrlen,
            writebytes, writestrlen * 5 + 1,
            &writebyteslen, 1, 1
        );
        if (!result || writebyteslen >= writestrlen * 5 + 1) {
            if (freebytes)
                free(writebytes);
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_RUNTIMEERROR,
                "internal error: unicode conversion "
                "unexpectedly failed"
            );
        }
        writebytes[writebyteslen] = '\0';
        writelenresult = writestrletters;
    }

    // If nothing to write, bail out early:
    if (writebyteslen == 0) {
        valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
        DELREF_NONHEAP(vcresult);
        valuecontent_Free(vcresult);
        vcresult->type = H64VALTYPE_INT64;
        vcresult->int_value = 0;
        return 1;
    }

    // Write out data:
    size_t written = fwrite(
        writebytes, 1, writebyteslen, f
    );
    if (written < (size_t)writebyteslen) {
        if (written == 0) {
            writelenresult = 0;
        } else if (written > 0 && writestr) {
            // Find out the amount of letters we wrote:
            writelenresult = 0;
            int64_t istr = 0;
            int64_t ibytes = 0;
            while (ibytes < (int64_t)written) {
                int next_char_len = utf32_letter_len(
                    writestr + istr, writestrlen - istr
                );
                assert(next_char_len > 0);
                int entireletterlen = 0;
                int k = 0;
                while (k < next_char_len) {
                    int utf8len = utf8_char_len(
                        (const uint8_t *)writebytes + ibytes
                    );
                    if (utf8len < 1)
                        utf8len = 1;
                    entireletterlen += utf8len;
                    ibytes += utf8len;
                    istr += 1;
                    k++;
                }
                if (ibytes > (int64_t)written)
                    break;
                writelenresult++;
            }
            assert(writelenresult <= writestrletters);
        } else if (written > 0) {
            writelenresult = written;
        }
        if (writelenresult > 0) {
            // Return the written amount first, delay
            // reporting the error until later:
            cdata->flags |= (FILEOBJ_FLAGS_CACHEDUNSENTERROR);
            valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
            DELREF_NONHEAP(vcresult);
            valuecontent_Free(vcresult);
            vcresult->type = H64VALTYPE_INT64;
            vcresult->int_value = writelenresult;
            return 1;
        }
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_RESOURCEERROR,
            "unknown I/O error"
        );
    } else {
        valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
        DELREF_NONHEAP(vcresult);
        valuecontent_Free(vcresult);
        vcresult->type = H64VALTYPE_INT64;
        vcresult->int_value = writelenresult;
        return 1;
    }
}

void _count_actual_letters_in_raw(
        const uint8_t *raw, int64_t rawlen,
        h64wchar *decodebuf,
        int64_t consider_all_valid_upto,
        int64_t *codepoints, int64_t *letters,
        int *last_codepoint_was_incomplete,
        int *endsinmultipleinvalid
        ) {
    *last_codepoint_was_incomplete = 0;
    *endsinmultipleinvalid = 0;
    int64_t cpcount = 0;
    int64_t lettercount = 0;
    int64_t i = 0;
    int previouswasinvalid = 0;
    while (i < rawlen) {
        int charlen = utf8_char_len(
            (const uint8_t *)(raw + i)
        );
        h64wchar cp = 0;
        if (charlen <= 0)
            charlen = 1;
        if (charlen + i > rawlen) {
            charlen = 1;
            *last_codepoint_was_incomplete = 1;
        }
        uint8_t *p = (uint8_t *)(raw + i);
        if (charlen <= 1 && *p > 127) {
            // Invalid byte, surrogate escape:
            cp = (0xDC80ULL + (
                (h64wchar)((uint8_t)*p)
            ));
            decodebuf[cpcount] = cp;
            cpcount++;
            i += 1;
            while (i < rawlen) {  // Finish incomplete code point:
                uint8_t *p = (uint8_t *)(raw + i);
                cp = (0xDC80ULL + (
                    (h64wchar)((uint8_t)*p)
                ));
                decodebuf[cpcount] = cp;
                cpcount++;
                i += 1;
            }
            *endsinmultipleinvalid = previouswasinvalid;
            break;
        } else if (charlen == 1) {
            cp = *p;
            assert(cp <= 127);
            previouswasinvalid = 0;
        } else {
            int out_len = 0;
            int cpresult = get_utf8_codepoint(
                p, charlen, &cp, &out_len
            );
            if (!cpresult) {
                assert(*((uint8_t*)p) > 127);
                charlen = 1;
                previouswasinvalid = (
                    i > consider_all_valid_upto
                );
                cp = (0xDC80ULL + (
                    (h64wchar)((uint8_t)*p)
                ));
            } else {
                previouswasinvalid = 0;
                assert(out_len == charlen);
            }
        }
        decodebuf[cpcount] = cp;
        cpcount++;
        i += charlen;
    }
    if (cpcount > 0) {
        lettercount = utf32_letters_count(
            decodebuf, cpcount
        );
    }
    *codepoints = cpcount;
    *letters = lettercount;
}

int iolib_fileread(
        h64vmthread *vmthread
        ) {
    /**
     * Read from the given file.
     *
     * @funcattr file read
     * @param len=-1 amount of bytes/letters to read. When
     *    the file was opened with binary=yes then amount will be
     *    interpreted as bytes, otherwise with binary=no as full
     *    decoded Unicode characters. Specify -1 to read
     *    everything until the end of the file.
     * @returns the data read, which is a @see{bytes} value when the
     *    file was opened with binary=yes, otherwise a @see{string}
     *    value.
     * @raises IOError raised when there is a failure that is NOT expected
     *    to go away with retrying, like reading from a file only opened
     *    for writing.
     * @raises ResourceError raised when there is unexpected resource
     *    exhaustion that MAY go away when retrying, like running out of
     *    file handles, read timeout, and so on.
     */
    assert(STACK_TOP(vmthread->stack) >= 2);

    valuecontent *vc = STACK_ENTRY(vmthread->stack, 1);  // first closure arg
    assert(vc->type == H64VALTYPE_GCVAL);
    h64gcvalue *gcvalue = (h64gcvalue *)vc->ptr_value;
    assert(gcvalue->type == H64GCVALUETYPE_OBJINSTANCE);
    assert(gcvalue->class_id ==
           vmthread->vmexec_owner->program->_io_file_class_idx);
    _fileobj_cdata *cdata = (gcvalue->cdata);
    FILE *f = cdata->file_handle;

    if (!f) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_IOERROR,
            "file was closed"
        );
    }
    if ((cdata->flags & FILEOBJ_FLAGS_CACHEDUNSENTERROR) != 0) {
        cdata->flags &= ~((uint8_t)FILEOBJ_FLAGS_CACHEDUNSENTERROR);
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_RESOURCEERROR,
            "unknown i/o error"
        );
    }

    if ((cdata->flags & FILEOBJ_FLAGS_READ) == 0) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_IOERROR,
            "not opened for reading"
        );
    }

    valuecontent *vcamount = STACK_ENTRY(vmthread->stack, 0);
    if ((vcamount->type != H64VALTYPE_INT64 &&
            vcamount->type != H64VALTYPE_FLOAT64) &&
            vcamount->type != H64VALTYPE_UNSPECIFIED_KWARG) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "len must be a number"
        );
    }
    int readbinary = ((cdata->flags & FILEOBJ_FLAGS_BINARY) != 0);
    int64_t amount = -1;
    if (vcamount->type == H64VALTYPE_INT64) {
        amount = vcamount->int_value;
    } else if (vcamount->type == H64VALTYPE_FLOAT64) {
        amount = clamped_round(vcamount->float_value);
    }
    if (amount == 0 || feof(f)) {
        valuecontent *vc = STACK_ENTRY(vmthread->stack, 0);
        DELREF_NONHEAP(vc);
        valuecontent_Free(vc);
        memset(vc, 0, sizeof(*vc));
        if (readbinary) {
            vc->type = H64VALTYPE_SHORTBYTES;
            vc->shortbytes_len = 0;
        } else {
            vc->type = H64VALTYPE_SHORTSTR;
            vc->shortstr_len = 0;
        }
        return 1;
    }

    char _stackreadbuf[1024];
    char *readbuf = _stackreadbuf;
    int64_t readbuffill = 0;
    int64_t readbufsize = 1024;
    int64_t readcount = 0;
    int readbufheap = 0;

    char _stackdecodebuf[1024];
    h64wchar *decodebuf = (h64wchar *)_stackdecodebuf;
    int decodebufsize = 1024 / sizeof(*decodebuf);
    int decodebuffill = 0;
    int decodebufheap = 0;

    if (ferror(f)) {
        clearerr(f);
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_RESOURCEERROR,
            "unknown I/O error"
        );
    }
    if ((cdata->flags & FILEOBJ_FLAGS_LASTWASWRITE) != 0) {
        cdata->flags &= ~((uint8_t)FILEOBJ_FLAGS_LASTWASWRITE);
        fflush(f);
        fseek64(f, 0, SEEK_CUR);
    }

    // Read from file up to requested amount:
    if (!readbinary || amount < 0) {
        while (readcount < amount || amount < 0) {
            int64_t readbytes = 0;
            if (amount > 0) {
                readbytes = (amount - readcount);
            } else {
                readbytes = 512;
            }
            if (readbytes + readbuffill + 1 >
                    readbufsize) {
                char *newbuf = NULL;
                int64_t newreadbufsize = (
                    readbytes + readbuffill + 1024 * 5
                );
                if (!readbufheap) {
                    newbuf = malloc(newreadbufsize);
                    if (newbuf) {
                        memcpy(newbuf, readbuf, readbuffill);
                    }
                } else {
                    newbuf = realloc(readbuf, newreadbufsize);
                }
                if (!newbuf) {
                    if (readbufheap)
                        free(readbuf);
                    if (decodebufheap)
                        free(decodebuf);
                    return vmexec_ReturnFuncError(
                        vmthread, H64STDERROR_OUTOFMEMORYERROR,
                        "out of memory resizing read buf"
                    );
                }
                readbufheap = 1;
                readbuf = newbuf;
                readbufsize = newreadbufsize;
            }
            int64_t _didread = fread(
                readbuf + readbuffill, 1, readbytes, f
            );
            if (_didread <= 0) {
                if (feof(f)) {
                    break;
                } else if (ferror(f)) {
                    clearerr(f);
                    if (readbuffill <= 0) {
                        if (readbufheap)
                            free(readbuf);
                        if (decodebufheap)
                            free(decodebuf);
                        return vmexec_ReturnFuncError(
                            vmthread, H64STDERROR_RESOURCEERROR,
                            "unknown I/O error"
                        );
                    }
                    cdata->flags |= FILEOBJ_FLAGS_CACHEDUNSENTERROR;
                    break;
                }
                continue;
            }
            assert(_didread > 0);
            int64_t oldfill = readbuffill;
            readbuffill += _didread;
            if (readbinary) {
                readcount += _didread;
            } else if (amount >= 0) {
                int64_t orig_lettercount = -1;
                int64_t orig_codepoints = -1;
                int orig_last_cp_incomplete = 0;
                int64_t last_stable_lettercount = -1;
                int64_t last_stable_lettercount_at = -1;
                int64_t extra_read_bytes = 0;
                // Ok, we need to read further until we firstly
                // have no incomplete code point, secondly letter count
                // changes (so we know we hit the next letter.
                int _ferrorabort = 0;
                while (1) {
                    // Make sure we got enough decode space:
                    if (decodebufsize < (readbuffill - oldfill) + 3) {
                        int64_t newsize = (
                            (readbuffill - oldfill) + 3 + decodebufsize * 2
                        );
                        h64wchar *decodebufnew = NULL;
                        if (decodebufheap) {
                            decodebufnew = realloc(
                                decodebuf, sizeof(*decodebuf) * newsize
                            );
                        } else {
                            decodebufnew = malloc(
                                sizeof(*decodebuf) * newsize
                            );
                            if (decodebufnew && decodebuffill > 0)
                                memcpy(
                                    decodebufnew, decodebuf,
                                    sizeof(*decodebuf) * decodebuffill
                                );
                        }
                        if (!decodebufnew) {
                            if (readbufheap)
                                free(readbuf);
                            if (decodebufheap)
                                free(decodebuf);
                            return vmexec_ReturnFuncError(
                                vmthread, H64STDERROR_OUTOFMEMORYERROR,
                                "out of memory resizing decode buf"
                            );
                        }
                    }
                    // Make sure we take stock of initial data if not done:
                    if (orig_lettercount < 0) {
                        int endsinmultipleinvalid = 0;
                        _count_actual_letters_in_raw(
                            (const uint8_t *)readbuf + oldfill,
                            readbuffill - oldfill,
                            decodebuf, last_stable_lettercount_at,
                            &orig_codepoints, &orig_lettercount,
                            &orig_last_cp_incomplete, &endsinmultipleinvalid
                        );
                        assert(orig_codepoints >= 0);
                        assert(orig_lettercount >= 0);
                        if (!orig_last_cp_incomplete &&
                                last_stable_lettercount < 0) {
                            last_stable_lettercount = orig_lettercount;
                            last_stable_lettercount_at = readbuffill;
                        }
                    }
                    // Read one more byte until letter count changes:
                    int64_t _didread2 = fread(
                        readbuf + readbuffill, 1, 1, f
                    );
                    if (_didread2 <= 0) {
                        if (feof(f)) {
                            // See if we must revert:
                            int64_t newcodepoints = -1;
                            int64_t newletters = -1;
                            int endsinmultipleinvalid = 0;
                            int newlastcpincomplete = 0;
                            _count_actual_letters_in_raw(
                                (const uint8_t *)readbuf + oldfill,
                                (readbuffill - oldfill),
                                decodebuf, last_stable_lettercount_at,
                                &newcodepoints, &newletters,
                                &newlastcpincomplete, &endsinmultipleinvalid
                            );
                            if (last_stable_lettercount >= 0 &&
                                    newlastcpincomplete &&
                                    newletters > last_stable_lettercount) {
                                // Our last pre-EOF addition can not be
                                // considered a valid appendage that counts
                                // into same letter. -> must exclude it
                                goto dorevert;
                            }
                            break;
                        } else if (ferror(f)) {
                            clearerr(f);
                            if (readbuffill <= 0) {
                                if (readbufheap)
                                    free(readbuf);
                                if (decodebufheap)
                                    free(decodebuf);
                                return vmexec_ReturnFuncError(
                                    vmthread, H64STDERROR_RESOURCEERROR,
                                    "unknown I/O error"
                                );
                            }
                            cdata->flags |= FILEOBJ_FLAGS_CACHEDUNSENTERROR;
                            _ferrorabort = 1;
                            break;
                        }
                        continue;
                    }
                    // Ok, see how what we've read changes letter count:
                    extra_read_bytes += _didread2;
                    readbuffill +=_didread2;
                    int64_t newcodepoints = -1;
                    int64_t newletters = -1;
                    int endsinmultipleinvalid = 0;
                    int newlastcpincomplete = 0;
                    _count_actual_letters_in_raw(
                        (const uint8_t *)readbuf + oldfill,
                        (readbuffill - oldfill),
                        decodebuf, last_stable_lettercount_at,
                        &newcodepoints, &newletters,
                        &newlastcpincomplete, &endsinmultipleinvalid
                    );
                    if (endsinmultipleinvalid &&
                            last_stable_lettercount < 0) {
                        // Since we got two unrelated broken ones
                        // after one another, the first broken one
                        // now counts as a separate completed char.
                        // (Since the first broken one for sure isn't
                        // completed/repaired by anything after, so it's
                        // always going to be a singled out broken item.)
                        assert(readbuffill >= oldfill + 2);
                        last_stable_lettercount_at = readbuffill - 1;
                        // Count how many letters that would be:
                        int64_t _innercodepoints = -1;
                        int64_t _innerletters = -1;
                        int _innerendsinmultipleinvalid = 0;
                        int _innernewlastcpincomplete = 0;
                        _count_actual_letters_in_raw(
                            (const uint8_t *)readbuf + oldfill,
                            (last_stable_lettercount_at - oldfill),
                            decodebuf, last_stable_lettercount_at,
                            &_innercodepoints, &_innerletters,
                            &_innernewlastcpincomplete,
                            &_innerendsinmultipleinvalid
                        );
                        last_stable_lettercount = _innerletters;
                        assert(_innerletters >= 0);
                        // Update the statistics since everything up to
                        // 'last_stable_lettercount_at' (which we just changed)
                        // must be considered valid characters, such that
                        // an invalid broken standalone glyph taken as stable
                        // end can have a valid multibyte unicode tag
                        // as a follow-up (which would otherwise trigger
                        // endsismultipleinvalid=1):
                        _count_actual_letters_in_raw(  // repeat from before.
                            (const uint8_t *)readbuf + oldfill,
                            (readbuffill - oldfill),
                            decodebuf, last_stable_lettercount_at,
                            &newcodepoints, &newletters,
                            &newlastcpincomplete, &endsinmultipleinvalid
                        );
                    }
                    if (last_stable_lettercount < 0 &&
                            !endsinmultipleinvalid &&
                            !newlastcpincomplete) {
                        // We didn't have a complete last letter yet,
                        // so take this even if it might be not a full
                        // glyph/letter as a minimum base.
                        last_stable_lettercount_at = readbuffill;
                        last_stable_lettercount = newletters;
                    } else if (newletters == last_stable_lettercount &&
                            !newlastcpincomplete) {
                        // This didn't increase letter count and it's
                        // not obviously incomplete/broken, so count it
                        // as 'established' part of our previous chunk
                        // (so we don't revert further back later):
                        last_stable_lettercount_at = readbuffill;
                        last_stable_lettercount = newletters;
                    } else if (last_stable_lettercount >= 0 &&
                            newletters > last_stable_lettercount &&
                            (!newlastcpincomplete ||
                             endsinmultipleinvalid)) {
                        dorevert:
                        // Ok, this was too far. Revert.
                        assert(last_stable_lettercount_at >= 0);
                        int64_t revert_dist = (
                            readbuffill - last_stable_lettercount_at
                        );
                        assert(revert_dist > 0);
                        int result = fseek64(
                            f, -revert_dist, SEEK_CUR
                        );
                        if (result != 0) {
                            if (readbufheap)
                                free(readbuf);
                            if (decodebufheap)
                                free(decodebuf);
                            return vmexec_ReturnFuncError(
                                vmthread, H64STDERROR_RESOURCEERROR,
                                "unknown I/O error"
                            );
                        }
                        readbuffill -= revert_dist;
                        assert(readbuffill > 0);
                        extra_read_bytes -= revert_dist;
                        // Ok, we should be at the end of this letter now.
                        // Add up how many new letters we have:
                        readcount += (newletters - 1);
                        break;
                    }
                }
                if (_ferrorabort || feof(f))
                    break;
            }
        }
        #ifndef NDEBUG
        if (!readbinary) {
            assert(
                (readcount == 0 && amount < 0) ||
                (readcount >= 0 && amount >= 0)
            );
        }
        #endif
    } else {
        assert(amount > 0);
        if (amount > readbufsize) {
            if (readbufheap)
                free(readbuf);
            readbufheap = 1;
            readbuf = malloc(
                sizeof(*readbuf) * readbufsize
            );
            if (!readbuf) {
                if (decodebufheap)
                    free(decodebuf);
                return vmexec_ReturnFuncError(
                    vmthread, H64STDERROR_OUTOFMEMORYERROR,
                    "out of memory resizing read buf"
                );
            }
        }
        int64_t _didread = fread(
            readbuf, 1, amount, f
        );
        if (_didread < 0) {
            if (feof(f)) {
                _didread = 0;
            } else if (ferror(f)) {
                clearerr(f);
                if (readbufheap)
                    free(readbuf);
                if (decodebufheap)
                    free(decodebuf);
                return vmexec_ReturnFuncError(
                    vmthread, H64STDERROR_RESOURCEERROR,
                    "unknown I/O error"
                );
            }
        }
        readbuffill = _didread;
    }
    if (decodebufheap) {
        free(decodebuf);
        decodebuf = NULL;
    }

    valuecontent *vresult = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vresult);
    valuecontent_Free(vresult);
    memset(vresult, 0, sizeof(*vresult));

    if (!readbinary && readbuffill == 0) {
        vresult->type = H64VALTYPE_SHORTSTR;
        vresult->constpreallocstr_len = 0;
    } else if (!readbinary) {
        char _convertedbuf[1024];
        h64wchar* converted = (h64wchar*)_convertedbuf;
        int64_t convertedlen = 0;
        int abortedinvalid = 0;
        int abortedoom = 0;
        converted = utf8_to_utf32_ex(
            readbuf, readbuffill,
            (char*)converted, sizeof(_convertedbuf),
            NULL, NULL, &convertedlen,
            ((cdata->flags & FILEOBJ_FLAGS_IGNOREENCODINGERROR) != 0),
            0, &abortedinvalid, &abortedoom
        );
        if (abortedinvalid) {
            assert(!converted);
            if (readbufheap)
                free(readbuf);
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_ENCODINGERROR,
                "invalid encoded character in file"
            );
        }
        if (abortedoom) {
            assert(!converted);
            if (readbufheap)
                free(readbuf);
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_OUTOFMEMORYERROR,
                "decode buffer alloc failure"
            );
        }
        assert(converted != NULL);
        if (readbufheap)
            free(readbuf);
        int convertedonheap = (
            (char*)converted != _convertedbuf
        );
        vresult->type = H64VALTYPE_GCVAL;
        vresult->ptr_value = poolalloc_malloc(
            vmthread->heap, 0
        );
        if (!vresult->ptr_value) {
            if (convertedonheap)
                free(converted);
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_OUTOFMEMORYERROR,
                "result value alloc failure"
            );
        }
        h64gcvalue *gcval = vresult->ptr_value;
        memset(gcval, 0, sizeof(*gcval));
        gcval->type = H64GCVALUETYPE_STRING;
        if (!vmstrings_AllocBuffer(
                vmthread, &gcval->str_val,
                convertedlen
                )) {
            poolalloc_free(vmthread->heap, gcval);
            vresult->ptr_value = NULL;
            vresult->type = H64VALTYPE_NONE;
            if (convertedonheap)
                free(converted);
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_OUTOFMEMORYERROR,
                "result value alloc failure"
            );
        }
        memcpy(
            gcval->str_val.s, converted, convertedlen * sizeof(h64wchar)
        );
        assert(gcval->str_val.len == (uint64_t)convertedlen);
        if (convertedonheap)
            free(converted);
        gcval->str_val.refcount = 1;
        gcval->externalreferencecount = 1;
        vmstrings_RequireLetterLen(
            &gcval->str_val
        );
        cdata->text_offset += gcval->str_val.letterlen;
    } else if (readbinary && readbuffill == 0) {
        vresult->type = H64VALTYPE_SHORTBYTES;
        vresult->constpreallocbytes_len = 0;
    } else {
        assert(readbinary);
        vresult->type = H64VALTYPE_GCVAL;
        vresult->ptr_value = poolalloc_malloc(
            vmthread->heap, 0
        );
        if (!vresult->ptr_value) {
            if (readbufheap)
                free(readbuf);
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_OUTOFMEMORYERROR,
                "result value alloc failure"
            );
        }
        h64gcvalue *gcval = vresult->ptr_value;
        memset(gcval, 0, sizeof(*gcval));
        gcval->type = H64GCVALUETYPE_BYTES;
        if (readbuffill > 0) {
            if (!vmbytes_AllocBuffer(
                    vmthread, &gcval->bytes_val,
                    readbuffill
                    )) {
                poolalloc_free(vmthread->heap, gcval);
                vresult->ptr_value = NULL;
                vresult->type = H64VALTYPE_NONE;
                if (readbufheap)
                    free(readbuf);
                return vmexec_ReturnFuncError(
                    vmthread, H64STDERROR_OUTOFMEMORYERROR,
                    "result value alloc failure"
                );
            }
            memcpy(gcval->bytes_val.s, readbuf, readbuffill);
        }
        if (readbufheap)
            free(readbuf);
        gcval->bytes_val.refcount = 1;
        gcval->externalreferencecount = 1;
    }
    return 1;
}

int iolib_fileseek(
        h64vmthread *vmthread
        ) {
    /**
     * Seek to the given offset in the given file. This is only
     * available in binary mode, in non-binary mode only linear reading and
     * writing is possible.
     *
     * @funcattr io.file seek
     * @param offset the offset to seek to. A negative number, like -1, will
     *    seek to the end of the file.
     * @raises TypeError raised when trying to seek on a file that doesn't
     *    support seeking, like a file in non-binary mode
     * @raises IOERROR raised when there is a failure that is NOT expected
     *    to go away with retrying, like wrong file type, permission errors,
     *    file was closed, etc.
     * @raises OSError raised when there is unexpected resource
     *    exhaustion that MAY go away when retrying, like running out of
     *    file handles, read timeout, and so on.
     */
    assert(STACK_TOP(vmthread->stack) >= 2);

    valuecontent *vc = STACK_ENTRY(vmthread->stack, 1);  // first closure arg
    assert(vc->type == H64VALTYPE_GCVAL);
    h64gcvalue *gcvalue = (h64gcvalue *)vc->ptr_value;
    assert(gcvalue->type == H64GCVALUETYPE_OBJINSTANCE);
    assert(gcvalue->class_id ==
           vmthread->vmexec_owner->program->_io_file_class_idx);
    _fileobj_cdata *cdata = (gcvalue->cdata);

    if (cdata->file_handle == NULL) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_IOERROR,
            "file was closed"
        );
    } else if (ferror(cdata->file_handle) ||
            (cdata->flags & FILEOBJ_FLAGS_CACHEDUNSENTERROR) != 0) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_RESOURCEERROR,
            "file has encountered read/write error"
        );
    } else if ((cdata->flags & FILEOBJ_FLAGS_BINARY) != 0) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "cannot seek in non-binary mode file"
        );
    }

    valuecontent *vcoffset = STACK_ENTRY(vmthread->stack, 0);
    if (vcoffset->type != H64VALTYPE_INT64 &&
            vcoffset->type != H64VALTYPE_FLOAT64) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "offset must be a number"
        );
    }
    int64_t offset = -1;
    if (vcoffset->type != H64VALTYPE_INT64)
        offset = vcoffset->int_value;
    else
        offset = clamped_round(vcoffset->float_value);

    int result = -1;
    if (offset < 0) {
        result = fseek64(cdata->file_handle, 0, SEEK_SET);
    } else {
        result = fseek64(cdata->file_handle, offset, SEEK_CUR);
    }
    if (result != 0) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_RESOURCEERROR,
            "unexpected seek failure"
        );
    }

    valuecontent *vresult = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vresult);
    valuecontent_Free(vresult);
    memset(vresult, 0, sizeof(*vresult));
    vresult->type = H64VALTYPE_NONE;
    return 1;
}

int iolib_fileoffset(
        h64vmthread *vmthread
        ) {
    /**
     * Get the seek offset in the given file, which in binary mode is
     * the read position in bytes from the beginning of the file, and
     * in non-binary mode is the letters read so far.
     *
     * @funcattr io.file offset
     * @raises IOERROR raised when there is a failure that is NOT expected
     *    to go away with retrying, like wrong file type, permission errors,
     *    file was closed, etc.
     * @raises OSError raised when there is unexpected resource
     *    exhaustion that MAY go away when retrying, like running out of
     *    file handles, read timeout, and so on.
     */
    assert(STACK_TOP(vmthread->stack) >= 1);

    valuecontent *vc = STACK_ENTRY(vmthread->stack, 0);  // first closure arg
    assert(vc->type == H64VALTYPE_GCVAL);
    h64gcvalue *gcvalue = (h64gcvalue *)vc->ptr_value;
    assert(gcvalue->type == H64GCVALUETYPE_OBJINSTANCE);
    assert(gcvalue->class_id ==
           vmthread->vmexec_owner->program->_io_file_class_idx);
    _fileobj_cdata *cdata = (gcvalue->cdata);

    if (cdata->file_handle == NULL) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_IOERROR,
            "file was closed"
        );
    } else if (ferror(cdata->file_handle) ||
            (cdata->flags & FILEOBJ_FLAGS_CACHEDUNSENTERROR) != 0) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_RESOURCEERROR,
            "file has encountered read/write error"
        );
    }
    int64_t result = -1;
    if ((cdata->flags & FILEOBJ_FLAGS_BINARY) != 0) {
        result = ftell64(cdata->file_handle);
    } else {
        result = cdata->text_offset;
    }
    if (result < 0) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_RESOURCEERROR,
            "unexpected ftell64() error"
        );
    }
    valuecontent *vresult = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vresult);
    valuecontent_Free(vresult);
    memset(vresult, 0, sizeof(*vresult));
    vresult->type = H64VALTYPE_INT64;
    vresult->int_value = result;
    return 1;
}

int iolib_fileclose(
        h64vmthread *vmthread
        ) {
    /**
     * Close the given file. If it was already closed then nothing happens,
     * so it is safe to close a file multiple times.
     *
     * @funcattr io.file close
     * @raises IOERROR raised when there is a failure that is NOT expected
     *    to go away with retrying, like wrong file type, permission errors,
     *    full disk, etc.
     * @raises OSError raised when there is unexpected resource
     *    exhaustion that MAY go away when retrying, like running out of
     *    file handles, read timeout, and so on.
     */
    assert(STACK_TOP(vmthread->stack) >= 1);

    valuecontent *vc = STACK_ENTRY(vmthread->stack, 0);  // first closure arg
    assert(vc->type == H64VALTYPE_GCVAL);
    h64gcvalue *gcvalue = (h64gcvalue *)vc->ptr_value;
    assert(gcvalue->type == H64GCVALUETYPE_OBJINSTANCE);
    assert(gcvalue->class_id ==
           vmthread->vmexec_owner->program->_io_file_class_idx);
    _fileobj_cdata *cdata = (gcvalue->cdata);

    if (cdata->file_handle != NULL) {
        fclose(cdata->file_handle);
        cdata->file_handle = NULL;
    }
    // Clear error, since file is closed anyway:
    cdata->flags &= ~((uint8_t)FILEOBJ_FLAGS_CACHEDUNSENTERROR);
    return 1;
}

int iolib_RegisterFuncsAndModules(h64program *p) {
    // io.open:
    const char *io_open_kw_arg_name[] = {
        NULL, "read", "write", "append", "binary",
        "allow_bad_encoding"
    };
    int64_t idx;
    idx = h64program_RegisterCFunction(
        p, "open", &iolib_open,
        NULL, 0, 6, io_open_kw_arg_name,  // fileuri, args
        "io", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    // file class:
    p->_io_file_class_idx = h64program_AddClass(
        p, "file", NULL, 0, "io", "core.horse64.org"
    );
    if (p->_io_file_class_idx < 0)
        return 0;

    // file.read method:
    const char *io_fileread_kw_arg_name[] = {"len"};
    idx = h64program_RegisterCFunction(
        p, "read", &iolib_fileread,
        NULL, 0, 1, io_fileread_kw_arg_name,  // fileuri, args
        "io", "core.horse64.org", 1, p->_io_file_class_idx
    );
    if (idx < 0)
        return 0;

    // file.write method:
    const char *io_filewrite_kw_arg_name[] = {NULL};
    idx = h64program_RegisterCFunction(
        p, "write", &iolib_filewrite,
        NULL, 0, 1, io_filewrite_kw_arg_name,  // fileuri, args
        "io", "core.horse64.org", 1, p->_io_file_class_idx
    );
    if (idx < 0)
        return 0;

    // file.offset method:
    const char *io_fileoffset_kw_arg_name[] = {};
    idx = h64program_RegisterCFunction(
        p, "offset", &iolib_fileoffset,
        NULL, 0, 0, io_fileoffset_kw_arg_name,  // fileuri, args
        "io", "core.horse64.org", 1, p->_io_file_class_idx
    );
    if (idx < 0)
        return 0;

    // file.seek method:
    const char *io_fileseek_kw_arg_name[] = {NULL};
    idx = h64program_RegisterCFunction(
        p, "seek", &iolib_fileseek,
        NULL, 0, 1, io_fileseek_kw_arg_name,  // fileuri, args
        "io", "core.horse64.org", 1, p->_io_file_class_idx
    );
    if (idx < 0)
        return 0;

    // file.close method:
    idx = h64program_RegisterCFunction(
        p, "close", &iolib_fileclose,
        NULL, 0, 0, NULL,  // fileuri, args
        "io", "core.horse64.org", 1, p->_io_file_class_idx
    );
    if (idx < 0)
        return 0;

    return 1;
}
