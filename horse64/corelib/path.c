// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <stdint.h>

#include "bytecode.h"
#include "corelib/errors.h"
#include "filesys32.h"
#include "path.h"
#include "stack.h"
#include "valuecontentstruct.h"
#include "vmexec.h"
#include "vmlist.h"
#include "widechar.h"


/// @module path Work with file system paths and folders.

int pathlib_join(
        h64vmthread *vmthread
        ) {
    /**
     * Join the given list of path components to a combined path.
     * This will use the platform-specific path separators, like
     * backslash (\) on Windows, forward slash (/) on Linux, etc.
     *
     * @func join
     * @param components the @see{list} of path components to be joined,
     *                   each component being a @see{string}.
     * @returns a @see{string} resulting from joining all path components.
     */
    assert(STACK_TOP(vmthread->stack) >= 1);

    valuecontent *vccomponents = STACK_ENTRY(vmthread->stack, 0);
    if (vccomponents->type != H64VALTYPE_GCVAL ||
            ((h64gcvalue*)(vccomponents->ptr_value))->type !=
                H64GCVALUETYPE_LIST) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "path must be a string"
        );
    }
    valuecontent *vcargs = STACK_ENTRY(vmthread->stack, 0);
    if ((vcargs->type != H64VALTYPE_GCVAL ||
            ((h64gcvalue*)(vcargs->ptr_value))->type !=
                H64GCVALUETYPE_LIST) &&
            vcargs->type != H64VALTYPE_UNSPECIFIED_KWARG) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "components argument must be a list"
        );
    }

    h64wchar *result = NULL;
    int64_t resultlen = 0;
    genericlist *l = ((h64gcvalue*)(vcargs->ptr_value))->list_values;
    const int64_t len = vmlist_Count(l);
    if (len == 0) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_ARGUMENTERROR,
            "components list must have at least one item"
        );
    }
    int64_t i = 1;
    while (i <= len) {
        valuecontent *component = vmlist_Get(l, i);
        h64wchar *componentstr = NULL;
        int64_t componentlen = 0;
        if (component->type == H64VALTYPE_GCVAL &&
                ((h64gcvalue *)component->ptr_value)->type ==
                    H64GCVALUETYPE_STRING) {
            componentstr = (
                ((h64gcvalue *)component->ptr_value)->str_val.s
            );
            componentlen = (
                ((h64gcvalue *)component->ptr_value)->str_val.len
            );
        } else if (component->type == H64VALTYPE_SHORTSTR) {
            componentstr = component->shortstr_value;
            componentlen = component->shortstr_len;
        } else {
            free(result);
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_TYPEERROR,
                "components in list must each be a string"
            );
        }
        if (result == NULL) {
            result = malloc(componentlen * sizeof(*componentstr));
            if (!result) {
                return vmexec_ReturnFuncError(
                    vmthread, H64STDERROR_OUTOFMEMORYERROR,
                    "out of memory creating joined string"
                );
            }
            memcpy(result, componentstr,
                   componentlen * sizeof(*componentstr));
            resultlen = componentlen;
        } else {
            assert(result != NULL && componentstr != NULL);
            int64_t newresultlen = 0;
            h64wchar *newresult = filesys32_Join(
                result, resultlen, componentstr, componentlen,
                &newresultlen
            );
            if (!newresult) {
                free(result);
                return vmexec_ReturnFuncError(
                    vmthread, H64STDERROR_OUTOFMEMORYERROR,
                    "out of memory creating joined string"
                );
            }
            free(result);
            result = newresult;
            resultlen = newresultlen;
        }
        i++;
    }
    valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vcresult);
    valuecontent_Free(vcresult);
    memset(vcresult, 0, sizeof(*vcresult));
    if (!valuecontent_SetStringU32(
            vmthread, vcresult, result, resultlen
            )) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OUTOFMEMORYERROR,
            "out of memory creating joined string"
        );
    }
    return 1;
}

int pathlib_RegisterFuncsAndModules(h64program *p) {
    int64_t idx;

    // path.join:
    const char *path_join_kw_arg_name[] = {
        NULL
    };    
    idx = h64program_RegisterCFunction(
        p, "join", &pathlib_join,
        NULL, 1, path_join_kw_arg_name,  // fileuri, args
        "path", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    return 1;
}