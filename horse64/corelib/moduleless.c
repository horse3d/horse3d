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
#include "corelib/moduleless.h"
#include "gcvalue.h"
#include "hash.h"
#include "stack.h"
#include "unicode.h"
#include "vmexec.h"
#include "vmlist.h"


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
                        buffree = 1;
                        if (newbuf) {
                            if (buffree)
                                free(buf);
                            buf = newbuf;
                            buflen = gcval->str_val.len * 4 + 1;
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
                    printf("%s", buf);
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
                        printf("...");
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
                    printf("[");
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
                    printf("]");
                    break;
                }
                default:
                    printf("<unhandled refvalue type=%d>",
                           (int)gcval->type);
            }
            break;
        }
        case H64VALTYPE_SHORTSTR: {
            assert(buflen >= 25);
            assert(c->shortstr_len >= 0 &&
                   c->shortstr_len < 5);
            unicodechar shortstr_value[
                VALUECONTENT_SHORTSTRLEN + 1
            ];
            memcpy(&shortstr_value, c->shortstr_value,
                   VALUECONTENT_SHORTSTRLEN + 1);
            int64_t outlen = 0;
            int result = utf32_to_utf8(
                shortstr_value, c->shortstr_len,
                buf, 25, &outlen, 1
            );
            assert(result != 0 && outlen > 0 && outlen + 1 < 25);
            buf[outlen] = '\0';
            printf("%s", buf);
            break;
        }
        case H64VALTYPE_INT64: {
            printf("%" PRId64, c->int_value);
            break;
        }
        default: {
            printf("<unhandled valuecontent type=%d>", (int)c->type);
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
    assert(STACK_TOP(vmthread->stack) == 1);
    assert(STACK_ENTRY(vmthread->stack, 0)->type == H64VALTYPE_GCVAL &&
        ((h64gcvalue*)STACK_ENTRY(vmthread->stack, 0)->ptr_value)->type ==
        H64GCVALUETYPE_LIST
    );
    genericlist *l = (
        ((h64gcvalue*)STACK_ENTRY(vmthread->stack, 0)->ptr_value)->
            list_values
    );
    char *buf = alloca(256);
    uint64_t buflen = 256;
    int buffree = 0;
    int64_t i = 0;
    while (i < vmlist_Count(l)) {
        if (i > 0)
            printf(" ");
        valuecontent *c = vmlist_Get(l, i + 1);
        assert(c != NULL);
        struct _printvalue_seeninfo sinfo = {0};
        if (!_corelib_printvalue(vmthread, c, &sinfo))
            return 0;  // error raised
        if (sinfo.seen)
            hashset_Free(sinfo.seen);
        i++;
    }
    printf("\n");
    return 1;
}

int corelib_RegisterFuncs(h64program *p) {
    int64_t idx;
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
    p->containeradd_func_index = idx;
    return 1;
}
