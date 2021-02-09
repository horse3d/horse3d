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
#include "filesys32.h"
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

/// @module io A module for manipulation files and directories
///     on disk, using whatever filesystems your computer provides.

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
     *    interpreted as bytes, otherwise with binary=no as fully
     *    decoded text (decoded from utf-8). Specify -1 to read
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
    if (amount < 0) {
        while (1) {
            int64_t readbytes = 512;
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
            int64_t _didread = 0;
            if (!feof(f))
                _didread = fread(
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
        }
    } else if (!readbinary) {
        assert(amount >= 0);
        int64_t complete_guaranteed_letters_count = 0;
        int complete_guaranteed_letters_count_at = 0;
        int hadeof = 0;
        int haderror = 0;
        while (1) {
            // If we read beyond what we determined to be "complete",
            // take stock of how much completed letters we have:
            if (readbuffill > complete_guaranteed_letters_count_at) {
                int64_t k = readbuffill;
                while (k > complete_guaranteed_letters_count_at) {
                    // See how much letters we got at this index:
                    int64_t _codepoints, _letters;
                    int _lastincomplete, _endsmultipleinvalid;
                    _count_actual_letters_in_raw(
                        (const uint8_t *)readbuf +
                            complete_guaranteed_letters_count_at,
                        k - complete_guaranteed_letters_count_at,
                        decodebuf, 0, &_codepoints, &_letters,
                        &_lastincomplete, &_endsmultipleinvalid
                    );
                    if (_letters <= 1)
                        break;
                    if (!_lastincomplete || _endsmultipleinvalid) {
                        // See where we drop below this letter count:
                        int64_t k2 = k - 1;
                        while (k2 > complete_guaranteed_letters_count) {
                            int64_t _codepoints2, _letters2;
                            int _lastincomplete2, _endsmultipleinvalid2;
                            _count_actual_letters_in_raw(
                                (const uint8_t *)readbuf +
                                    complete_guaranteed_letters_count_at,
                                k2 - complete_guaranteed_letters_count_at,
                                decodebuf, 0, &_codepoints2, &_letters2,
                                &_lastincomplete2, &_endsmultipleinvalid2
                            );
                            if (_letters2 < _letters) {
                                complete_guaranteed_letters_count_at = (
                                    k2
                                );
                                complete_guaranteed_letters_count += (
                                    _letters2
                                );
                                break;
                            }
                            k2--;
                        }
                        break;
                    }
                    k--;
                }
            }
            if (complete_guaranteed_letters_count >= amount ||
                    hadeof || haderror) {
                // We reached what we wanted, or encountered an error.
                break;  // done.
            }
            // Read more bytes:
            int64_t readbytes = (
                amount - complete_guaranteed_letters_count
            ) * 1.5;  // just a wild guess how much we might need
            if (readbytes < 16) {
                readbytes = 16;
            }
            // Make sure our buffer is large enough:
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
            // Make sure decode buffer is large enough:
            if (readbytes + decodebuffill + 1 >
                    decodebufsize) {
                h64wchar *newbuf = NULL;
                int64_t newdecodebusize = (
                    readbytes + decodebuffill + 512
                );
                if (!decodebufheap) {
                    newbuf = malloc(newdecodebusize * sizeof(*newbuf));
                    if (newbuf) {
                        memcpy(newbuf, decodebuf,
                            decodebuffill * sizeof(*newbuf));
                    }
                } else {
                    newbuf = realloc(
                        decodebuf, newdecodebusize * sizeof(*newbuf)
                    );
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
                decodebufheap = 1;
                decodebuf = newbuf;
                decodebufsize = newdecodebusize;
            }
            // Do the reading:
            size_t amountread = fread(
                readbuf + readbuffill, 1, readbytes, f
            );
            if (amountread <= 0) {
                if (ferror(f)) {
                    haderror = 1;
                } else {
                    hadeof = 1;
                }
            } else {
                readbuffill += amountread;
            }
        }
        // Figure out the buffer bytes to return that actually
        // make up the requested letters.
        int64_t letterssofar = 0;
        int64_t i = 0;
        while (i < readbuffill && letterssofar < amount) {
            int64_t bytes = utf32_next_letter_byteslen_in_utf8(
                readbuf + i, readbuffill - i
            );
            if (bytes < 0) {
                if (readbufheap)
                    free(readbuf);
                if (decodebufheap)
                    free(decodebuf);
                return vmexec_ReturnFuncError(
                    vmthread, H64STDERROR_OUTOFMEMORYERROR,
                    "out of memory determining read buf reverse"
                );
            }
            if (bytes <= 0)
                break;
            letterssofar++;
            i += bytes;
        }
        if (!haderror) {
            // We want to seek back what we overshot.
            int64_t overshotbytes = (
                readbuffill - i
            );
            if (overshotbytes > 0) {
                if (fseek64(f, -overshotbytes, SEEK_CUR) != 0) {
                    clearerr(f);
                    return vmexec_ReturnFuncError(
                        vmthread, H64STDERROR_RESOURCEERROR,
                        "unexpected I/O error reading file"
                    );
                }
            }
        }
        if (haderror) {
            clearerr(f);
            if (i <= 0) {
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
        }
        readbuffill = i;  // Truncate to what we want to return.
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
        if (readbufheap)
            free(readbuf);
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
        if (readbufheap)
            free(readbuf);
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
    clearerr(cdata->file_handle);

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


int iolib_get_unix_perms(
        h64vmthread *vmthread
        ) {
    /**
     * Get Unix style permissions from the target as an
     * octal permission string, e.g. "0755" for
     * user read/write/exec, group read/exec, any read/exec.
     *
     * On non-Unix platforms, this will raise a NotImplementedError.
     *
     * @func get_unix_perms
     * @param path the target from which to get Unix style permissions
     * @raises NotImplementedError raised on non-Unix platforms that
     *     do not support this permission style.
     * @raises IOError raised when there was an error getting permissions
     *     that will likely persist on immediate retry, like lack of
     *     access permission, the target not existing, and so on.
     * @raises ResourceError raised when there is a failure that might go
     *     away on retry, like an unexpected disk input output error,
     *     a read timeout, and similar.
     */
    assert(STACK_TOP(vmthread->stack) >= 1);

    h64wchar *pathstr = NULL;
    int64_t pathlen = 0;
    valuecontent *vcpath = STACK_ENTRY(vmthread->stack, 0);
    if (vcpath->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue*)(vcpath->ptr_value))->type ==
                H64GCVALUETYPE_STRING) {
        pathstr = (
            ((h64gcvalue*)(vcpath->ptr_value))->str_val.s
        );
        pathlen = (
            ((h64gcvalue*)(vcpath->ptr_value))->str_val.len
        );
    } else if (vcpath->type == H64VALTYPE_SHORTSTR) {
        pathstr = vcpath->shortstr_value;
        pathlen = vcpath->shortstr_len;
    } else {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "path argument must be a string"
        );
    }

    int permextra, permuser, permgroup, permany;
    int _err = 0;
    int result = filesys32_GetOctalPermissions(
        pathstr, pathlen, &_err, &permextra,
        &permuser, &permgroup, &permany
    );
    if (!result) {
        if (_err == FS32_ERR_UNSUPPORTEDPLATFORM) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_NOTIMPLEMENTEDERROR,
                "not a unix platform"
            );
        } else if (_err == FS32_ERR_NOPERMISSION) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "permission denied"
            );
        } else if (_err == FS32_ERR_NOSUCHTARGET) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "no such file or directory"
            );
        } else if (_err == FS32_ERR_OUTOFFDS) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_RESOURCEERROR,
                "out of file descriptors"
            );
        }
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_RESOURCEERROR,
            "unexpected type of I/O error"
        );
    }

    int64_t slen = 4;
    h64wchar s[4];
    s[0] = '0' + permextra;
    s[1] = '0' + permuser;
    s[2] = '0' + permgroup;
    s[3] = '0' + permany;
    valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vcresult);
    valuecontent_Free(vcresult);
    vcresult->type = H64VALTYPE_NONE;
    if (!valuecontent_SetStringU32(
            vmthread, vcresult, s, slen)) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OUTOFMEMORYERROR,
            "out of memory returning permission string"
        );
    }
    return 1;
}

