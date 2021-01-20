// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>

#include "bytecode.h"
#include "corelib/errors.h"
#include "corelib/moduleless.h"
#include "corelib/moduleless_strings.h"
#include "debugsymbols.h"
#include "gcvalue.h"
#include "stack.h"
#include "valuecontentstruct.h"
#include "vmexec.h"
#include "widechar.h"


int corelib_RegisterStringsFunc(
            h64program *p, const char *name,
            funcid_t funcidx
            ) {
    char **new_func_name = realloc(
        p->string_indexes.func_name,
        sizeof(*new_func_name) * (p->string_indexes.func_count + 1)
    );
    if (!new_func_name)
        return 0;
    p->string_indexes.func_name = new_func_name;
    int64_t *new_name_idx = realloc(
        p->string_indexes.func_name_idx,
        sizeof(*new_name_idx) * (p->string_indexes.func_count + 1)
    );
    if (!new_name_idx)
        return 0;
    p->string_indexes.func_name_idx = new_name_idx;
    funcid_t *new_func_idx = realloc(
        p->string_indexes.func_idx,
        sizeof(*new_func_idx) * (p->string_indexes.func_count + 1)
    );
    if (!new_func_idx)
        return 0;
    p->string_indexes.func_idx = new_func_idx;
    p->string_indexes.func_name[
        p->string_indexes.func_count
    ] = strdup(name);
    if (!p->string_indexes.func_name[
            p->string_indexes.func_count])
        return 0;
    p->string_indexes.func_idx[
        p->string_indexes.func_count
    ] = funcidx;
    p->string_indexes.func_name_idx[
        p->string_indexes.func_count
    ] = h64debugsymbols_AttributeNameToAttributeNameId(
        p->symbols, name, 1, 1
    );
    if (p->string_indexes.func_name_idx[
            p->string_indexes.func_count
            ] < 0) {
        free(p->string_indexes.func_name[
             p->string_indexes.func_count]);
        return 0;
    }
    p->string_indexes.func_count++;
    return 1;
}

