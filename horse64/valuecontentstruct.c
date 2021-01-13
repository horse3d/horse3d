// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <stdlib.h>
#include <string.h>

#include "bytecode.h"
#include "gcvalue.h"
#include "poolalloc.h"
#include "valuecontentstruct.h"
#include "vmexec.h"
#include "vmstrings.h"
#include "widechar.h"


int valuecontent_SetStringU32(
        h64vmthread *vmthread, valuecontent *v,
        h64wchar *s, int64_t slen
        ) {
    valuecontent_Free(v);
    memset(v, 0, sizeof(*v));

    v->type = H64VALTYPE_GCVAL;
    v->ptr_value = poolalloc_malloc(
        vmthread->heap, 0
    );
    if (!v->ptr_value) {
        v->type = H64VALTYPE_NONE;
        return 0;
    }
    h64gcvalue *gcstr = v->ptr_value;
    memset(gcstr, 0, sizeof(*gcstr));
    int result = vmstrings_AllocBuffer(
        vmthread, &gcstr->str_val, slen
    );
    if (!result) {
        poolalloc_free(vmthread->heap, v->ptr_value);
        v->ptr_value = NULL;
        v->type = H64VALTYPE_NONE;
        return 0;
    }
    memcpy(
        gcstr->str_val.s, s,
        sizeof(*s) * slen
    );
    gcstr->type = H64GCVALUETYPE_STRING;
    return 1;
}

int valuecontent_SetStringU8(
        h64vmthread *vmthread, valuecontent *v, const char *u8
        ) {
    int wasinvalid = 0;
    int wasoom = 0;
    char short_buf[512];
    int64_t u32len = 0;
    h64wchar *u32 = utf8_to_utf32_ex(
        u8, strlen(u8),
        short_buf, sizeof(short_buf),
        NULL, NULL, &u32len, 1, 0,
        &wasinvalid, &wasoom
    );
    if (!u32)
        return 0;
    int result = valuecontent_SetStringU32(
        vmthread, v, u32, u32len
    );
    if (u32 != (h64wchar*) short_buf)
        free(u32);
    return result;
}

int valuecontent_SetPreallocStringU8(
        ATTR_UNUSED h64program *p, valuecontent *v, const char *u8
        ) {
    int wasinvalid = 0;
    int wasoom = 0;
    char short_buf[512];
    int64_t u32len = 0;
    h64wchar *u32 = utf8_to_utf32_ex(
        u8, strlen(u8),
        short_buf, sizeof(short_buf),
        NULL, NULL, &u32len, 1, 0,
        &wasinvalid, &wasoom
    );
    if (!u32)
        return 0;
    valuecontent_Free(v);
    memset(v, 0, sizeof(*v));

    v->constpreallocstr_value = malloc(u32len * sizeof(h64wchar));
    int result = 0;
    if (v->constpreallocstr_value) {
        result = 1;
        v->type = H64VALTYPE_CONSTPREALLOCSTR;
        v->constpreallocstr_len = u32len;
        memcpy(
            v->constpreallocstr_value, u32,
            u32len * sizeof(h64wchar)
        );
    }

    if (u32 != (h64wchar*) short_buf)
        free(u32);
    return 1;
}