int iolib_set_unix_perms(
        h64vmthread *vmthread
        ) {
    /**
     * Directly set Unix style permissions on the target from
     * the given octal permission string, e.g. "0755" for
     * user read/write/exec, group read/exec, any read/exec.
     *
     * On non-Unix platforms, this will raise a NotImplementedError.
     *
     * @func set_unix_perms
     * @param path the target for which to set Unix style permissions
     * @param permissions the permissions string, e.g. "0755".
     * @raises NotImplementedError raised on non-Unix platforms that
     *     do not support this permission style.
     * @raises IOError raised when there was an error setting permissions
     *     that will likely persist on immediate retry, like lack of
     *     permission, the target not existing, and so on.
     * @raises ResourceError raised when there is a failure that might go
     *     away on retry, like an unexpected disk input output error,
     *     a write timeout, and similar.
     */
    assert(STACK_TOP(vmthread->stack) >= 2);

    h64wchar *pathstr = NULL;
    int64_t pathlen = 0;
    valuecontent *vcpath = STACK_ENTRY(vmthread->stack, 0);
    if (vcpath->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue*)(vcpath->ptr_value))->type ==
                H64GCVALUETYPE_STRING) {
        pathstr = (
            ((h64gcvalue*)(vcpath->ptr_value))->str_val.s
        );
        pathlen = (
            ((h64gcvalue*)(vcpath->ptr_value))->str_val.len
        );
    } else if (vcpath->type == H64VALTYPE_SHORTSTR) {
        pathstr = vcpath->shortstr_value;
        pathlen = vcpath->shortstr_len;
    } else {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "path argument must be a string"
        );
    }

    h64wchar *permstr = NULL;
    int64_t permlen = 0;
    valuecontent *vcperm = STACK_ENTRY(vmthread->stack, 1);
    if (vcperm->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue*)(vcpath->ptr_value))->type ==
                H64GCVALUETYPE_STRING) {
        permstr = (
            ((h64gcvalue*)(vcperm->ptr_value))->str_val.s
        );
        permlen = (
            ((h64gcvalue*)(vcperm->ptr_value))->str_val.len
        );
    } else if (vcperm->type == H64VALTYPE_SHORTSTR) {
        permstr = vcperm->shortstr_value;
        permlen = vcperm->shortstr_len;
    } else {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "permissions argument must be a string"
        );
    }

    if (permlen != 3 && permlen != 4) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_ARGUMENTERROR,
            "permissions string must be 3 or 4 digits"
        );
    }
    if ((permstr[0] < '0' || permstr[0] > '9') ||
            (permstr[1] < '0' || permstr[1] > '9') ||
            (permstr[2] < '0' || permstr[2] > '9') ||
            (permlen == 4 && (
                permstr[3] < '0' || permstr[3] > '9'))) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_ARGUMENTERROR,
            "permissions string must be 3 or 4 digits"
        );
    }

    int permextra = (
        (permlen == 4) ? (permstr[0] - '0') : 0
    );
    int permuser = (
        (permlen == 4) ? (permstr[1] - '0') : (permstr[0] - '0')
    );
    int permgroup = (
        (permlen == 4) ? (permstr[2] - '0') : (permstr[1] - '0')
    );
    int permany = (
        (permlen == 4) ? (permstr[3] - '0') : (permstr[2] - '0')
    );

    int _err = 0;
    int result = filesys32_SetOctalPermissions(
        pathstr, pathlen, &_err, permextra, permuser,
        permgroup, permany
    );
    if (!result) {
        if (_err == FS32_ERR_UNSUPPORTEDPLATFORM) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_NOTIMPLEMENTEDERROR,
                "not a unix platform"
            );
        } else if (_err == FS32_ERR_NOPERMISSION) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "permission denied"
            );
        } else if (_err == FS32_ERR_NOSUCHTARGET) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "no such file or directory"
            );
        } else if (_err == FS32_ERR_OUTOFFDS) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_RESOURCEERROR,
                "out of file descriptors"
            );
        }
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_RESOURCEERROR,
            "unexpected type of I/O error"
        );
    }

    valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vcresult);
    valuecontent_Free(vcresult);
    vcresult->type = H64VALTYPE_NONE;
    return 1;
}