static int _contains_or_find(
        h64vmthread *vmthread, int iscontains
        ) {
    const char *funcname = (
        iscontains ? "contains" : "find"
    );
    valuecontent *vc = STACK_ENTRY(vmthread->stack, 1);
    if (vc->type == H64VALTYPE_SHORTSTR ||
            ((h64gcvalue *)vc->ptr_value)->type ==
                H64GCVALUETYPE_STRING) {
        // U32 code path:

        // Get string we work on:
        h64wchar *s = NULL; int64_t slen = 0;
        if (vc->type == H64VALTYPE_GCVAL) {
            h64gcvalue *gcvalue = (h64gcvalue *)vc->ptr_value;
            s = gcvalue->str_val.s;
            slen = gcvalue->str_val.len;
        } else {
            s = vc->shortstr_value;
            slen = vc->shortstr_len;
        }

        // Get parameter which must also be string:
        h64wchar *params = NULL; int64_t paramlen = 0;
        valuecontent *vparam = STACK_ENTRY(vmthread->stack, 0);
        if (vparam->type == H64VALTYPE_SHORTSTR) {
            params = vparam->shortstr_value;
            paramlen = vparam->shortstr_len;
        } else if (vparam->type == H64VALTYPE_GCVAL &&
                ((h64gcvalue *)vparam->ptr_value)->type ==
                    H64GCVALUETYPE_STRING) {
            params = ((h64gcvalue *)vparam->ptr_value)->str_val.s;
            paramlen = (
                ((h64gcvalue *)vparam->ptr_value)->str_val.len
            );
        } else {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_TYPEERROR,
                ".%s() on strings needs a string parameter",
                funcname
            );
        }

        // Return result:
        if (likely(paramlen) > 0) {
            int64_t found_at = -1;
            int64_t charidx = 0;
            int64_t i = 0;
            while (i < slen) {
                charidx++;
                int charlen = utf32_letter_len(s + i, slen - i);
                assert(charlen > 0);
                if ((unsigned)(paramlen) > (unsigned)(slen - i))
                    break;
                if (memcmp(s + i, params,
                        paramlen * sizeof(*params)
                        ) == 0) {
                    found_at = charidx;
                    break;
                }
                i += charlen;
            }
            if (found_at >= 0) {
                valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
                DELREF_NONHEAP(vcresult);
                valuecontent_Free(vcresult);
                memset(vcresult, 0, sizeof(*vcresult));
                if (iscontains) {
                    vcresult->type = H64VALTYPE_BOOL;
                    vcresult->int_value = 1;
                } else {
                    vcresult->type = H64VALTYPE_INT64;
                    vcresult->int_value = found_at;
                }
                return 1;
            }
        }
        valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
        DELREF_NONHEAP(vcresult);
        valuecontent_Free(vcresult);
        memset(vcresult, 0, sizeof(*vcresult));
        if (iscontains) {
            vcresult->type = H64VALTYPE_BOOL;
            vcresult->int_value = 0;
        } else {
            vcresult->type = H64VALTYPE_INT64;
            vcresult->int_value = -1;
        }
        return 1;
    } else {
        // Bytes code path
        char *s = NULL; int64_t slen = 0;
        if (vc->type == H64VALTYPE_GCVAL) {
            h64gcvalue *gcvalue = (h64gcvalue *)vc->ptr_value;
            s = gcvalue->bytes_val.s;
            slen = gcvalue->bytes_val.len;
        } else {
            s = vc->shortbytes_value;
            slen = vc->shortbytes_len;
        }

        // Get parameter which must also be bytes:
        char *params = NULL; int64_t paramlen = 0;
        valuecontent *vparam = STACK_ENTRY(vmthread->stack, 0);
        if (vparam->type == H64VALTYPE_SHORTBYTES) {
            params = vparam->shortbytes_value;
            paramlen = vparam->shortbytes_len;
        } else if (vparam->type == H64VALTYPE_GCVAL &&
                ((h64gcvalue *)vparam->ptr_value)->type ==
                    H64GCVALUETYPE_BYTES) {
            params = ((h64gcvalue *)vparam->ptr_value)->bytes_val.s;
            paramlen = ((h64gcvalue *)vparam->ptr_value)->bytes_val.len;
        } else {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_TYPEERROR,
                ".%s() on bytes needs a bytes parameter"
            );
        }

        // Return result:
        if (likely(paramlen) > 0) {
            int64_t found_at = -1;
            int64_t i = 0;
            while (i < slen) {
                if ((unsigned)(paramlen) >
                    (unsigned)(slen - i))
                    break;
                if (memcmp(s + i, params, paramlen
                        ) == 0) {
                    found_at = i;
                    break;
                }
                i++;
            }
            if (found_at >= 0) {
                valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
                DELREF_NONHEAP(vcresult);
                valuecontent_Free(vcresult);
                memset(vcresult, 0, sizeof(*vcresult));
                if (iscontains) {
                    vcresult->type = H64VALTYPE_BOOL;
                    vcresult->int_value = 1;
                } else {
                    vcresult->type = H64VALTYPE_INT64;
                    vcresult->int_value = found_at;
                }
                return 1;
            }
        }
        valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
        DELREF_NONHEAP(vcresult);
        valuecontent_Free(vcresult);
        memset(vcresult, 0, sizeof(*vcresult));
        if (iscontains) {
            vcresult->type = H64VALTYPE_BOOL;
            vcresult->int_value = 0;
        } else {
            vcresult->type = H64VALTYPE_INT64;
            vcresult->int_value = -1;
        }
        return 1;
    }
}

