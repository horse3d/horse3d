// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bytecode.h"
#include "gcvalue.h"
#include "hash.h"
#include "nonlocale.h"
#include "poolalloc.h"
#include "valuecontentstruct.h"
#include "vmexec.h"
#include "vmlist.h"
#include "vmmap.h"
#include "vmstrings.h"
#include "widechar.h"


int valuecontent_SetBytesU8(
        h64vmthread *vmthread, valuecontent *v,
        uint8_t *bytes, int64_t byteslen
        ) {
    valuecontent_Free(v);
    memset(v, 0, sizeof(*v));

    if (byteslen < VALUECONTENT_SHORTBYTESLEN) {
        v->type = H64VALTYPE_SHORTBYTES;
        if (byteslen > 0)
            memcpy(
                v->shortbytes_value, bytes,
                byteslen * sizeof(h64wchar)
            );
        v->shortstr_len = byteslen;
        return 1;
    }

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
    int result = vmbytes_AllocBuffer(
        vmthread, &gcstr->bytes_val, byteslen
    );
    if (!result) {
        poolalloc_free(vmthread->heap, v->ptr_value);
        v->ptr_value = NULL;
        v->type = H64VALTYPE_NONE;
        return 0;
    }
    memcpy(
        gcstr->bytes_val.s, bytes,
        sizeof(*bytes) * byteslen
    );
    gcstr->type = H64GCVALUETYPE_BYTES;
    return 1;
}