int iolib_set_as_exec(
        h64vmthread *vmthread
        ) {
    /**
     * Change the permissions of the given target to mark it as an
     * executable.
     *
     * On platforms where this is not meaningful (like MS Windows),
     * the target will be verified to exist and otherwise nothing is done.
     * Therefore, it is safe to use this function on all platforms.
     *
     * @func set_as_exec
     * @param path the target which to mark as executable
     * @param enabled=yes whether to mark it as executable (enabled=yes,
     *     the default), or the opposite and unmark it so it is no
     *     longer executable (enabled=no).
     * @raises IOError raised when there was an error marking the item
     *     that will likely persist on immediate retry, like lack of
     *     permission, the target not existing, and so on.
     * @raises ResourceError raised when there is a failure that might go
     *     away on retry, like an unexpected disk input output error,
     *     a read timeout, and similar.
     */
    assert(STACK_TOP(vmthread->stack) >= 2);

    h64wchar *pathstr = NULL;
    int64_t pathlen = 0;
    valuecontent *vcpath = STACK_ENTRY(vmthread->stack, 0);
    if (vcpath->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue*)(vcpath->ptr_value))->type ==
                H64GCVALUETYPE_STRING) {
        pathstr = (
            ((h64gcvalue*)(vcpath->ptr_value))->str_val.s
        );
        pathlen = (
            ((h64gcvalue*)(vcpath->ptr_value))->str_val.len
        );
    } else if (vcpath->type == H64VALTYPE_SHORTSTR) {
        pathstr = vcpath->shortstr_value;
        pathlen = vcpath->shortstr_len;
    } else {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "path argument must be a string"
        );
    }

    int enabled = 0;
    valuecontent *vcenabled = STACK_ENTRY(vmthread->stack, 1);
    if (vcenabled->type == H64VALTYPE_BOOL) {
        enabled = (vcenabled->int_value != 0);
    } else if (vcenabled->type == H64VALTYPE_UNSPECIFIED_KWARG) {
        enabled = 1;
    } else {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "enabled argument must be a boolean"
        );
    }

    int _err = 0;
    int result = filesys32_SetExecutable(
        pathstr, pathlen, &_err
    );
    if (!result) {
        if (_err == FS32_ERR_NOPERMISSION) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "permission denied"
            );
        } else if (_err == FS32_ERR_NOSUCHTARGET) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "no such file or directory"
            );
        } else if (_err == FS32_ERR_OUTOFFDS) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_RESOURCEERROR,
                "out of file descriptors"
            );
        }
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_RESOURCEERROR,
            "unexpected type of I/O error"
        );
    }

    valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vcresult);
    valuecontent_Free(vcresult);
    vcresult->type = H64VALTYPE_NONE;
    return 1;
}