int corelib_stringcontains(  // $$builtin.$$string_contains
        h64vmthread *vmthread
        ) {
    assert(STACK_TOP(vmthread->stack) >= 2);

    valuecontent *vc = STACK_ENTRY(vmthread->stack, 1);
    assert(
        (vc->type == H64VALTYPE_GCVAL &&
         ((h64gcvalue *)vc->ptr_value)->type == H64GCVALUETYPE_STRING) ||
        vc->type == H64VALTYPE_SHORTSTR ||
        (vc->type == H64VALTYPE_GCVAL &&
         ((h64gcvalue *)vc->ptr_value)->type == H64GCVALUETYPE_BYTES) ||
        vc->type == H64VALTYPE_SHORTBYTES
    );
    return _contains_or_find(vmthread, 1);
}

int corelib_stringfind(  // $$builtin.$$string_find
        h64vmthread *vmthread
        ) {
    assert(STACK_TOP(vmthread->stack) >= 2);

    valuecontent *vc = STACK_ENTRY(vmthread->stack, 1);
    assert(
        (vc->type == H64VALTYPE_GCVAL &&
         ((h64gcvalue *)vc->ptr_value)->type == H64GCVALUETYPE_STRING) ||
        vc->type == H64VALTYPE_SHORTSTR ||
        (vc->type == H64VALTYPE_GCVAL &&
         ((h64gcvalue *)vc->ptr_value)->type == H64GCVALUETYPE_BYTES) ||
        vc->type == H64VALTYPE_SHORTBYTES
    );
    return _contains_or_find(vmthread, 0);
}

int corelib_stringlower(  // $$builtin.$$string_lower
        h64vmthread *vmthread
        ) {
    assert(STACK_TOP(vmthread->stack) == 1);

    valuecontent *vc = STACK_ENTRY(vmthread->stack, 0);
    assert(
        (vc->type == H64VALTYPE_GCVAL &&
         ((h64gcvalue *)vc->ptr_value)->type == H64GCVALUETYPE_STRING) ||
        vc->type == H64VALTYPE_SHORTSTR
    );

    h64wchar *results = NULL;
    h64wchar *s = NULL;
    int64_t slen = 0;

    if (vc->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue *)vc->ptr_value)->type == H64GCVALUETYPE_STRING) {
        s = ((h64gcvalue *)vc->ptr_value)->str_val.s;
        slen = ((h64gcvalue *)vc->ptr_value)->str_val.len;
    } else {
        assert(vc->type == H64VALTYPE_SHORTSTR);
        s = vc->shortstr_value;
        slen = vc->shortstr_len;
    }
    results = malloc(sizeof(*results) * slen);
    if (!results) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OUTOFMEMORYERROR,
            "out of memory allocating lower buffer"
        );
    }
    memcpy(results, s, sizeof(*s) * slen);
    utf32_tolower(results, slen);

    valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vcresult);
    valuecontent_Free(vcresult);
    memset(vcresult, 0, sizeof(*vcresult));
    if (!valuecontent_SetStringU32(
            vmthread, vcresult, results, slen)) {
        free(results);
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OUTOFMEMORYERROR,
            "out of memory allocating lower result"
        );
    }
    free(results);
    return 1;
}

int corelib_stringupper(  // $$builtin.$$string_upper
        h64vmthread *vmthread
        ) {
    assert(STACK_TOP(vmthread->stack) == 1);

    valuecontent *vc = STACK_ENTRY(vmthread->stack, 0);
    assert(
        (vc->type == H64VALTYPE_GCVAL &&
         ((h64gcvalue *)vc->ptr_value)->type == H64GCVALUETYPE_STRING) ||
        vc->type == H64VALTYPE_SHORTSTR
    );

    h64wchar *results = NULL;
    h64wchar *s = NULL;
    int64_t slen = 0;

    if (vc->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue *)vc->ptr_value)->type == H64GCVALUETYPE_STRING) {
        s = ((h64gcvalue *)vc->ptr_value)->str_val.s;
        slen = ((h64gcvalue *)vc->ptr_value)->str_val.len;
    } else {
        assert(vc->type == H64VALTYPE_SHORTSTR);
        s = vc->shortstr_value;
        slen = vc->shortstr_len;
    }
    results = malloc(sizeof(*results) * slen);
    if (!results) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OUTOFMEMORYERROR,
            "out of memory allocating upper buffer"
        );
    }
    memcpy(results, s, sizeof(*s) * slen);
    utf32_toupper(results, slen);

    valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vcresult);
    valuecontent_Free(vcresult);
    memset(vcresult, 0, sizeof(*vcresult));
    if (!valuecontent_SetStringU32(
            vmthread, vcresult, results, slen)) {
        free(results);
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OUTOFMEMORYERROR,
            "out of memory allocating upper result"
        );
    }
    free(results);
    return 1;
}

