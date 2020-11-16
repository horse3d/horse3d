// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "corelib/system.h"
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

    valuecontent *vc = STACK_ENTRY(vmthread->stack, 0);
    assert(vc->type == H64VALTYPE_GCVAL);
    h64gcvalue *gcvalue = (h64gcvalue *)vc->ptr_value;
    if (gcvalue->type == H64GCVALUETYPE_LIST) {
        if (!vmlist_Add(
                gcvalue->list_values, STACK_ENTRY(vmthread->stack, 1)
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

static void _corelib_printbytes(
        char *bytesref, int64_t byteslen
        ) {
    h64printf("b\"");
    int64_t i = 0;
    while (i < byteslen) {
        uint8_t byte = ((uint8_t*)bytesref)[i];
        if ((byte >= 'a' && byte <= 'z') ||
                (byte >= 'A' && byte <= 'Z') ||
                byte == ' ' || (byte >= '!' && byte <= '~' &&
                byte != '\\')) {
            h64printf("%c", byte);
        } else if (byte == '"') {
            h64printf("\\\"");
        } else if (byte == '\\') {
            h64printf("\\\\");
        } else if (byte == '\r') {
            h64printf("\\r");
        } else if (byte == '\n') {
            h64printf("\\n");
        } else {
            char hexval[5] = {0};
            h64snprintf(hexval, sizeof(hexval) - 1, "%x", (int)byte);
            h64printf("\\x");
            if (strlen(hexval) < 2)
                h64printf("0");
            h64printf("%s", hexval);
        }
        i++;
    }
    h64printf("\"");
}

static int _corelib_printvalue(
        h64vmthread *vmthread,
        valuecontent *c, struct _printvalue_seeninfo *sinfo
        ) {
    char *buf = alloca(256);
    uint64_t buflen = 256;
    int buffree = 0;
    switch (c->type) {
        case H64VALTYPE_GCVAL: {
            h64gcvalue *gcval = c->ptr_value;
            switch (gcval->type) {
                case H64GCVALUETYPE_STRING: {
                    int allocfail = 0;
                    if (buflen < gcval->str_val.len * 4 + 1) {
                        char *newbuf = malloc(
                            gcval->str_val.len * 4 + 1
                        );
                        if (newbuf) {
                            if (buffree)
                                free(buf);
                            buf = newbuf;
                            buflen = gcval->str_val.len * 4 + 1;
                            buffree = 1;
                        } else {
                            allocfail = 1;
                        }
                    }
                    int64_t outlen = 0;
                    int result = utf32_to_utf8(
                        gcval->str_val.s, gcval->str_val.len,
                        buf, buflen, &outlen, 1
                    );
                    if (!result) {
                        assert(allocfail);
                        if (buffree)
                            free(buf);
                        return vmexec_ReturnFuncError(
                            vmthread, H64STDERROR_OUTOFMEMORYERROR,
                            "alloc failure printing string"
                        );
                    }
                    if (outlen >= (int64_t)buflen)
                        outlen = buflen - 1;
                    buf[outlen] = '\0';
                    h64printf("%s", buf);
                    break;
                }
                case H64GCVALUETYPE_BYTES: {
                    _corelib_printbytes(
                        gcval->bytes_val.s, gcval->bytes_val.len
                    );
                    break;
                }
                case H64GCVALUETYPE_LIST: {
                    if (!sinfo->seen) {
                        sinfo->seen = hashset_New(64);
                    }

                    if (hashset_Contains(
                            sinfo->seen, &gcval->list_values,
                            sizeof(gcval->list_values)
                            )) {
                        h64printf("...");
                        break;
                    }
                    if (!hashset_Add(
                            sinfo->seen, &gcval->list_values,
                            sizeof(gcval->list_values)
                            )) {
                        if (buffree)
                            free(buf);
                        return vmexec_ReturnFuncError(
                            vmthread, H64STDERROR_OUTOFMEMORYERROR,
                            "alloc failure printing list"
                        );
                    }
                    h64printf("[");
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
                            if (!_corelib_printvalue(
                                    vmthread,
                                    &block->entry_values[k], sinfo
                                    )) {
                                if (buffree)
                                    free(buf);
                                return 0;
                            }
                            if (likely(entry_offset + 1 <
                                    total_entry_count)) {
                                printf(", ");
                            }
                            k++;
                        }
                        block = block->next_block;
                    }
                    h64printf("]");
                    break;
                }
                default:
                    h64printf(
                        "<unhandled gc refvalue type=%d>",
                        (int)gcval->type
                    );
            }
            break;
        }
        case H64VALTYPE_SHORTSTR: {
            assert(buflen >= 25);
            assert(c->shortstr_len >= 0 &&
                   c->shortstr_len < 5);
            h64wchar shortstr_value[
                VALUECONTENT_SHORTSTRLEN + 1
            ];
            memcpy(&shortstr_value, c->shortstr_value,
                   sizeof(h64wchar) * (VALUECONTENT_SHORTSTRLEN + 1));
            int64_t outlen = 0;
            int result = utf32_to_utf8(
                shortstr_value, c->shortstr_len,
                buf, 25, &outlen, 1
            );
            assert(result != 0 && outlen > 0 && outlen + 1 < 25);
            buf[outlen] = '\0';
            h64printf("%s", buf);
            break;
        }
        case H64VALTYPE_INT64: {
            h64printf("%" PRId64, c->int_value);
            break;
        }
        case H64VALTYPE_FLOAT64: {
            char buf[256];
            snprintf(buf, sizeof(buf) - 1, "%f", c->float_value);
            // Cut off trailing zeroes if we got a fractional part:
            int gotdot = 0;
            size_t len = strlen(buf);
            size_t i = 0;
            while (i < len) {
                if (buf[i] == '.') {
                    gotdot = 1;
                    break;
                }
                i++;
            }
            while (gotdot && len > 0 && buf[len - 1] == '0') {
                buf[len - 1] = '\0';
                len--;
            }
            if (len > 0 && buf[len - 1] == '.') {
                buf[len - 1] = '\0';
                len--;
            }
            h64printf("%s", buf);
            break;
        }
        case H64VALTYPE_BOOL: {
            h64printf("%s", (c->int_value != 0 ? "true" : "false"));
            break;
        }
        default: {
            h64printf("<unhandled valuecontent type=%d>", (int)c->type);
            break;
        }
    }
    if (buffree)
        free(buf);
    return 1;
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
    struct _printvalue_seeninfo sinfo = {0};
    if (!_corelib_printvalue(vmthread, c, &sinfo))
        return 0;  // error raised
    if (sinfo.seen)
        hashset_Free(sinfo.seen);
    printf("\n");
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
