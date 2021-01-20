// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#define _FILE_OFFSET_BITS 64
#define __USE_LARGEFILE64
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#if defined(_WIN32) || defined(_WIN64)
#define fseek64 _fseeki64
#define ftell64 _ftelli64
#else
#define fseek64 fseeko64
#define ftell64 ftello64
#endif

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

int iolib_open(
        h64vmthread *vmthread
        ) {
    /**
     * Open a file for reading or writing.
     *
     * @func open
     * @param path the path of the file to be opened
     * @param read=true whether the file should be opened for @see(
              reading|io.file.read).
     * @param write=false whether the file should be opened for @see(
              writing|io.file.write).
     * @param append=io.APPEND_DEFAULT whether if opening writing, the
              writing should always be appended to the end.
              For opening a file for reading only, this option is ignored.

              **append=true:** the file that is opened for writing will
              keep its contents, and reading will occur from the start.
              However, any write will be appended to its end, even when
              @see{seek|io.file.seek}ing to another position for reading.

              **append=false:** the file that is opened for writing will
              be cleared out on open, and both reading and writing will
              occur from the start, or wherever you @see{seek|io.file.seek}
              to.

              **append=io.APPEND_DEFAULT:** if the file is opened just
              for writing, appending will be disabled. if the file was
              opeend for both writing and reading, appending will be
              enabled. See above for the respective result of that.
     * @param binary=false whether the file should be read as unchanged
              binary which will return @see(bytes) when reading, or
              otherwise it will be decoded from utf-8 and return a
              @see(string). When decoding to a string, invalid bytes
              will be replaced by code point U+FFFD which is lossy.
     * @param allow_bad_encoding=false when reading in non-binary mode,
              this setting determines whether to throw an EncodingError
              when reading invalid encoding (when this setting is false,
              the default), or whether garbage will be silently surrogate
              escaped and retained (when this setting is set to true).
     * @returns a @see{file object|io.file}
     * @raises IOERROR raised when there is a failure that is NOT expected
     *    to go away with retrying, like wrong file type, permission errors,
     *    full disk, etc.
     * @raises OSError raised when there is unexpected resource
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

    uint8_t flags = 0;
    int mode_read = 1;
    int mode_write = 0;
    int mode_append = 2;
    {
        valuecontent *vcarg = STACK_ENTRY(vmthread->stack, 1);
        if (vcarg->type != H64VALTYPE_BOOL &&
                vcarg->type != H64VALTYPE_UNSPECIFIED_KWARG) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_TYPEERROR,
                "read option must be a boolean"
            );
        }
        if (vcarg->type == H64VALTYPE_UNSPECIFIED_KWARG)
            mode_read = 1;
        else
            mode_read = (vcarg->int_value != 0);
    }
    {
        valuecontent *vcarg = STACK_ENTRY(vmthread->stack, 2);
        if (vcarg->type != H64VALTYPE_BOOL &&
                vcarg->type != H64VALTYPE_UNSPECIFIED_KWARG) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_TYPEERROR,
                "write option must be a boolean"
            );
        }
        if (vcarg->type == H64VALTYPE_UNSPECIFIED_KWARG)
            mode_write = 0;
        else
            mode_write = (vcarg->int_value != 0);
    }
    {
        valuecontent *vcarg = STACK_ENTRY(vmthread->stack, 3);
        if (vcarg->type != H64VALTYPE_BOOL && (
                vcarg->type != H64VALTYPE_INT64 ||
                vcarg->int_value != 2) &&
                vcarg->type != H64VALTYPE_UNSPECIFIED_KWARG) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_TYPEERROR,
                "append option must be a boolean or io.APPEND_DEFAULT"
            );
        }
        if (vcarg->type == H64VALTYPE_BOOL)
            mode_append = (vcarg->int_value != 0);
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
        if (mode_append)
            memcpy(modestr, "a+b", strlen("a+b"));
        else
            memcpy(modestr, "r+b", strlen("r+b"));
        if (mode_append)
            flags |= FILEOBJ_FLAGS_APPEND;
    } else if (mode_write) {
        flags |= FILEOBJ_FLAGS_WRITE;
        if (mode_append)
            memcpy(modestr, "ab", strlen("ab"));
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
    HANDLE f2 = CreateFileW(
        (LPCWSTR)wpath,
        0 | (mode_read ? GENERIC_READ : 0)
        | (mode_write ? GENERIC_WRITE : 0),
        (mode_read ? 0 : FILE_SHARE_READ),
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
                vmthread, H64STDERROR_OSERROR,
                "too many open files"
            );
        } else if (err == ERROR_ACCESS_DENIED) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_PERMISSIONERROR,
                "permission denied"
            );
        } else if (err == ERROR_SHARING_VIOLATION) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_OSERROR,
                "file in use by other process"
            );
        } else if (err == ERROR_INVALID_NAME) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "given path name is invalid"
            );
        } else if (err == ERROR_LABEL_TOO_LONG ||
                err == ERROR_BUFFER_OVERFLOW ||
                err == ERROR_FILENAME_EXCED_RANGE
                ) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "given path name too long"
            );
        }
        char buf[256];
        snprintf(buf, sizeof(buf) - 1,
            "unhandled type of operation error: %d",
            (int)err
        );
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OSERROR,
            buf
        );
    }
    int filedescr = _open_osfhandle(
        (intptr_t)f2, _O_RDONLY
    );
    if (filedescr < 0) {
        CloseHandle(f2);
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OSERROR,
            "conversion to FILE* unexpectly failed"
        );
    }
    f2 = NULL;  // now owned by 'filedescr'
    FILE *f = _fdopen(filedescr, "rb");
    if (!f) {
        _close(filedescr);
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OSERROR,
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
    FILE *f = fopen64(
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
                vmthread, H64STDERROR_OSERROR,
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
            vmthread, H64STDERROR_OSERROR,
            buf
        );
    }
    if (freeutf8buf)
        free(utf8buf);

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
            vmthread, H64STDERROR_OSERROR,
            "fstat() failed on file for unknown reason"
        );
    }
    if (S_ISDIR(filestatinfo.st_mode)) {
        fclose(f);
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OSERROR,
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

/**
 * A file object class, returned from @see{io.open}.
 *
 * @class file
 */

int iolib_fileread(
        h64vmthread *vmthread
        ) {
    /**
     * Read from the given file.
     *
     * @funcattr file read
     * @param len=-1 amount of bytes/letters to read. When
     *    the file was opened with binary=true then amount will be
     *    interpreted as bytes, otherwise with binary=false as full
     *    decoded Unicode characters. Specify -1 to read
     *    everything until the end of the file.
     * @returns the data read, which is a @see{bytes} value when the
     *    file was opened with binary=false, otherwise a @see{string}
     *    value.
     * @raises IOERROR raised when there is a failure that is NOT expected
     *    to go away with retrying, like wrong file type, permission errors,
     *    full disk, etc.
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
            vmthread, H64STDERROR_OSERROR,
            "unknown read error"
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

    if (ferror(f)) {
        clearerr(f);
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OSERROR,
            "unknown read error"
        );
    }
    if ((cdata->flags & FILEOBJ_FLAGS_LASTWASWRITE) != 0) {
        cdata->flags &= ~((uint8_t)FILEOBJ_FLAGS_LASTWASWRITE);
        fflush(f);
        fseek(f, 0, SEEK_CUR);
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
                    return vmexec_ReturnFuncError(
                        vmthread, H64STDERROR_OUTOFMEMORYERROR,
                        "out of memory"
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
                        return vmexec_ReturnFuncError(
                            vmthread, H64STDERROR_OSERROR,
                            "unknown read error"
                        );
                    }
                    cdata->flags |= FILEOBJ_FLAGS_CACHEDUNSENTERROR;
                    break;
                }
                continue;
            }
            readbuffill += _didread;
            if (readbinary) {
                if (_didread >= 0)
                    readcount += _didread;
            }
        }
        #ifndef NDEBUG
        if (!readbinary) {
            assert(readcount == 0 && amount < 0);
        }
        #endif
    } else {
        assert(0);  // FIXME: implement this code path
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
            vmthread, H64STDERROR_OSERROR,
            "file has encountered read/write error"
        );
    } else if ((cdata->flags & FILEOBJ_FLAGS_BINARY) != 0) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "cannot seek on non-binary mode file"
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
            vmthread, H64STDERROR_OSERROR,
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
     * Get the seek offset in the given file. When binary mode is off,
     * even though seeking will be unavailable this function will still work
     * and return the characters read so far.
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
            vmthread, H64STDERROR_OSERROR,
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
            vmthread, H64STDERROR_OSERROR,
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
        NULL, 6, io_open_kw_arg_name,  // fileuri, args
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
    const char *io_fileread_kw_arg_name[] = {"len"};
    idx = h64program_RegisterCFunction(
        p, "read", &iolib_fileread,
        NULL, 1, io_fileread_kw_arg_name,  // fileuri, args
        "io", "core.horse64.org", 1, p->_io_file_class_idx
    );
    if (idx < 0)
        return 0;

    // file.offset method:
    const char *io_fileoffset_kw_arg_name[] = {};
    idx = h64program_RegisterCFunction(
        p, "offset", &iolib_fileoffset,
        NULL, 0, io_fileoffset_kw_arg_name,  // fileuri, args
        "io", "core.horse64.org", 1, p->_io_file_class_idx
    );
    if (idx < 0)
        return 0;

    // file.seek method:
    const char *io_fileseek_kw_arg_name[] = {NULL};
    idx = h64program_RegisterCFunction(
        p, "seek", &iolib_fileseek,
        NULL, 1, io_fileseek_kw_arg_name,  // fileuri, args
        "io", "core.horse64.org", 1, p->_io_file_class_idx
    );
    if (idx < 0)
        return 0;

    // file.close method:
    idx = h64program_RegisterCFunction(
        p, "close", &iolib_fileclose,
        NULL, 0, NULL,  // fileuri, args
        "io", "core.horse64.org", 1, p->_io_file_class_idx
    );
    if (idx < 0)
        return 0;

    return 1;
}