int iolib_remove(
        h64vmthread *vmthread
        ) {
    /**
     * Remove the target represented given by the filesystem path, which
     * may be a file or a directory. If the function returns without an
     * error then the target was successfully removed.
     *
     * **Note on symbolic links:** on Unix symbolic links will not be followed
     * for recursive deletion, so if the given target is a symbolic link or
     * contains symbolic links to other folders only the links themselves
     * will be removed. On Windows, there is no differentiation and even
     * symbolic links or junctions will have their contents recursively
     * deleted.
     *
     * @func remove
     * @param path the filesystem path of the item to be removed
     * @param recursive=no whether a non-empty directory will cause an
     *     IOError (default, recursive=no), or the items will also be
     *     recursively removed (recursive=yes).
     * @raises IOError raised when there was an error removing the item
     *     that will likely persist on immediate retry, like lack of
     *     permissions, the target not existing, and so on.
     * @raises ResourceError raised when there is a failure that might go
     *     away on retry, like an unexpected disk input output error,
     *     a write timeout, and similar.
     */
    assert(STACK_TOP(vmthread->stack) >= 2);

    h64wchar *pathstr = NULL;
    int64_t pathlen = 0;
    valuecontent *vcpath = STACK_ENTRY(vmthread->stack, 0);
    if (vcpath->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue*)(vcpath->ptr_value))->type ==
                H64GCVALUETYPE_STRING) {
        pathstr = (
            ((h64gcvalue*)(vcpath->ptr_value))->str_val.s
        );
        pathlen = (
            ((h64gcvalue*)(vcpath->ptr_value))->str_val.len
        );
    } else if (vcpath->type == H64VALTYPE_SHORTSTR) {
        pathstr = vcpath->shortstr_value;
        pathlen = vcpath->shortstr_len;
    } else {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "path argument must be a string"
        );
    }
    int recursive = 0;
    valuecontent *vcrecursive = STACK_ENTRY(vmthread->stack, 1);
    if (vcrecursive->type == H64VALTYPE_BOOL) {
        recursive = (vcrecursive->int_value != 0);
    } else if (vcrecursive->type == H64VALTYPE_UNSPECIFIED_KWARG) {
        recursive = 0;
    } else {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "recursive argument must be a boolean"
        );
    }

    int result = 0;
    int error = 0;
    if (!recursive) {
        goto removesinglefileordir;
    } else {
        result = filesys32_RemoveFolderRecursively(
            pathstr, pathlen, &error
        );
        if (!result && error == FS32_ERR_TARGETNOTADIRECTORY) {
            removesinglefileordir:
            result = filesys32_RemoveFileOrEmptyDir(
                pathstr, pathlen, &error
            );
            if (!result) {
                if (error == FS32_ERR_DIRISBUSY) {
                    return vmexec_ReturnFuncError(
                        vmthread, H64STDERROR_RESOURCEERROR,
                        "directory is in use by process "
                        "and cannot be deleted"
                    );
                } else if (error == FS32_ERR_NONEMPTYDIRECTORY) {
                    // Oops, seems like something concurrently
                    // filled in files again.
                    return vmexec_ReturnFuncError(
                        vmthread, H64STDERROR_RESOURCEERROR,
                        "target directory is not empty"
                    );
                } else if (error == FS32_ERR_NOPERMISSION) {
                    return vmexec_ReturnFuncError(
                        vmthread, H64STDERROR_IOERROR,
                        "permission denied"
                    );
                } else if (error == FS32_ERR_NOSUCHTARGET) {
                    return vmexec_ReturnFuncError(
                        vmthread, H64STDERROR_IOERROR,
                        "no such file or directory"
                    );
                } else if (error == FS32_ERR_OUTOFFDS) {
                    return vmexec_ReturnFuncError(
                        vmthread, H64STDERROR_RESOURCEERROR,
                        "out of file descriptors"
                    );
                }
                return vmexec_ReturnFuncError(
                    vmthread, H64STDERROR_RESOURCEERROR,
                    "unexpected type of I/O error"
                );
            }
        } else if (!result) {
            if (error == FS32_ERR_DIRISBUSY) {
                return vmexec_ReturnFuncError(
                    vmthread, H64STDERROR_RESOURCEERROR,
                    "directory is in use by process "
                    "and cannot be deleted"
                );
            } else if (error == FS32_ERR_NONEMPTYDIRECTORY) {
                // Oops, seems like something concurrently
                // filled in files again.
                return vmexec_ReturnFuncError(
                    vmthread, H64STDERROR_RESOURCEERROR,
                    "encountered unexpected re-added "
                    "file"
                );
            } else if (error == FS32_ERR_NOPERMISSION) {
                return vmexec_ReturnFuncError(
                    vmthread, H64STDERROR_IOERROR,
                    "permission denied"
                );
            } else if (error == FS32_ERR_NOSUCHTARGET) {
                return vmexec_ReturnFuncError(
                    vmthread, H64STDERROR_IOERROR,
                    "no such file or directory"
                );
            } else if (error == FS32_ERR_OUTOFFDS) {
                return vmexec_ReturnFuncError(
                    vmthread, H64STDERROR_RESOURCEERROR,
                    "out of file descriptors"
                );
            }
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_RESOURCEERROR,
                "unexpected type of I/O error"
            );
        }
    }

    valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vcresult);
    valuecontent_Free(vcresult);
    vcresult->type = H64VALTYPE_NONE;
    return 1;
}