int valuecontent_SetStringU32(
        h64vmthread *vmthread, valuecontent *v,
        const h64wchar *s, int64_t slen
        ) {
    valuecontent_Free(v);
    memset(v, 0, sizeof(*v));

    if (slen < VALUECONTENT_SHORTSTRLEN) {
        v->type = H64VALTYPE_SHORTSTR;
        if (slen > 0)
            memcpy(
                v->shortstr_value, s,
                slen * sizeof(h64wchar)
            );
        v->shortstr_len = slen;
        return 1;
    }

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
    assert(gcstr->str_val.len == (uint64_t)slen);
    assert(gcstr->str_val.letterlen == 0);
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

int valuecontent_IsMutable(valuecontent *v) {
    if (v->type == H64VALTYPE_GCVAL) {
        h64gcvalue *gcval = ((h64gcvalue *)v->ptr_value);
        return (gcval->type != H64GCVALUETYPE_BYTES &&
                gcval->type != H64GCVALUETYPE_STRING);
    }
    return 0;
}

uint32_t _valuecontent_Hash_Do(
        valuecontent *v, int depth
        ) {
    if (depth >= 2)
        return 0;
    if (v->type == H64VALTYPE_NONE ||
            v->type == H64VALTYPE_UNSPECIFIED_KWARG) {
        return 0;
    } else if (v->type == H64VALTYPE_INT64) {
        return (v->int_value % INT32_MAX);
    } else if (v->type == H64VALTYPE_FLOAT64) {
        // Split up into exponent & factor,
        // the float is fac_f * (2 ^ exponent).
        int exponent32 = 0;
        double fac_f = frexp(v->float_value, &exponent32);
        int64_t exponent = exponent32;
        // fac_f is in -1.0...1.0, map to roughly 32bit int range:
        int64_t fac = (int)(fac_f * (double)2147483648LL);
        if (fac < 0) fac = -fac;
        return ((exponent + fac) % INT32_MAX);
    } else if (v->type == H64VALTYPE_BOOL) {
        return (v->int_value != 0);
    } else if (v->type == H64VALTYPE_SHORTSTR ||
               v->type == H64VALTYPE_CONSTPREALLOCSTR) {
        char *s = (char *)(
            v->type == H64VALTYPE_SHORTSTR ? v->shortstr_value :
            v->constpreallocstr_value
        );
        uint64_t slen = (
            v->type == H64VALTYPE_SHORTSTR ? v->shortstr_len :
            v->constpreallocstr_len
        );
        uint64_t h = 0;
        uint64_t i = 0;
        while (i < slen && i < 16) {
            h = (h + ((h64wchar *)s)[i]) % INT32_MAX;
            i++;
        }
        h = (h + slen % INT32_MAX) % INT32_MAX;
        return (h != 0 ? h : 1);
    } else if (v->type == H64VALTYPE_SHORTBYTES ||
               v->type == H64VALTYPE_CONSTPREALLOCBYTES) {
        char *s = (
            v->type == H64VALTYPE_SHORTBYTES ? v->shortbytes_value :
            v->constpreallocbytes_value
        );
        uint64_t slen = (
            v->type == H64VALTYPE_SHORTBYTES ? v->shortbytes_len :
            v->constpreallocbytes_len
        );
        uint64_t h = 0;
        uint64_t i = 0;
        while (i < slen && i < 16) {
            h = (h + s[i]) % INT32_MAX;
            i++;
        }
        h = (h + slen % INT32_MAX) % INT32_MAX;
        return (h != 0 ? h : 1);
    } else if (v->type == H64VALTYPE_GCVAL) {
        h64gcvalue *gcval = ((h64gcvalue *)v->ptr_value);
        if (gcval->hash != 0)
            return gcval->hash;
        if (gcval->type == H64GCVALUETYPE_FUNCREF_CLOSURE) {
            uint64_t h = (
                gcval->closure_info->closure_func_id %
                INT32_MAX
            );
            return h;
        } else if (gcval->type == H64GCVALUETYPE_STRING) {
            uint64_t h = 0;
            uint64_t i = 0;
            while (i < gcval->str_val.len && i < 16) {
                h = (h + gcval->str_val.s[i]) % INT32_MAX;
                i++;
            }
            h = (h + gcval->str_val.len % INT32_MAX) % INT32_MAX;
            gcval->hash = (h != 0 ? h : 1);
            return gcval->hash;
        } else if (gcval->type == H64GCVALUETYPE_BYTES) {
            uint64_t h = 0;
            uint64_t i = 0;
            while (i < gcval->bytes_val.len && i < 16) {
                h = (h + gcval->bytes_val.s[i]) % INT32_MAX;
                i++;
            }
            h = (h + gcval->bytes_val.len % INT32_MAX) % INT32_MAX;
            gcval->hash = (h != 0 ? h : 1);
            return gcval->hash;
        } else if (gcval->type == H64GCVALUETYPE_LIST) {
            uint64_t h = 0;
            uint64_t count = vmlist_Count(gcval->list_values);
            uint64_t upto = count;
            if (upto > 32)
                upto = 32;
            uint64_t i = 0;
            while (i < upto) {
                valuecontent *item = vmlist_Get(gcval->list_values, i);
                if (valuecontent_IsMutable(item)) {
                    i++;
                    continue;
                }
                h = (h + _valuecontent_Hash_Do(
                    item, depth + 1
                ) % INT32_MAX) % INT32_MAX;
                i++;
            }
            h = (h + upto % INT32_MAX) % INT32_MAX;
            gcval->hash = h;
            return gcval->hash;
        } else if (gcval->type == H64GCVALUETYPE_SET) {
            return 0;
        } else if (gcval->type == H64GCVALUETYPE_MAP) {
            return 0;
        } else if (gcval->type == H64GCVALUETYPE_OBJINSTANCE) {
            return 0;
        } else {
            assert(0);  // Should be unreachable
            return 0;
        }
    } else if (v->type ==  H64VALTYPE_FUNCREF ||
            v->type == H64VALTYPE_CLASSREF) {
        uint64_t h = (v->int_value % INT32_MAX);
        return h;
    } else if (v->type == H64VALTYPE_ERROR) {
        uint64_t h = (v->error_class_id % INT32_MAX);
        return h;
    } else {
        assert(0);  // Should be unreachable
        return 0;
    }
    return 0;
}

uint32_t valuecontent_Hash(
        valuecontent *v
        ) {
    return _valuecontent_Hash_Do(v, 0);
}

int _valuecontent_CheckContainerEquality_Do(
        valuecontent *v1, valuecontent *v2,
        hashmap *seen, uint64_t *seennum, int *oom
        ) {
    if (oom) *oom = 0;

    if (unlikely(v1->type != H64VALTYPE_GCVAL ||
            v2->type != H64VALTYPE_GCVAL ||
            ((h64gcvalue *)v1->ptr_value)->type !=
                ((h64gcvalue *)v2->ptr_value)->type))
        return 0;
    h64gcvalue *g1 = (h64gcvalue *)v1->ptr_value;
    h64gcvalue *g2 = (h64gcvalue *)v2->ptr_value;
    if (g1 == g2)
        return 1;

    // If this container pair was already seen, then it needs to have
    // the same id (= was registered as seen for both v1 + v2 at the
    // same time, which ensures cycle graph equivalency):
    uintptr_t g1_seenid = 0;
    if (unlikely(hash_BytesMapGet(
            seen, (const char *)&g1, sizeof(g1), (uint64_t *)&g1_seenid
            ))) {
        uintptr_t g2_seenid = 0;
        if (!hash_BytesMapGet(
                seen, (const char *)&g2, sizeof(g2),
                (uint64_t *)&g2_seenid
                ) || g1_seenid != g2_seenid) {
            // Different cycle, or only cycle on one side.
            return 0;
        }
        // Same cycle.
        return 1;
    }
    if (*seennum + 1 >= UINT64_MAX) {
        // We can't reasonably compare something of this graph size.
        if (oom) *oom = 1;
        return 0;
    }
    (*seennum)++;
    uint64_t newseennum = *seennum;
    if (!hash_BytesMapSet(seen, (const char *)&g1,
            sizeof(g1), (uint64_t)newseennum
            ) || !hash_BytesMapSet(seen, (const char *)&g2,
            sizeof(g2), (uint64_t)newseennum
            )) {
        if (oom) *oom = 1;
        return 0;
    }

    // Now compare contents by value:
    if (g1->type == H64GCVALUETYPE_LIST) {
        assert(g2->type == H64GCVALUETYPE_LIST);
        if (vmlist_Count(g1->list_values) != vmlist_Count(g2->list_values))
            return 0;
        assert(0);  // fixme. finish
    } else if (g1->type == H64GCVALUETYPE_MAP) {
        assert(g2->type == H64GCVALUETYPE_MAP);
        if (vmmap_Count(g1->map_values) != vmmap_Count(g2->map_values))
            return 0;
        assert(0);  // fixme. finish.
    } else {
        fprintf(stderr, "valuecontentstruct.c: container comparison "
            "not implemented");
    }
    return 0;
}

int valuecontent_CheckContainerEquality(
        valuecontent *v1, valuecontent *v2,
        int *oom
        ) {
    if (oom) *oom = 0;
    hashmap *seen = hash_NewBytesMap(128);
    if (!seen) {
        if (oom) *oom = 1;
        return 0;
    }
    uint64_t seennum = 0;
    int result = _valuecontent_CheckContainerEquality_Do(
        v1, v2, seen, &seennum, oom
    );
    hash_FreeMap(seen);
    return result;
}

int valuecontent_CheckEquality(
        valuecontent *v1, valuecontent *v2, int *oom
        ) {
    if (oom) *oom = 0;
    if (likely((v1->type != H64VALTYPE_INT64 &&
            v1->type != H64VALTYPE_FLOAT64) ||
            (v2->type != H64VALTYPE_INT64 &&
            v2->type != H64VALTYPE_FLOAT64))) {
        if (unlikely(v1->type == H64VALTYPE_GCVAL &&
                ((h64gcvalue*)v1->ptr_value)->type ==
                H64GCVALUETYPE_OBJINSTANCE && (
                v2->type != H64VALTYPE_GCVAL ||
                ((h64gcvalue*)v2->ptr_value)->type ==
                H64GCVALUETYPE_OBJINSTANCE ||
                ((h64gcvalue*)v1->ptr_value)->class_id !=
                ((h64gcvalue*)v2->ptr_value)->class_id))) {
            // Special case: quick fail, don't do an expensive
            // in-depth .equals() when these aren't even both
            // object instances of same class.
            return 0;
        } else if ((v1->type == H64VALTYPE_GCVAL &&
                ((h64gcvalue*)v1->ptr_value)->type ==
                H64GCVALUETYPE_STRING) ||
                v1->type == H64VALTYPE_SHORTSTR ||
                v1->type == H64VALTYPE_CONSTPREALLOCSTR) {
            // Strings!
            if (likely((v2->type == H64VALTYPE_GCVAL &&
                    ((h64gcvalue*)v2->ptr_value)->type ==
                    H64GCVALUETYPE_STRING) ||
                    v2->type == H64VALTYPE_SHORTSTR ||
                    v2->type == H64VALTYPE_CONSTPREALLOCSTR)) {
                return vmstrings_Equality(v1, v2);
            } else {
                return 0;
            }
            return 1;
        } else if ((v1->type == H64VALTYPE_GCVAL &&
                ((h64gcvalue*)v1->ptr_value)->type ==
                H64GCVALUETYPE_BYTES) ||
                v1->type == H64VALTYPE_SHORTBYTES ||
                v1->type == H64VALTYPE_CONSTPREALLOCBYTES) {
            // Bytes!
            if (likely((v2->type == H64VALTYPE_GCVAL &&
                    ((h64gcvalue*)v1->ptr_value)->type ==
                    H64GCVALUETYPE_BYTES) ||
                    v2->type == H64VALTYPE_SHORTBYTES ||
                    v2->type == H64VALTYPE_CONSTPREALLOCBYTES)) {
                return vmbytes_Equality(v1, v2);
            } else {
                return 0;
            }
            return 1;
        } else {
            // Remaining cases:
            if (v1->type != v2->type || (
                    v1->type == H64VALTYPE_GCVAL &&
                    ((h64gcvalue *)v1->ptr_value)->type !=
                    ((h64gcvalue *)v2->ptr_value)->type)) {
                return 0;
            } else if (v1->type == H64VALTYPE_BOOL) {
                return (
                    (v1->int_value != 0) == (v2->int_value != 0)
                );
            } else if (v1->type == H64VALTYPE_NONE) {
                return 1;
            } else if (v1->type == H64VALTYPE_UNSPECIFIED_KWARG) {
                return (v2->type == H64VALTYPE_UNSPECIFIED_KWARG);
            } else if (v1->type == H64VALTYPE_GCVAL && (
                    ((h64gcvalue *)v1->ptr_value)->type ==
                    H64GCVALUETYPE_LIST ||
                    ((h64gcvalue *)v1->ptr_value)->type ==
                    H64GCVALUETYPE_MAP ||
                    ((h64gcvalue *)v1->ptr_value)->type ==
                    H64GCVALUETYPE_SET ||
                    ((h64gcvalue *)v1->ptr_value)->type ==
                    H64GCVALUETYPE_OBJINSTANCE)) {
                int inneroom = 0;
                int result = valuecontent_CheckContainerEquality(
                    v1, v2, &inneroom
                );
                if (!result && inneroom) {
                    if (oom) *oom = 1;
                }
                return result;
            } else {
                // Shouldn't be hit, at least once we're done
                // FIXME: will still be hit for now
                h64fprintf(stderr, "UNIMPLEMENTED EQ CASE\n");
                _exit(1);
                return 0;
            }
        }
    } else {
        // Numbers.
        if (v1->type == H64VALTYPE_FLOAT64 ||
                v2->type == H64VALTYPE_FLOAT64) {
            double v1no = 1;
            if (v1->type == H64VALTYPE_FLOAT64) {
                v1no = v1->float_value;
            } else {
                v1no = v1->int_value;
            }
            double v2no = 1;
            if (v2->type == H64VALTYPE_FLOAT64) {
                v2no = v2->float_value;
            } else {
                v2no = v2->int_value;
            }
            return (v1no == v2no);
        } else {
            return (
                v1->int_value == v2->int_value
            );
        }
    }
}

int valuecontent_CompareValues(
        valuecontent *v1, valuecontent *v2,
        int *result, int *typesnotcomparable
        ) {
    if (likely(v1->type == H64VALTYPE_INT64 &&
            v2->type == H64VALTYPE_INT64)) {
        if (v1->int_value > v2->int_value)
            *result = 1;
        else if (v1->int_value < v2->int_value)
            *result = -1;
        else
            *result = 0;
        return 1;
    } else if ((v1->type == H64VALTYPE_INT64 ||
            v1->type == H64VALTYPE_FLOAT64) && (
            v2->type == H64VALTYPE_INT64 ||
            v2->type == H64VALTYPE_FLOAT64)) {
        double v1f = 0;
        if (v1->type == H64VALTYPE_INT64) {
            v1f = v1->int_value;
        } else {
            v1f = v1->float_value;
        }
        double v2f = 0;
        if (v2->type == H64VALTYPE_INT64) {
            v2f = v2->int_value;
        } else {
            v2f = v2->float_value;
        }
        if (v1 > v2)
            *result = 1;
        else if (v1 < v2)
            *result = -1;
        else
            *result = 0;
        return 1;
    } else {
        *typesnotcomparable = 1;
        return 0;
    }
}