int corelib_stringtrim(  // $$builtin.$$string_trim
        h64vmthread *vmthread
        ) {
    assert(STACK_TOP(vmthread->stack) == 1);

    valuecontent *vc = STACK_ENTRY(vmthread->stack, 0);
    assert(
        (vc->type == H64VALTYPE_GCVAL &&
         ((h64gcvalue *)vc->ptr_value)->type == H64GCVALUETYPE_STRING) ||
        vc->type == H64VALTYPE_SHORTSTR ||
        (vc->type == H64VALTYPE_GCVAL &&
         ((h64gcvalue *)vc->ptr_value)->type == H64GCVALUETYPE_BYTES) ||
        vc->type == H64VALTYPE_SHORTBYTES
    );

    if ((vc->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue *)vc->ptr_value)->type == H64GCVALUETYPE_BYTES) ||
            vc->type == H64VALTYPE_SHORTBYTES) {
        // Bytes trim():
        char *trimmed = NULL;
        int64_t trimmedlen = 0;
        char *s = NULL;
        int64_t slen = 0;

        if (vc->type == H64VALTYPE_GCVAL &&
                ((h64gcvalue *)vc->ptr_value)->type == H64GCVALUETYPE_BYTES) {
            s = ((h64gcvalue *)vc->ptr_value)->bytes_val.s;
            slen = ((h64gcvalue *)vc->ptr_value)->bytes_val.len;
        } else {
            assert(vc->type == H64VALTYPE_SHORTBYTES);
            s = vc->shortbytes_value;
            slen = vc->shortbytes_len;
        }
        trimmed = malloc(slen);
        if (!trimmed) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_OUTOFMEMORYERROR,
                "out of memory allocating trim buffer"
            );
        }
        int64_t skipstart = 0;
        while (skipstart < slen &&
                (s[skipstart] == ' ' || s[skipstart] == '\r' ||
                s[skipstart] == '\t' || s[skipstart] == '\n')) {
            skipstart++;
        }
        int64_t skipend = 0;
        if (skipstart < slen) {
            while (skipend < slen &&
                    (s[slen - skipend - 1] == ' ' ||
                    s[slen - skipend - 1] == '\r' ||
                    s[slen - skipend - 1] == '\t' ||
                    s[slen - skipend - 1] == '\n'))
                skipend++;
        }
        trimmedlen = slen - skipstart - skipend;
        if (trimmedlen > 0)
            memcpy(
                trimmed, s + skipstart,
                sizeof(*s) * (slen - skipstart - skipend
            ));
        valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
        DELREF_NONHEAP(vcresult);
        valuecontent_Free(vcresult);
        memset(vcresult, 0, sizeof(*vcresult));
        if (!valuecontent_SetBytesU8(
                vmthread, vcresult, (uint8_t *)trimmed, trimmedlen)) {
            free(trimmed);
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_OUTOFMEMORYERROR,
                "out of memory allocating trim result"
            );
        }
        free(trimmed);
        return 1;
    } else {
        // U32 trim():
        h64wchar *trimmed = NULL;
        int64_t trimmedlen = 0;
        h64wchar *s = NULL;
        int64_t slen = 0;

        if (vc->type == H64VALTYPE_GCVAL &&
                ((h64gcvalue *)vc->ptr_value)->type == H64GCVALUETYPE_STRING) {
            s = ((h64gcvalue *)vc->ptr_value)->str_val.s;
            slen = ((h64gcvalue *)vc->ptr_value)->str_val.len;
        } else {
            assert(vc->type == H64VALTYPE_SHORTSTR);
            s = vc->shortstr_value;
            slen = vc->shortstr_len;
        }
        trimmed = malloc(sizeof(*trimmed) * slen);
        if (!trimmed) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_OUTOFMEMORYERROR,
                "out of memory allocating trim buffer"
            );
        }
        int64_t skipstart = 0;
        while (skipstart < slen &&
                (s[skipstart] == ' ' || s[skipstart] == '\r' ||
                s[skipstart] == '\t' || s[skipstart] == '\n')) {
            skipstart++;
        }
        int64_t skipend = 0;
        if (skipstart < slen) {
            while (skipend < slen &&
                    (s[slen - skipend - 1] == ' ' ||
                    s[slen - skipend - 1] == '\r' ||
                    s[slen - skipend - 1] == '\t' ||
                    s[slen - skipend - 1] == '\n'))
                skipend++;
        }
        trimmedlen = slen - skipstart - skipend;
        if (trimmedlen > 0)
            memcpy(
                trimmed, s + skipstart,
                sizeof(*s) * (slen - skipstart - skipend
            ));
        valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
        DELREF_NONHEAP(vcresult);
        valuecontent_Free(vcresult);
        memset(vcresult, 0, sizeof(*vcresult));
        if (!valuecontent_SetStringU32(
                vmthread, vcresult, trimmed, trimmedlen)) {
            free(trimmed);
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_OUTOFMEMORYERROR,
                "out of memory allocating trim result"
            );
        }
        free(trimmed);
        return 1;
    }
}