int iolib_add_tmp_dir(
        h64vmthread *vmthread
        ) {
    /**
     * Create a new empty subfolder with equivalent of @see{add_dir}'s
     * limit_to_owner=yes (inaccessible to other processes of different
     * users) at whatever place the system stores temporary files.
     * Your program is responsible for removing this directory when
     * you're done with using it.
     *
     * @func add_tmp_dir
     * @param prefix="" what sort of prefix to use for the otherwise
     *     randomized subfolder name.
     * @raises ResourceError raised when there is a failure that might go
     *     away on retry, like an unexpected disk input output error,
     *     a read timeout, and similar.
     * @return @see{string} of the system's temporary files directory.
     */
    assert(STACK_TOP(vmthread->stack) >= 1);

    h64wchar *prefixstr = NULL;
    int64_t prefixlen = 0;
    valuecontent *vcprefix = STACK_ENTRY(vmthread->stack, 0);
    if (vcprefix->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue*)(vcprefix->ptr_value))->type ==
                H64GCVALUETYPE_STRING) {
        prefixstr = (
            ((h64gcvalue*)(vcprefix->ptr_value))->str_val.s
        );
        prefixlen = (
            ((h64gcvalue*)(vcprefix->ptr_value))->str_val.len
        );
    } else if (vcprefix->type == H64VALTYPE_SHORTSTR) {
        prefixstr = vcprefix->shortstr_value;
        prefixlen = vcprefix->shortstr_len;
    } else if (vcprefix->type == H64VALTYPE_UNSPECIFIED_KWARG) {
        prefixstr = NULL;
        prefixlen = 0;
    } else {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "prefix argument must be a string"
        );
    }

    int64_t folder_path_len = 0;
    h64wchar *folder_path = NULL;
    int64_t file_path_len = 0;
    h64wchar *file_path = NULL;
    FILE *f = filesys32_TempFile(
        1, 1, prefixstr, prefixlen, NULL, 0,
        &folder_path, &folder_path_len,
        &file_path, &file_path_len
    );
    assert(!f && file_path == NULL);
    if (!folder_path) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_RESOURCEERROR,
            "unexpected I/O error creating a temporary dir"
        );
    }

    valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vcresult);
    valuecontent_Free(vcresult);
    memset(vcresult, 0, sizeof(*vcresult));
    if (!valuecontent_SetStringU32(
            vmthread, vcresult, folder_path, folder_path_len)) {
        free(folder_path);
        int innererror = 0;
        filesys32_RemoveFileOrEmptyDir(
            folder_path, folder_path_len, &innererror
        );
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OUTOFMEMORYERROR,
            "failed to allocate temporary dir name"
        );
    }
    free(folder_path);
    return 1;
}

