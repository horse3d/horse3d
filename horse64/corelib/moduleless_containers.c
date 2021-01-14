// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>

#include "bytecode.h"
#include "corelib/errors.h"
#include "corelib/moduleless.h"
#include "corelib/moduleless_containers.h"
#include "debugsymbols.h"
#include "stack.h"
#include "vmexec.h"
#include "vmlist.h"
#include "vmmap.h"


int corelib_containeradd(  // $$builtin.$$container_add
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

int corelib_containercontains(  // $$builtin.$$container_contains
        h64vmthread *vmthread
        ) {
    assert(STACK_TOP(vmthread->stack) >= 2);

    valuecontent *vc = STACK_ENTRY(vmthread->stack, 1);
    assert(vc->type == H64VALTYPE_GCVAL);
    h64gcvalue *gcvalue = (h64gcvalue *)vc->ptr_value;
    int result = 0;
    if (gcvalue->type == H64GCVALUETYPE_LIST) {
        result = vmlist_Contains(gcvalue->list_values,
            STACK_ENTRY(vmthread->stack, 0)
        );
    } else if (gcvalue->type == H64GCVALUETYPE_MAP) {
        result = vmmap_Contains(gcvalue->map_values,
            STACK_ENTRY(vmthread->stack, 0)
        );
    }

    valuecontent *vresult = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vresult);
    valuecontent_Free(vresult);
    memset(vresult, 0, sizeof(*vresult));
    vresult->type = H64VALTYPE_BOOL;
    vresult->int_value = result;
    return 1;
}

int corelib_RegisterContainersFunc(
            h64program *p, const char *name,
            funcid_t funcidx
            ) {
    char **new_func_name = realloc(
        p->container_indexes.func_name,
        sizeof(*new_func_name) * (p->container_indexes.func_count + 1)
    );
    if (!new_func_name)
        return 0;
    p->container_indexes.func_name = new_func_name;
    int64_t *new_name_idx = realloc(
        p->container_indexes.func_name_idx,
        sizeof(*new_name_idx) * (p->container_indexes.func_count + 1)
    );
    if (!new_name_idx)
        return 0;
    p->container_indexes.func_name_idx = new_name_idx;
    funcid_t *new_func_idx = realloc(
        p->container_indexes.func_idx,
        sizeof(*new_func_idx) * (p->container_indexes.func_count + 1)
    );
    if (!new_func_idx)
        return 0;
    p->container_indexes.func_idx = new_func_idx;
    p->container_indexes.func_name[
        p->container_indexes.func_count
    ] = strdup(name);
    if (!p->container_indexes.func_name[
            p->container_indexes.func_count])
        return 0;
    p->container_indexes.func_idx[
        p->container_indexes.func_count
    ] = funcidx;
    p->container_indexes.func_name_idx[
        p->container_indexes.func_count
    ] = h64debugsymbols_AttributeNameToAttributeNameId(
        p->symbols, name, 1, 1
    );
    if (p->container_indexes.func_name_idx[
            p->container_indexes.func_count
            ] < 0) {
        free(p->container_indexes.func_name[
             p->container_indexes.func_count]);
        return 0;
    }
    p->container_indexes.func_count++;
    return 1;
}

funcid_t corelib_GetContainerFuncIdx(
        h64program *p, int64_t nameidx, int container_type
        ) {
    int i = 0;
    while (i < p->container_indexes.func_count) {
        if (p->container_indexes.func_name_idx[i] == nameidx) {
            if (container_type == H64GCVALUETYPE_MAP &&
                    strcmp(p->container_indexes.func_name[i], "add") == 0)
                return -1;  // maps have no .add()
            return p->container_indexes.func_idx[i];
        }
        i++;
    }
    return -1;
}

int corelib_RegisterContainerFuncs(h64program *p) {
    int64_t idx;

    // '$$container_add' function:
    idx = h64program_RegisterCFunction(
        p, "$$container_add", &corelib_containeradd,
        NULL, 1, NULL, NULL, NULL, 1, -1
    );
    if (idx < 0)
        return 0;
    p->func[idx].input_stack_size++;  // for 'self'
    if (!corelib_RegisterContainersFunc(p, "add", idx))
        return 0;

    // '$$container_contains' function:
    idx = h64program_RegisterCFunction(
        p, "$$container_contains", &corelib_containercontains,
        NULL, 1, NULL, NULL, NULL, 1, -1
    );
    if (idx < 0)
        return 0;
    p->func[idx].input_stack_size++;  // for 'self'
    if (!corelib_RegisterContainersFunc(p, "contains", idx))
        return 0;

    return 1;
}