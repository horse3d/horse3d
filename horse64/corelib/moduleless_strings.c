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
#include "vmexec.h"


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


int corelib_RegisterStringFuncs(h64program *p) {
    return 1;
}