int corelib_stringstarts(  // $$builtin.$$string_starts
        h64vmthread *vmthread
        ) {
    assert(STACK_TOP(vmthread->stack) == 1);

    valuecontent *vc = STACK_ENTRY(vmthread->stack, 0);
    assert(
        (vc->type == H64VALTYPE_GCVAL &&
         ((h64gcvalue *)vc->ptr_value)->type == H64GCVALUETYPE_STRING) ||
        vc->type == H64VALTYPE_SHORTSTR ||
        (vc->type == H64VALTYPE_GCVAL &&
         ((h64gcvalue *)vc->ptr_value)->type == H64GCVALUETYPE_BYTES) ||
        vc->type == H64VALTYPE_SHORTBYTES
    );

    if ((vc->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue *)vc->ptr_value)->type == H64GCVALUETYPE_BYTES) ||
            vc->type == H64VALTYPE_SHORTBYTES) {   // Bytes starts():
        // Get parameter which must also be bytes:
        char *params = NULL; int64_t paramlen = 0;
        valuecontent *vparam = STACK_ENTRY(vmthread->stack, 0);
        if (vparam->type == H64VALTYPE_SHORTBYTES) {
            params = vparam->shortbytes_value;
            paramlen = vparam->shortbytes_len;
        } else if (vparam->type == H64VALTYPE_GCVAL &&
                ((h64gcvalue *)vparam->ptr_value)->type ==
                    H64GCVALUETYPE_BYTES) {
            params = ((h64gcvalue *)vparam->ptr_value)->bytes_val.s;
            paramlen = ((h64gcvalue *)vparam->ptr_value)->bytes_val.len;
        } else {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_TYPEERROR,
                ".%s() on bytes needs a bytes parameter"
            );
        }

        // Get what we chack .starts() on:
        char *s = NULL;
        int64_t slen = 0;
        if (vc->type == H64VALTYPE_GCVAL &&
                ((h64gcvalue *)vc->ptr_value)->type == H64GCVALUETYPE_BYTES) {
            s = ((h64gcvalue *)vc->ptr_value)->bytes_val.s;
            slen = ((h64gcvalue *)vc->ptr_value)->bytes_val.len;
        } else {
            assert(vc->type == H64VALTYPE_SHORTBYTES);
            s = vc->shortbytes_value;
            slen = vc->shortbytes_len;
        }

        if (slen < paramlen || paramlen == 0) {
            valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
            DELREF_NONHEAP(vcresult);
            valuecontent_Free(vcresult);
            memset(vcresult, 0, sizeof(*vcresult));
            vcresult->type = H64VALTYPE_BOOL;
            vcresult->int_value = !(slen < paramlen);
            return 1;
        }
        int result = (
            memcmp(s, params, sizeof(*params) * paramlen)
        );
        valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
        DELREF_NONHEAP(vcresult);
        valuecontent_Free(vcresult);
        memset(vcresult, 0, sizeof(*vcresult));
        vcresult->type = H64VALTYPE_BOOL;
        vcresult->int_value = (result == 0);
        return 1;
    } else {  // U32 starts():
        // Get parameter which must also be str:
        h64wchar *params = NULL;
        int64_t paramlen = 0;
        valuecontent *vparam = STACK_ENTRY(vmthread->stack, 0);
        if (vparam->type == H64VALTYPE_SHORTSTR) {
            params = vparam->shortstr_value;
            paramlen = vparam->shortstr_len;
        } else if (vparam->type == H64VALTYPE_GCVAL &&
                ((h64gcvalue *)vparam->ptr_value)->type ==
                    H64GCVALUETYPE_STRING) {
            params = ((h64gcvalue *)vparam->ptr_value)->str_val.s;
            paramlen = ((h64gcvalue *)vparam->ptr_value)->str_val.len;
        } else {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_TYPEERROR,
                ".%s() on string needs a string parameter"
            );
        }

        // Get what we chack .starts() on:
        h64wchar *s = NULL;
        int64_t slen = 0;
        if (vc->type == H64VALTYPE_GCVAL &&
                ((h64gcvalue *)vc->ptr_value)->type == H64GCVALUETYPE_STRING) {
            s = ((h64gcvalue *)vc->ptr_value)->str_val.s;
            slen = ((h64gcvalue *)vc->ptr_value)->str_val.len;
        } else {
            assert(vc->type == H64VALTYPE_SHORTSTR);
            s = vc->shortstr_value;
            slen = vc->shortstr_len;
        }

        if (slen < paramlen || paramlen == 0) {
            valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
            DELREF_NONHEAP(vcresult);
            valuecontent_Free(vcresult);
            memset(vcresult, 0, sizeof(*vcresult));
            vcresult->type = H64VALTYPE_BOOL;
            vcresult->int_value = !(slen < paramlen);
            return 1;
        }
        int result = (
            memcmp(s, params, sizeof(*params) * paramlen)
        );
        valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
        DELREF_NONHEAP(vcresult);
        valuecontent_Free(vcresult);
        memset(vcresult, 0, sizeof(*vcresult));
        vcresult->type = H64VALTYPE_BOOL;
        vcresult->int_value = (result == 0);
        return 1;
    }
}

