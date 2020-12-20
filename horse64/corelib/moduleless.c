// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

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
#include "corelib/moduleless.h"
#include "corelib/system.h"
#include "corelib/time.h"
#include "gcvalue.h"
#include "hash.h"
#include "net.h"
#include "nonlocale.h"
#include "process.h"
#include "stack.h"
#include "vmexec.h"
#include "vmlist.h"
#include "widechar.h"


int corelib_containeradd(  // $$builtin.$$containeradd
        h64vmthread *vmthread
        ) {
    assert(STACK_TOP(vmthread->stack) >= 2);

    valuecontent *vc = STACK_ENTRY(vmthread->stack, 1);
    assert(vc->type == H64VALTYPE_GCVAL);
    h64gcvalue *gcvalue = (h64gcvalue *)vc->ptr_value;
    if (gcvalue->type == H64GCVALUETYPE_LIST) {
        if (!vmlist_Add(
                gcvalue->list_values, STACK_ENTRY(vmthread->stack, 0)
                )) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_OUTOFMEMORYERROR,
                "alloc failure extending container"
            );
        }
    } else {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "cannot .add() on this container type"
        );
    }

    return 1;
}

struct _printvalue_seeninfo {
    hashset *seen;
};

static h64wchar *_corelib_printbytes(
        char *bytesref, int64_t byteslen,
        int64_t *outlen
        ) {
    int buflen = 16 + byteslen * 1.5;
    h64wchar *buf = malloc(buflen * sizeof(h64wchar));
    if (!buf)
        return NULL;
    int buffill = 2;
    buf[0] = 'b';
    buf[1] = '"';
    int64_t i = 0;
    while (i < byteslen) {
        if (buffill + 16 > buflen) {
            h64wchar *bufnew = realloc(
                buf, (buffill + 64)  * sizeof(h64wchar)
            );
            if (!bufnew) {
                free(buf);
                return NULL;
            }
            buf = bufnew;
            buflen = buffill + 64;
        }
        uint8_t byte = ((uint8_t*)bytesref)[i];
        if ((byte >= 'a' && byte <= 'z') ||
                (byte >= 'A' && byte <= 'Z') ||
                byte == ' ' || (byte >= '!' && byte <= '~' &&
                byte != '\\')) {
            buf[buffill] = byte;
            buffill++;
        } else if (byte == '"') {
            buf[buffill] = '\\';
            buf[buffill + 1] = '"';
            buffill += 2;
        } else if (byte == '\\') {
            buf[buffill] = '\\';
            buf[buffill + 1] = '\\';
            buffill += 2;
        } else if (byte == '\r') {
            buf[buffill] = '\\';
            buf[buffill + 1] = 'r';
            buffill += 2;
        } else if (byte == '\n') {
            buf[buffill] = '\\';
            buf[buffill + 1] = 'n';
            buffill += 2;
        } else {
            char hexval[5] = {0};
            h64snprintf(hexval, sizeof(hexval) - 1, "%x", (int)byte);
            buf[buffill] = '\\';
            buf[buffill + 1] = 'x';
            buffill += 2;
            if (strlen(hexval) < 2) {
                buf[buffill] = '0';
                buffill++;
            }
            unsigned int k = 0;
            while (k < strlen(hexval)) {
                buf[buffill] = hexval[k];
                buffill++;
                k++;
            }
        }
        i++;
    }
    buf[buffill] = '"';
    buffill += 1;
    *outlen = buffill;
    return buf;
}