int iolib_add_dir(
        h64vmthread *vmthread
        ) {
    /**
     * Add a new empty directory at the given target path.
     *
     * @func add_dir
     * @param path the filesystem path for the new directory to be added
     * @param recursive=no whether to recursively add the parent directories
     *     in case these don't exist yet (recursive=yes), or whether to
     *     raise an IOError if all but the last path component don't exist
     *     already (recursive=no, the default).
     * @param limit_to_owner=no whether the new directory created
     *     should have permissions set to only allow the user under which
     *     this program runs to access its contents, and also disallow
     *     any write access from groups. Changing the permissions
     *     manually after you create the directory might have already
     *     allowed a different process to change their working directory
     *     into it or write a file via group permissions, so
     *     limit_to_owner=yes is a safe alternative to prevent that.
     *     By default, other processes will be allowed to read from and change
     *     their working directory to the new directory, or write into it
     *     based on group permissions (limit_to_owner=no).
     * @raises IOError raised when there was an error adding the directory
     *     that will likely persist on immediate retry, like lack of
     *     permissions, the target already exists, and so on.
     * @raises ResourceError raised when there is a failure that might go
     *     away on retry, like an unexpected disk input output error,
     *     a write timeout, and similar.
     */
    assert(STACK_TOP(vmthread->stack) >= 3);

    h64wchar *pathstr = NULL;
    int64_t pathlen = 0;
    valuecontent *vcpath = STACK_ENTRY(vmthread->stack, 0);
    if (vcpath->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue*)(vcpath->ptr_value))->type ==
                H64GCVALUETYPE_STRING) {
        pathstr = (
            ((h64gcvalue*)(vcpath->ptr_value))->str_val.s
        );
        pathlen = (
            ((h64gcvalue*)(vcpath->ptr_value))->str_val.len
        );
    } else if (vcpath->type == H64VALTYPE_SHORTSTR) {
        pathstr = vcpath->shortstr_value;
        pathlen = vcpath->shortstr_len;
    } else {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "path argument must be a string"
        );
    }
    int recursive = 0;
    valuecontent *vcrecursive = STACK_ENTRY(vmthread->stack, 1);
    if (vcrecursive->type == H64VALTYPE_BOOL) {
        recursive = (vcrecursive->int_value != 0);
    } else if (vcrecursive->type == H64VALTYPE_UNSPECIFIED_KWARG) {
        recursive = 0;
    } else {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "recursive argument must be a boolean"
        );
    }
    int limittoowner = 0;
    valuecontent *vclimittoowner = STACK_ENTRY(vmthread->stack, 2);
    if (vclimittoowner->type == H64VALTYPE_BOOL) {
        limittoowner = (vclimittoowner->int_value != 0);
    } else if (vclimittoowner->type == H64VALTYPE_UNSPECIFIED_KWARG) {
        limittoowner = 0;
    } else {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "limit_to_owner argument must be a boolean"
        );
    }

    int result = 0;
    if (recursive) {
        result = filesys32_CreateDirectoryRecursively(
            pathstr, pathlen, limittoowner
        );
    } else {
        result = filesys32_CreateDirectory(
            pathstr, pathlen, limittoowner
        );
    }
    if (result < 0) {
        if (result == FS32_ERR_TARGETALREADYEXISTS) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "target already exists"
            );
        } else if (result == FS32_ERR_NOPERMISSION) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "permission denied"
            );
        } else if (result == FS32_ERR_PARENTSDONTEXIST) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "parent directory missing"
            );
        } else if (result == FS32_ERR_OUTOFFDS) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_RESOURCEERROR,
                "out of file descriptors"
            );
        } else if (result == FS32_ERR_OUTOFMEMORY) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_OUTOFMEMORYERROR,
                "out of memory creating directory"
            );
        }
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_RESOURCEERROR,
            "unexpected type of I/O error"
        );
    }

    valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vcresult);
    valuecontent_Free(vcresult);
    memset(vcresult, 0, sizeof(*vcresult));
    return 1;
}