int corelib_stringends(  // $$builtin.$$string_ends
        h64vmthread *vmthread
        ) {
    assert(STACK_TOP(vmthread->stack) == 1);

    valuecontent *vc = STACK_ENTRY(vmthread->stack, 0);
    assert(
        (vc->type == H64VALTYPE_GCVAL &&
         ((h64gcvalue *)vc->ptr_value)->type == H64GCVALUETYPE_STRING) ||
        vc->type == H64VALTYPE_SHORTSTR ||
        (vc->type == H64VALTYPE_GCVAL &&
         ((h64gcvalue *)vc->ptr_value)->type == H64GCVALUETYPE_BYTES) ||
        vc->type == H64VALTYPE_SHORTBYTES
    );

    if ((vc->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue *)vc->ptr_value)->type == H64GCVALUETYPE_BYTES) ||
            vc->type == H64VALTYPE_SHORTBYTES) {   // Bytes ends():
        // Get parameter which must also be bytes:
        char *params = NULL; int64_t paramlen = 0;
        valuecontent *vparam = STACK_ENTRY(vmthread->stack, 0);
        if (vparam->type == H64VALTYPE_SHORTBYTES) {
            params = vparam->shortbytes_value;
            paramlen = vparam->shortbytes_len;
        } else if (vparam->type == H64VALTYPE_GCVAL &&
                ((h64gcvalue *)vparam->ptr_value)->type ==
                    H64GCVALUETYPE_BYTES) {
            params = ((h64gcvalue *)vparam->ptr_value)->bytes_val.s;
            paramlen = ((h64gcvalue *)vparam->ptr_value)->bytes_val.len;
        } else {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_TYPEERROR,
                ".%s() on bytes needs a bytes parameter"
            );
        }

        // Get what we chack .starts() on:
        char *s = NULL;
        int64_t slen = 0;
        if (vc->type == H64VALTYPE_GCVAL &&
                ((h64gcvalue *)vc->ptr_value)->type == H64GCVALUETYPE_BYTES) {
            s = ((h64gcvalue *)vc->ptr_value)->bytes_val.s;
            slen = ((h64gcvalue *)vc->ptr_value)->bytes_val.len;
        } else {
            assert(vc->type == H64VALTYPE_SHORTBYTES);
            s = vc->shortbytes_value;
            slen = vc->shortbytes_len;
        }

        if (slen < paramlen || paramlen == 0) {
            valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
            DELREF_NONHEAP(vcresult);
            valuecontent_Free(vcresult);
            memset(vcresult, 0, sizeof(*vcresult));
            vcresult->type = H64VALTYPE_BOOL;
            vcresult->int_value = !(slen < paramlen);
            return 1;
        }
        int result = (
            memcmp(s, params + (slen - paramlen), sizeof(*params) * paramlen)
        );
        valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
        DELREF_NONHEAP(vcresult);
        valuecontent_Free(vcresult);
        memset(vcresult, 0, sizeof(*vcresult));
        vcresult->type = H64VALTYPE_BOOL;
        vcresult->int_value = (result == 0);
        return 1;
    } else {  // U32 ends():
        // Get parameter which must also be str:
        h64wchar *params = NULL;
        int64_t paramlen = 0;
        valuecontent *vparam = STACK_ENTRY(vmthread->stack, 0);
        if (vparam->type == H64VALTYPE_SHORTSTR) {
            params = vparam->shortstr_value;
            paramlen = vparam->shortstr_len;
        } else if (vparam->type == H64VALTYPE_GCVAL &&
                ((h64gcvalue *)vparam->ptr_value)->type ==
                    H64GCVALUETYPE_STRING) {
            params = ((h64gcvalue *)vparam->ptr_value)->str_val.s;
            paramlen = ((h64gcvalue *)vparam->ptr_value)->str_val.len;
        } else {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_TYPEERROR,
                ".%s() on string needs a string parameter"
            );
        }

        // Get what we chack .starts() on:
        h64wchar *s = NULL;
        int64_t slen = 0;
        if (vc->type == H64VALTYPE_GCVAL &&
                ((h64gcvalue *)vc->ptr_value)->type == H64GCVALUETYPE_STRING) {
            s = ((h64gcvalue *)vc->ptr_value)->str_val.s;
            slen = ((h64gcvalue *)vc->ptr_value)->str_val.len;
        } else {
            assert(vc->type == H64VALTYPE_SHORTSTR);
            s = vc->shortstr_value;
            slen = vc->shortstr_len;
        }

        if (slen < paramlen || paramlen == 0) {
            valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
            DELREF_NONHEAP(vcresult);
            valuecontent_Free(vcresult);
            memset(vcresult, 0, sizeof(*vcresult));
            vcresult->type = H64VALTYPE_BOOL;
            vcresult->int_value = !(slen < paramlen);
            return 1;
        }
        int result = (
            memcmp(s, params + (slen - paramlen), sizeof(*params) * paramlen)
        );
        valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
        DELREF_NONHEAP(vcresult);
        valuecontent_Free(vcresult);
        memset(vcresult, 0, sizeof(*vcresult));
        vcresult->type = H64VALTYPE_BOOL;
        vcresult->int_value = (result == 0);
        return 1;
    }
}

