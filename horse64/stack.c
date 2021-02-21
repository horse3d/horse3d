// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bytecode.h"
#include "nonlocale.h"
#include "vmlist.h"
#include "stack.h"


h64stack *stack_New() {
    h64stack *st = malloc(sizeof(*st));
    if (!st)
        return NULL;
    memset(st, 0, sizeof(*st));

    return st;
}

HOTSPOT static inline void stack_FreeEntry(
        h64stack *st, h64vmthread *vmthread,
        int slot
        ) {
    assert(st->entry[slot].type != H64VALTYPE_CONSTPREALLOCSTR);
    DELREF_NONHEAP(&st->entry[slot]);
    valuecontent_Free(vmthread, &st->entry[slot]);
}

void stack_Free(h64stack *st, h64vmthread *vmthread) {
    if (!st)
        return;
    int64_t k = 0;
    while (k < st->entry_count) {
        stack_FreeEntry(st, vmthread, k);
        k++;
    }
    free(st->entry);
    free(st);
}

void stack_Shrink(
        h64stack *st, h64vmthread *vmthread, int64_t total_entries
        ) {
    int64_t i = st->entry_count;
    while (i > total_entries) {
        stack_FreeEntry(st, vmthread, i - 1);
        i--;
    }
    st->entry_count = i;
    if (st->alloc_count - ALLOC_MAXOVERSHOOT > st->entry_count) {
        int64_t new_alloc = st->entry_count + ALLOC_OVERSHOOT;
        valuecontent *new_entries = realloc(
            st->entry, sizeof(*new_entries) * new_alloc
        );
        if (!new_entries) {
            return;
        }
        st->entry = new_entries;
        st->alloc_count = new_alloc;
    }
}

void stack_PrintDebug(h64stack *st) {
    h64fprintf(stderr, "=== STACK %p ===\n", st);
    h64fprintf(
        stderr, "* Total entries: %" PRId64
        ", alloc entries: %" PRId64 ", func floor: %"
        PRId64 "\n",
        st->entry_count, st->alloc_count,
        st->current_func_floor
    );
    int64_t k = 0;
    while (k < st->entry_count) {
        valuecontent *vc = &st->entry[k];
        h64fprintf(stderr, "%" PRId64 ": ", k);
        if (vc->type == H64VALTYPE_INT64) {
            h64fprintf(stderr, "%" PRId64 " (int)", vc->int_value);
        } else if (vc->type == H64VALTYPE_FLOAT64) {
            h64fprintf(stderr, "%f (float)", vc->float_value);
        } else if (vc->type == H64VALTYPE_BOOL) {
            h64fprintf(stderr, "%s", (vc->int_value ? "true" : "false"));
        } else if (vc->type == H64VALTYPE_GCVAL &&
                   ((h64gcvalue*)vc->ptr_value)->type ==
                   H64GCVALUETYPE_STRING) {
            h64fprintf(stderr, "string at %p", vc->ptr_value);
        } else if (vc->type == H64VALTYPE_GCVAL &&
                   ((h64gcvalue*)vc->ptr_value)->type ==
                   H64GCVALUETYPE_LIST) {
            h64fprintf(
                stderr, "list len=%" PRId64 " at %p",
                vmlist_Count(((h64gcvalue*)vc->ptr_value)->list_values),
                vc->ptr_value
            );
        } else if (vc->type == H64VALTYPE_GCVAL &&
                   ((h64gcvalue*)vc->ptr_value)->type ==
                   H64GCVALUETYPE_FUNCREF_CLOSURE) {
            h64closureinfo *cinfo = (
                ((h64gcvalue*)vc->ptr_value)->closure_info
            );
            h64fprintf(
                stderr, "func closure func_id=%" PRId64
                " at %p", (cinfo ? cinfo->closure_func_id : -1),
                vc->ptr_value
            );
        } else if (vc->type == H64VALTYPE_GCVAL) {
            h64fprintf(
                stderr, "gcval at %p type %d",
                vc->ptr_value, (int)((h64gcvalue *)vc->ptr_value)->type
            );
        } else {
            h64fprintf(stderr, "<value type %d>", (int)vc->type);
        }
        h64fprintf(stderr, "\n");
        k++;
    }
}

int stack_IncreaseAlloc(
        h64stack *st, ATTR_UNUSED h64vmthread *vmthread,
        int64_t total_entries, int alloc_needed_margin
        ) {
    int alloc_optional_margin = alloc_needed_margin;
    if (alloc_optional_margin == 0)
        alloc_optional_margin = ALLOC_EMERGENCY_MARGIN;
    valuecontent *new_entries = realloc(
        st->entry, sizeof(*new_entries) *
            (total_entries + alloc_optional_margin + ALLOC_OVERSHOOT)
    );
    if (!new_entries) {
        if (alloc_needed_margin > 0)
            return 0;
        if (total_entries > st->alloc_count) {
            // Retry without the margin or overshoot:
            valuecontent *new_entries = realloc(
                st->entry, sizeof(*new_entries) *
                    (total_entries)
            );
            if (!new_entries)
                return 0;
        }
    }
    assert(new_entries != NULL);
    st->entry = new_entries;
    st->alloc_count = (
        total_entries + alloc_needed_margin + ALLOC_OVERSHOOT
    );
    return 1;
}