static h64wchar *_corelib_value_to_str_do(
        h64vmthread *vmthread,
        valuecontent *c, struct _printvalue_seeninfo *sinfo,
        h64wchar *tempbuf, int tempbuflen, int currentnesting,
        int64_t *outlen
        ) {
    h64wchar *buf = tempbuf;
    uint64_t buflen = tempbuflen;
    int buffree = 0;
    if (tempbuflen < 64) {
        buf = malloc(64 * sizeof(h64wchar));
        if (!buf)
            return NULL;
        buffree = 1;
        buflen = 64;
    }
    switch (c->type) {
        case H64VALTYPE_GCVAL: {
            h64gcvalue *gcval = c->ptr_value;
            switch (gcval->type) {
                case H64GCVALUETYPE_STRING: {
                    if (buflen < (gcval->str_val.len + 1)) {
                        h64wchar *newbuf = malloc(
                            (gcval->str_val.len + 1) * sizeof(h64wchar)
                        );
                        if (newbuf) {
                            if (buffree)
                                free(buf);
                            buf = newbuf;
                            buflen = (
                                (gcval->str_val.len + 1)
                            );
                            buffree = 1;
                        } else {
                            if (buffree)
                                free(buf);
                            return NULL;
                        }
                    }
                    memcpy(
                        buf, gcval->str_val.s,
                        gcval->str_val.len * sizeof(h64wchar)
                    );
                    *outlen = gcval->str_val.len;
                    return buf;
                }
                case H64GCVALUETYPE_BYTES: {
                    if (buffree)
                        free(buf);
                    return _corelib_printbytes(
                        gcval->bytes_val.s, gcval->bytes_val.len, outlen
                    );
                }
                case H64GCVALUETYPE_LIST: {
                    if (!sinfo->seen) {
                        sinfo->seen = hashset_New(64);
                    }

                    if (currentnesting >= 10 ||
                            hashset_Contains(
                            sinfo->seen, &gcval->list_values,
                            sizeof(gcval->list_values)
                            )) {
                        buf[0] = '.';
                        buf[1] = '.';
                        buf[2] = '.';
                        *outlen = 3;
                        return buf;
                    }
                    if (!hashset_Add(
                            sinfo->seen, &gcval->list_values,
                            sizeof(gcval->list_values)
                            )) {
                        if (buffree)
                            free(buf);
                        return NULL;
                    }
                    int buffill = 1;
                    buf[0] = '[';
                    int64_t entry_offset = -1;
                    int64_t total_entry_count = (
                        gcval->list_values->list_total_entry_count
                    );
                    listblock *block = gcval->list_values->first_block;
                    while (block) {
                        int64_t k = 0;
                        while (k < block->entry_count) {
                            entry_offset++;
                            assert(entry_offset < total_entry_count);
                            int64_t innerlen = 0;
                            h64wchar *innerval = (
                                _corelib_value_to_str_do(
                                    vmthread,
                                    &block->entry_values[k], sinfo,
                                    NULL, 0, currentnesting + 1,
                                    &innerlen
                                )
                            );
                            if (!innerval) {
                                if (buffree)
                                    free(buf);
                                return 0;
                            }
                            if (innerlen + 10 + buffill > (int64_t)buflen) {
                                h64wchar *newbuf = malloc(
                                    (innerlen + 512 + buffill) *
                                    sizeof(h64wchar)
                                );
                                if (!newbuf) {
                                    if (buffree)
                                        free(buf);
                                    return NULL;
                                }
                                memcpy(
                                    newbuf, buf, buffill * sizeof(h64wchar)
                                );
                                if (buffree)
                                    free(buf);
                                buf = newbuf;
                                buffree = 1;
                                buflen = (innerlen + 512 + buffill);
                            }
                            memcpy(
                                buf + buffill, innerval,
                                innerlen * sizeof(h64wchar)
                            );
                            buffill += innerlen;
                            if (likely(entry_offset + 1 <
                                    total_entry_count)) {
                                buf[buffill] = ',';
                                buf[buffill + 1] = ' ';
                                buffill += 2;
                            }
                            k++;
                        }
                        block = block->next_block;
                    }
                    buf[buffill] = ']';
                    buffill++;
                    *outlen = buffill;
                    return buf;
                }
                default: {
                    char s[] = "<unhandled gc refvalue type>";
                    int k = 0;
                    while (k < (int)strlen(s)) {
                        buf[k] = s[k];
                        k++;
                    }
                    *outlen = strlen(s);
                    return buf;
                }
            }
            break;
        }
        case H64VALTYPE_SHORTSTR: {
            assert(buflen >= 25);
            assert(c->shortstr_len >= 0 &&
                   c->shortstr_len < 5);
            memcpy(buf, c->shortstr_value, c->shortstr_len);
            *outlen = c->shortstr_len;
            return buf;
        }
        case H64VALTYPE_INT64: {
            char nobuf[64];
            h64snprintf(nobuf, 64, "%" PRId64, c->int_value);
            nobuf[63] = 0;
            assert((int)strlen(nobuf) <= (int)tempbuflen);
            int k = 0;
            while (k < (int)strlen(nobuf)) {
                buf[k] = nobuf[k];
                k++;
            }
            *outlen = strlen(nobuf);
            return buf;
        }
        case H64VALTYPE_FLOAT64: {
            char nobuf[64];
            h64snprintf(nobuf, 64, "%f", c->float_value);
            nobuf[63] = 0;
            // Cut off trailing zeroes if we got a fractional part:
            int gotdot = 0;
            size_t len = strlen(nobuf);
            size_t i = 0;
            while (i < len) {
                if (nobuf[i] == '.') {
                    gotdot = 1;
                    break;
                }
                i++;
            }
            while (gotdot && len > 0 && nobuf[len - 1] == '0') {
                nobuf[len - 1] = '\0';
                len--;
            }
            if (len > 0 && nobuf[len - 1] == '.') {
                nobuf[len - 1] = '\0';
                len--;
            }
            assert((int)strlen(nobuf) <= (int)tempbuflen);
            int k = 0;
            while (k < (int)strlen(nobuf)) {
                buf[k] = nobuf[k];
                k++;
            }
            *outlen = strlen(nobuf);
            return buf;
        }
        case H64VALTYPE_BOOL: {
            char s[12];
            if (c->int_value != 0)
                memcpy(s, "true", strlen("true") + 1);
            else
                memcpy(s, "false", strlen("false") + 1);
            int k = 0;
            while (k < (int)strlen(s)) {
                buf[k] = s[k];
                k++;
            }
            *outlen = strlen(s);
            return buf;
        }
        case H64VALTYPE_NONE: {
            char s[] = "none";
            int k = 0;
            while (k < (int)strlen(s)) {
                buf[k] = s[k];
                k++;
            }
            *outlen = strlen(s);
            return buf;
        }
        default: {
            char s[] = "<unhandled valuecontent type>";
            int k = 0;
            while (k < (int)strlen(s)) {
                buf[k] = s[k];
                k++;
            }
            *outlen = strlen(s);
            return buf;
        }
    }
    if (buffree)
        free(buf);
    return NULL;
}