int corelib_RegisterStringFuncs(h64program *p) {
    int64_t idx = -1;

    // '$$string_contains' function:
    idx = h64program_RegisterCFunction(
        p, "$$string_contains", &corelib_stringcontains,
        NULL, 1, NULL, NULL, NULL, 1, -1
    );
    if (idx < 0)
        return 0;
    p->func[idx].input_stack_size++;  // for 'self'
    if (!corelib_RegisterStringsFunc(p, "contains", idx))
        return 0;

    // '$$string_find' function:
    idx = h64program_RegisterCFunction(
        p, "$$string_find", &corelib_stringfind,
        NULL, 1, NULL, NULL, NULL, 1, -1
    );
    if (idx < 0)
        return 0;
    p->func[idx].input_stack_size++;  // for 'self'
    if (!corelib_RegisterStringsFunc(p, "find", idx))
        return 0;

    // '$$string_trim' function:
    idx = h64program_RegisterCFunction(
        p, "$$string_trim", &corelib_stringtrim,
        NULL, 0, NULL, NULL, NULL, 1, -1
    );
    if (idx < 0)
        return 0;
    p->func[idx].input_stack_size++;  // for 'self'
    if (!corelib_RegisterStringsFunc(p, "trim", idx))
        return 0;

    // '$$string_starts' function:
    idx = h64program_RegisterCFunction(
        p, "$$string_starts", &corelib_stringstarts,
        NULL, 1, NULL, NULL, NULL, 1, -1
    );
    if (idx < 0)
        return 0;
    p->func[idx].input_stack_size++;  // for 'self'
    if (!corelib_RegisterStringsFunc(p, "starts", idx))
        return 0;

    // '$$string_ends' function:
    idx = h64program_RegisterCFunction(
        p, "$$string_ends", &corelib_stringends,
        NULL, 1, NULL, NULL, NULL, 1, -1
    );
    if (idx < 0)
        return 0;
    p->func[idx].input_stack_size++;  // for 'self'
    if (!corelib_RegisterStringsFunc(p, "ends", idx))
        return 0;

    // '$$string_lower' function:
    idx = h64program_RegisterCFunction(
        p, "$$string_lower", &corelib_stringlower,
        NULL, 0, NULL, NULL, NULL, 1, -1
    );
    if (idx < 0)
        return 0;
    p->func[idx].input_stack_size++;  // for 'self'
    if (!corelib_RegisterStringsFunc(p, "lower", idx))
        return 0;

    // '$$string_upper' function:
    idx = h64program_RegisterCFunction(
        p, "$$string_upper", &corelib_stringupper,
        NULL, 0, NULL, NULL, NULL, 1, -1
    );
    if (idx < 0)
        return 0;
    p->func[idx].input_stack_size++;  // for 'self'
    if (!corelib_RegisterStringsFunc(p, "upper", idx))
        return 0;

    return 1;
}

funcid_t corelib_GetStringFuncIdx(
        h64program *p, int64_t nameidx, ATTR_UNUSED int isbytes
        ) {
    int i = 0;
    while (i < p->string_indexes.func_count) {
        if (p->string_indexes.func_name_idx[i] == nameidx &&
                (!isbytes || (
                 strcmp(p->string_indexes.func_name[i], "lower") != 0 &&
                 strcmp(p->string_indexes.func_name[i], "upper") != 0))) {
            return p->string_indexes.func_idx[i];
        }
        i++;
    }
    return -1;
}