// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_DEBUGSYMBOLS_H_
#define HORSE64_DEBUGSYMBOLS_H_

#include "compileconfig.h"

#include <stdint.h>

#include "widechar.h"

typedef struct hashmap hashmap;
typedef struct h64program h64program;

typedef struct h64funcsymbol {
    char *name;
    int has_self_arg, arg_count, last_arg_is_multiarg,
        stack_temporaries_count, closure_bound_count;
    char **arg_kwarg_name;
    int fileuri_index;
    int64_t header_symbol_line, header_symbol_column;
    int instruction_count;
    int64_t *instruction_to_line;
    int64_t *instruction_to_column;

    int global_id;
} h64funcsymbol;

typedef struct h64classsymbol {
    char *name;
    int fileuri_index;

    void *_tmp_classdef_expr_ptr;
    void **_tmp_varattr_expr_ptr;

    int global_id;
} h64classsymbol;

typedef struct h64globalvarsymbol {
    char *name;
    int is_const;
    int fileuri_index;
    int is_simple_const;

    int global_id;
} h64globalvarsymbol;

typedef struct h64modulesymbols {
    int index;
    char *module_path, *library_name;
    int noncfunc_count;

    hashmap *func_name_to_entry;
    int func_count;
    h64funcsymbol *func_symbols;

    hashmap *class_name_to_entry;
    int classes_count;
    h64classsymbol *classes_symbols;

    hashmap *globalvar_name_to_entry;
    int globalvar_count;
    h64globalvarsymbol *globalvar_symbols;
} h64modulesymbols;

typedef struct h64debugsymbols {
    h64program *program;

    int fileuri_count;
    h64wchar **fileuri;
    int64_t *fileurilen;

    int mainfileuri_index;
    char *mainfile_module_path;

    hashmap *modulelibpath_to_modulesymbol_id;
    int module_count;
    h64modulesymbols **module_symbols;

    hashmap *attribute_name_to_global_attribute_id;
    int64_t global_attribute_count;
    char **global_attribute_name;
    int *global_attribute_needsidx;

    hashmap *func_id_to_module_symbols_index;
    hashmap *func_id_to_module_symbols_func_subindex;
    hashmap *class_id_to_module_symbols_index;
    hashmap *class_id_to_module_symbols_class_subindex;
    hashmap *globalvar_id_to_module_symbols_index;
    hashmap *globalvar_id_to_module_symbols_globalvar_subindex;
} h64debugsymbols;

int64_t h64debugsymbols_AttributeNameToAttributeNameId(
    h64debugsymbols *symbols, const char *name,
    int addifnotpresent, int requirefixednameidx
);

void h64debugsymbols_ClearFuncSymbol(
    h64funcsymbol *fsymbol
);

void h64debugsymbols_ClearClassSymbol(
    h64classsymbol *csymbol
);

void h64debugsymbols_ClearGlobalvarSymbol(
    h64globalvarsymbol *gsymbol
);

h64modulesymbols *h64debugsymbols_GetModule(
    h64debugsymbols *symbols, const char *modpath,
    const char *library_name,
    int addifnotpresent
);

h64modulesymbols *h64debugsymbols_GetBuiltinModule(
    h64debugsymbols *symbols
);

void h64debugsymbols_ClearModule(h64modulesymbols *msymbols);

void h64debugsymbols_Free(h64debugsymbols *symbols);

h64debugsymbols *h64debugsymbols_New();

h64modulesymbols *h64debugsymbols_GetModuleSymbolsByFuncId(
    h64debugsymbols *symbols, int64_t funcid
);

h64modulesymbols *h64debugsymbols_GetModuleSymbolsByClassId(
    h64debugsymbols *symbols, int64_t classid
);

h64funcsymbol *h64debugsymbols_GetFuncSymbolById(
    h64debugsymbols *symbols, int64_t funcid
);

h64classsymbol *h64debugsymbols_GetClassSymbolById(
    h64debugsymbols *symbols, int64_t classid
);

h64globalvarsymbol *h64debugsymbols_GetGlobalvarSymbolById(
    h64debugsymbols *symbols, int64_t globalid
);

int64_t h64debugsymbols_GetFileUriIndex(
    h64debugsymbols *symbols, const h64wchar *fileuri,
    int64_t fileurilen, int addifnotpresent
);

#endif  // HORSE64_DEBUGSYMBOLS_H_