h64wchar *corelib_value_to_str(
        h64vmthread *vmthread,
        valuecontent *c, h64wchar *tempbuf, int tempbuflen,
        int64_t *outlen
        ) {
    struct _printvalue_seeninfo sinfo = {0};
    h64wchar *result = _corelib_value_to_str_do(
        vmthread, c, &sinfo,
        tempbuf, tempbuflen, 0,
        outlen
    );
    if (sinfo.seen)
        hashset_Free(sinfo.seen);
    return result;
}

int corelib_print(  // $$builtin.print
        h64vmthread *vmthread
        ) {
    /**
     * Print out strings and other values to the terminal (stdout).
     *
     * @func print
     */
    assert(STACK_TOP(vmthread->stack) == 1);
    valuecontent *c = STACK_ENTRY(vmthread->stack, 0);
    assert(c != NULL);
    int64_t slen = 0;
    h64wchar *s = NULL;
    if ((s = corelib_value_to_str(vmthread, c, NULL, 0, &slen)) == NULL) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OUTOFMEMORYERROR,
            "alloc failure while printing value"
        );
    }
    char *outbuf = malloc(slen * 5 + 1);
    if (!outbuf) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OUTOFMEMORYERROR,
            "alloc failure while printing value"
        );
    }
    int64_t utf8len = 0;
    int outbufsize = slen * 5 + 1;
    int result = utf32_to_utf8(
        s, slen, outbuf, outbufsize, &utf8len, 1, 1
    );
    if (!result) {
        free(outbuf);
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OUTOFMEMORYERROR,
            "alloc failure while printing value"
        );
    }
    assert(utf8len <= outbufsize);
    outbuf[outbufsize - 1] = '\0';
    if (utf8len < outbufsize)
        outbuf[utf8len] = '\0';
    h64printf("%s", outbuf);
    free(outbuf);
    h64printf("\n");
    return 1;
}

int corelib_RegisterFuncsAndModules(h64program *p) {
    int64_t idx;

    // Error types:
    if (!corelib_RegisterErrorClasses(p))
        return 0;

    // 'io' module:
    if (!iolib_RegisterFuncsAndModules(p))
        return 0;

    // 'process' module:
    if (!processlib_RegisterFuncsAndModules(p))
        return 0;

    // 'net' module:
    if (!netlib_RegisterFuncsAndModules(p))
        return 0;

    // 'time' module:
    if (!timelib_RegisterFuncsAndModules(p))
        return 0;

    // 'system' module:
    if (!systemlib_RegisterFuncsAndModules(p))
        return 0;

    // 'print' function:
    idx = h64program_RegisterCFunction(
        p, "print", &corelib_print,
        NULL, 1, NULL, NULL, NULL, 1, -1
    );
    if (idx < 0)
        return 0;
    p->print_func_index = idx;

    // '$$container.add' function:
    idx = h64program_RegisterCFunction(
        p, "$$containeradd", &corelib_containeradd,
        NULL, 1, NULL, NULL, NULL, 1, -1
    );
    if (idx < 0)
        return 0;
    p->func[idx].input_stack_size++;  // for 'self'
    p->containeradd_func_index = idx;

    return 1;
}
