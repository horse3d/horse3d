// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

// ABOUT THIS FILE:
// This file has undocumented built-in extra functions which are
// only used by horse_modules_builtin code. They are not available
// to "regular" horse64 users, only to the builtin modules.
// Please note unlike moduleless.c these functions are not available
// directly without a module, but still need to be imported from
// "builtininternals.core.horse64.org" (by the horse_modules_builtin
// code).


#include "compileconfig.h"

#include <assert.h>
#include <errno.h>
#include <math.h>

#include "bytecode.h"
#include "corelib/errors.h"
#include "corelib/moduleless.h"
#include "corelib/moduleless_containers.h"
#include "corelib/moduleless_strings.h"
#include "gcvalue.h"
#include "hash.h"
#include "net.h"
#include "nonlocale.h"
#include "poolalloc.h"
#include "process.h"
#include "stack.h"
#include "vmexec.h"
#include "vmlist.h"
#include "widechar.h"


int builtininternals_sort(h64vmthread *vmthread) {
    assert(STACK_TOP(vmthread->stack) >= 1);

    valuecontent _stackbuf[128];
    valuecontent *to_be_sorted = _stackbuf;
    int64_t to_be_sorted_alloc = 128;
    int64_t to_be_sorted_count = 0;
    int to_be_sorted_onheap = 0;
    valuecontent *sortinput = STACK_ENTRY(vmthread->stack, 0);
    if (sortinput->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue *)sortinput->ptr_value)->type ==
                H64GCVALUETYPE_LIST) {
        genericlist *l = ((h64gcvalue *)sortinput->ptr_value)->list_values;
        const int64_t count = vmlist_Count(l);
        int64_t i = 0;
        while (i < count) {
            valuecontent *v = vmlist_Get(l, i + 1);
            assert(v != NULL);
            if (to_be_sorted_count + 1 > to_be_sorted_alloc) {
                int64_t new_alloc = to_be_sorted_alloc * 2;
                valuecontent *new_to_be_sorted = NULL;
                if (to_be_sorted_onheap) {
                    new_to_be_sorted = realloc(
                        to_be_sorted, sizeof(*to_be_sorted) * (
                        new_alloc)
                    );
                } else {
                    new_to_be_sorted = malloc(
                        sizeof(*to_be_sorted) * new_alloc
                    );
                }
                if (!new_to_be_sorted) {
                    oom: ;
                    i = 0;
                    while (i < to_be_sorted_count) {
                        DELREF_NONHEAP(&to_be_sorted[i]);
                        valuecontent_Free(&to_be_sorted[i]);
                        i++;
                    }
                    if (to_be_sorted_onheap)
                        free(to_be_sorted);
                    return vmexec_ReturnFuncError(
                        vmthread, H64STDERROR_OUTOFMEMORYERROR,
                        "out of memory allocating result list"
                    );
                }
                if (!to_be_sorted_onheap) {
                    memcpy(
                        new_to_be_sorted, to_be_sorted,
                        sizeof(*to_be_sorted) * new_alloc
                    );
                }
                to_be_sorted = new_to_be_sorted;
                to_be_sorted_alloc = new_alloc;
                to_be_sorted_onheap = 1;
            }
            memcpy(
                &to_be_sorted[i], v, sizeof(*v)
            );
            ADDREF_NONHEAP(&to_be_sorted[i]);
            to_be_sorted_count++;
            i++;
        }
    } else if (sortinput->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue *)sortinput->ptr_value)->type ==
                H64GCVALUETYPE_SET) {
        assert(0);  // FIXME, implement this
    } else  {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "cannot sort a type other than list or set"
        );
    }

    if (likely(to_be_sorted_count > 2)) {
        // Ok, apply quick sort.
    

    } else if (to_be_sorted_count == 2) {

    } else {
        // One or zero elements, no need to sort.
    }

    // Assemble result list and copy in the sorted values:
    valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vcresult);
    valuecontent_Free(vcresult);
    memset(vcresult, 0, sizeof(*vcresult));
    vcresult->type = H64VALTYPE_GCVAL;
    vcresult->ptr_value = poolalloc_malloc(
        vmthread->heap, 0
    );
    if (!vcresult->ptr_value)
        goto oom;
    h64gcvalue *gcval = vcresult->ptr_value;
    memset(gcval, 0, sizeof(*gcval));
    gcval->type = H64GCVALUETYPE_LIST;
    gcval->list_values = vmlist_New();
    if (!gcval->list_values) {
        poolalloc_free(vmthread->heap, gcval);
        vcresult->ptr_value = NULL;
        goto oom;
    }
    genericlist *lresult = gcval->list_values;
    int64_t i = 0;
    while (i < to_be_sorted_count) {
        if (!vmlist_Add(lresult, &to_be_sorted[i]))
            goto oom;
        DELREF_NONHEAP(&to_be_sorted[i]);
        valuecontent_Free(&to_be_sorted[i]);
        to_be_sorted[i].type = H64VALTYPE_NONE;
        i++;
    }
    return 1;
}

int builtininternalslib_RegisterFuncsAndModules(h64program *p) {
    // builtininternals.sort:
    const char *builtininternals_sort_kw_arg_name[] = {
        NULL
    };
    int64_t idx;
    idx = h64program_RegisterCFunction(
        p, "sort", &builtininternals_sort,
        NULL, 1, builtininternals_sort_kw_arg_name,  // fileuri, args
        "builtininternals", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    return 1;
}