int iolib_RegisterFuncsAndModules(h64program *p) {
    int64_t idx;

    // io.add_tmp_dir:
    const char *io_add_tmp_dir_kw_arg_name[] = {
        "prefix"
    };
    idx = h64program_RegisterCFunction(
        p, "add_tmp_dir", &iolib_add_tmp_dir,
        NULL, 0, 1, io_add_tmp_dir_kw_arg_name,  // fileuri, args
        "io", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    // io.add_dir:
    const char *iolib_add_dir_kw_arg_name[] = {
        NULL, "recursive", "limit_to_owner"
    };
    idx = h64program_RegisterCFunction(
        p, "add_dir", &iolib_add_dir,
        NULL, 0, 3, iolib_add_dir_kw_arg_name,  // fileuri, args
        "io", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    // io.remove:
    const char *io_remove_kw_arg_name[] = {
        NULL, "recursive"
    };
    idx = h64program_RegisterCFunction(
        p, "remove", &iolib_remove,
        NULL, 0, 2, io_remove_kw_arg_name,  // fileuri, args
        "io", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    // io.get_unix_perms:
    const char *io_get_unix_perms_kw_arg_name[] = {
        NULL
    };
    idx = h64program_RegisterCFunction(
        p, "get_unix_perms", &iolib_get_unix_perms,
        NULL, 0, 1, io_get_unix_perms_kw_arg_name,  // fileuri, args
        "io", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    // io.set_unix_perms:
    const char *io_set_unix_perms_kw_arg_name[] = {
        NULL, NULL
    };
    idx = h64program_RegisterCFunction(
        p, "set_unix_perms", &iolib_set_unix_perms,
        NULL, 0, 2, io_set_unix_perms_kw_arg_name,  // fileuri, args
        "io", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    // io.set_as_exec:
    const char *io_set_as_exec_kw_arg_name[] = {
        NULL, "enabled"
    };
    idx = h64program_RegisterCFunction(
        p, "set_as_exec", &iolib_set_as_exec,
        NULL, 0, 2, io_set_as_exec_kw_arg_name,  // fileuri, args
        "io", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    // io.open:
    const char *io_open_kw_arg_name[] = {
        NULL, "read", "write", "append", "binary",
        "allow_bad_encoding"
    };
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
