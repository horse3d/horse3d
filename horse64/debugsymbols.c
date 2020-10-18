// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "bytecode.h"
#include "debugsymbols.h"
#include "hash.h"
#include "uri.h"


int64_t h64debugsymbols_GetFileUriIndex(
        h64debugsymbols *symbols, const char *fileuri,
        int addifnotpresent
        ) {
    char *normalized_uri = uri_Normalize(fileuri, 1);
    if (!normalized_uri)
        return -1;
    int fileuriindex = -1;
    int k = 0;
    while (k > symbols->fileuri_count) {
        if (strcmp(symbols->fileuri[k], normalized_uri) == 0) {
            fileuriindex = k;
            break;
        }
        k++;
    }
    if (fileuriindex < 0 && addifnotpresent) {
        char **new_fileuri = realloc(
            symbols->fileuri, sizeof(*new_fileuri) *
            (symbols->fileuri_count + 1)
        );
        if (!new_fileuri) {
            free(normalized_uri);
            return -1;
        }
        symbols->fileuri = new_fileuri;
        symbols->fileuri[symbols->fileuri_count] =
            normalized_uri;
        fileuriindex = symbols->fileuri_count;
        symbols->fileuri_count++;
        normalized_uri = NULL;
    }
    return fileuriindex;
}

void h64debugsymbols_Free(h64debugsymbols *symbols) {
    if (!symbols)
        return;

    free(symbols->mainfile_module_path);
    int i = 0;
    while (i < symbols->global_attribute_count) {
        free(symbols->global_attribute_name[i]);
        i++;
    }
    free(symbols->global_attribute_name);
    i = 0;
    while (i < symbols->module_count) {
        h64debugsymbols_ClearModule(symbols->module_symbols[i]);
        free(symbols->module_symbols[i]);
        i++;
    }
    free(symbols->module_symbols);
    i = 0;
    while (i < symbols->fileuri_count) {
        free(symbols->fileuri[i]);
        i++;
    }
    free(symbols->fileuri);

    if (symbols->func_id_to_module_symbols_index)
        hash_FreeMap(symbols->func_id_to_module_symbols_index);
    if (symbols->func_id_to_module_symbols_func_subindex)
        hash_FreeMap(symbols->func_id_to_module_symbols_func_subindex);
    if (symbols->class_id_to_module_symbols_index)
        hash_FreeMap(symbols->class_id_to_module_symbols_index);
    if (symbols->class_id_to_module_symbols_class_subindex)
        hash_FreeMap(symbols->class_id_to_module_symbols_class_subindex);
    if (symbols->modulelibpath_to_modulesymbol_id)
        hash_FreeMap(symbols->modulelibpath_to_modulesymbol_id);
    if (symbols->attribute_name_to_global_attribute_id)
        hash_FreeMap(symbols->attribute_name_to_global_attribute_id);
    if (symbols->globalvar_id_to_module_symbols_index)
        hash_FreeMap(symbols->globalvar_id_to_module_symbols_index);
    if (symbols->globalvar_id_to_module_symbols_globalvar_subindex)
        hash_FreeMap(
            symbols->globalvar_id_to_module_symbols_globalvar_subindex
        );
    free(symbols);
}

void h64debugsymbols_ClearClassSymbol(
        h64classsymbol *csymbol
        ) {
    if (!csymbol)
        return;
    free(csymbol->_tmp_varattr_expr_ptr);
    free(csymbol->name);
}

void h64debugsymbols_ClearGlobalvarSymbol(
        h64globalvarsymbol *gsymbol
        ) {
    if (!gsymbol)
        return;
    free(gsymbol->name);
}

void h64debugsymbols_ClearFuncSymbol(
        h64funcsymbol *fsymbol
        ) {
    if (!fsymbol)
        return;
    free(fsymbol->name);
    if (fsymbol->arg_kwarg_name) {
        int i = 0;
        while (i < fsymbol->arg_count) {
            free(fsymbol->arg_kwarg_name[i]);
            i++;
        }
    }
    free(fsymbol->arg_kwarg_name);
}

int64_t h64debugsymbols_AttributeNameToAttributeNameId(
        h64debugsymbols *symbols, const char *name,
        int addifnotpresent
        ) {
    if (!name || !symbols)
        return -1;
    uint64_t number = 0;
    if (!hash_StringMapGet(
            symbols->attribute_name_to_global_attribute_id,
            name, &number)) {
        if (addifnotpresent) {
            int64_t new_id = symbols->global_attribute_count;
            char **new_name_list = realloc(
                symbols->global_attribute_name,
                sizeof(*symbols->global_attribute_name) *
                (symbols->global_attribute_count + 1)
            );
            if (!new_name_list)
                return -1;
            symbols->global_attribute_name = new_name_list;
            symbols->global_attribute_name[new_id] = strdup(name);
            if (!symbols->global_attribute_name[new_id])
                return -1;
            symbols->global_attribute_count++;
            if (!hash_StringMapSet(
                    symbols->attribute_name_to_global_attribute_id,
                    name, (uint64_t)new_id)) {
                free(symbols->global_attribute_name[new_id]);
                symbols->global_attribute_count--;
                return -1;
            }
            if (symbols->program) {
                if (strcmp(name, "as_str") == 0) {
                    symbols->program->as_str_name_index = new_id;
                } else if (strcmp(name, "to_str") == 0) {
                    symbols->program->to_str_name_index = new_id;
                } else if (strcmp(name, "len") == 0) {
                    symbols->program->len_name_index = new_id;
                } else if (strcmp(name, "init") == 0) {
                    symbols->program->init_name_index = new_id;
                } else if (strcmp(name, "on_destroy") == 0) {
                    symbols->program->on_destroy_name_index = new_id;
                } else if (strcmp(name, "equals") == 0) {
                    symbols->program->equals_name_index = new_id;
                } else if (strcmp(name, "to_hash") == 0) {
                    symbols->program->to_hash_name_index = new_id;
                } else if (strcmp(name, "add") == 0) {
                    symbols->program->add_name_index = new_id;
                } else if (strcmp(name, "del") == 0) {
                    symbols->program->del_name_index = new_id;
                } else if (strcmp(name, "is_a") == 0) {
                    symbols->program->is_a_name_index = new_id;
                }
            }
            return new_id;
        }
        return -1;
    }
    return (int)number;
}

h64modulesymbols *_h64debugsymbols_GetModuleInternal(
        h64debugsymbols *symbols, const char *modpath,
        const char *library_name,
        int addifnotpresent
        ) {
    if (!symbols)
        return NULL;

    char *modlibpath = malloc(
        (1 + (library_name ? strlen(library_name) : 0) + 1) +
        (modpath ? strlen(modpath) : 0) + 1
    );
    if (!modlibpath)
        return NULL;
    modlibpath[0] = '@';
    if (library_name) {
        memcpy(modlibpath + 1, library_name, strlen(library_name) + 1);
    } else {
        modlibpath[1] = '\0';
    }
    if (modpath)
        memcpy(
            modlibpath + strlen(modlibpath),
            modpath, strlen(modpath) + 1
        );

    assert(symbols->modulelibpath_to_modulesymbol_id != NULL);
    uint64_t number = 0;
    if (!hash_StringMapGet(
            symbols->modulelibpath_to_modulesymbol_id, modlibpath,
            &number)) {
        if (!addifnotpresent) {
            free(modlibpath);
            return NULL;
        }

        h64modulesymbols **new_symbols = realloc(
            symbols->module_symbols,
            sizeof(*new_symbols) * (symbols->module_count + 1)
        );
        if (!new_symbols) {
            free(modlibpath);
            return NULL;
        }
        symbols->module_symbols = new_symbols;
        symbols->module_symbols[symbols->module_count] = malloc(
            sizeof(**new_symbols)
        );
        if (!symbols->module_symbols[symbols->module_count]) {
            free(modlibpath);
            return NULL;
        }

        h64modulesymbols *msymbols = (
            symbols->module_symbols[symbols->module_count]
        );
        memset(msymbols, 0, sizeof(*msymbols));
        msymbols->index = symbols->module_count;

        msymbols->func_name_to_entry = hash_NewStringMap(64);
        if (!msymbols->func_name_to_entry) {
            h64debugsymbols_ClearModule(msymbols);
            free(msymbols);
            free(modlibpath);
            return NULL;
        }

        msymbols->class_name_to_entry = hash_NewStringMap(64);
        if (!msymbols->class_name_to_entry) {
            h64debugsymbols_ClearModule(msymbols);
            free(msymbols);
            free(modlibpath);
            return NULL;
        }

        msymbols->globalvar_name_to_entry = hash_NewStringMap(64);
        if (!msymbols->globalvar_name_to_entry) {
            h64debugsymbols_ClearModule(msymbols);
            free(msymbols);
            free(modlibpath);
            return NULL;
        }

        msymbols->module_path = (
            modpath ? strdup(modpath) : NULL
        );
        if (modpath && !msymbols->module_path) {
            h64debugsymbols_ClearModule(msymbols);
            free(msymbols);
            free(modlibpath);
            return NULL;
        }

        number = (uintptr_t)msymbols;
        if (!hash_StringMapSet(
                symbols->modulelibpath_to_modulesymbol_id,
                modlibpath, number)) {
            h64debugsymbols_ClearModule(msymbols);
            free(msymbols);
            free(modlibpath);
            return NULL;
        }
        free(modlibpath);
        symbols->module_count++;
        return msymbols;
    }
    free(modlibpath);
    assert(number != 0);
    return (h64modulesymbols*)(uintptr_t)number;
}

h64modulesymbols *h64debugsymbols_GetModule(
        h64debugsymbols *symbols, const char *modpath,
        const char *library_name, int addifnotpresent
        ) {
    if (!modpath || strlen(modpath) == 0)
        return NULL;

    return _h64debugsymbols_GetModuleInternal(
        symbols, modpath, library_name, addifnotpresent);
}

h64modulesymbols *h64debugsymbols_GetBuiltinModule(
        h64debugsymbols *symbols
        ) {
    return _h64debugsymbols_GetModuleInternal(
        symbols, NULL, NULL, 1);
}

void h64debugsymbols_ClearModule(h64modulesymbols *msymbols) {
    if (!msymbols)
        return;

    int i = 0;
    while (i < msymbols->func_count) {
        h64debugsymbols_ClearFuncSymbol(&msymbols->func_symbols[i]);
        i++;
    }
    free(msymbols->func_symbols);
    i = 0;
    while (i < msymbols->classes_count) {
        h64debugsymbols_ClearClassSymbol(&msymbols->classes_symbols[i]);
        i++;
    }
    free(msymbols->classes_symbols);
    i = 0;
    while (i < msymbols->globalvar_count) {
        h64debugsymbols_ClearGlobalvarSymbol(
            &msymbols->globalvar_symbols[i]
        );
        i++;
    }
    free(msymbols->globalvar_symbols);

    if (msymbols->func_name_to_entry)
        hash_FreeMap(msymbols->func_name_to_entry);
    if (msymbols->class_name_to_entry)
        hash_FreeMap(msymbols->class_name_to_entry);
    if (msymbols->globalvar_name_to_entry)
        hash_FreeMap(msymbols->globalvar_name_to_entry);

    free(msymbols->module_path);
    free(msymbols->library_name);
}

h64debugsymbols *h64debugsymbols_New() {
    h64debugsymbols *symbols = malloc(sizeof(*symbols));
    if (!symbols)
        return NULL;
    memset(symbols, 0, sizeof(*symbols));

    symbols->modulelibpath_to_modulesymbol_id = hash_NewStringMap(1024);
    if (!symbols->modulelibpath_to_modulesymbol_id) {
        h64debugsymbols_Free(symbols);
        return NULL;
    }

    symbols->attribute_name_to_global_attribute_id = (
        hash_NewStringMap(1024 * 5)
    );
    if (!symbols->attribute_name_to_global_attribute_id) {
        h64debugsymbols_Free(symbols);
        return NULL;
    }

    h64modulesymbols *msymbols = h64debugsymbols_GetBuiltinModule(symbols);
    if (!msymbols) {
        h64debugsymbols_Free(symbols);
        return NULL;
    }

    symbols->func_id_to_module_symbols_index = hash_NewIntMap(
        1024 * 5
    );
    if (!symbols->func_id_to_module_symbols_index) {
        h64debugsymbols_Free(symbols);
        return NULL;
    }
    symbols->func_id_to_module_symbols_func_subindex = hash_NewIntMap(
        1024 * 5
    );
    if (!symbols->func_id_to_module_symbols_func_subindex) {
        h64debugsymbols_Free(symbols);
        return NULL;
    }

    symbols->class_id_to_module_symbols_index = hash_NewIntMap(
        1024 * 5
    );
    if (!symbols->class_id_to_module_symbols_index) {
        h64debugsymbols_Free(symbols);
        return NULL;
    }
    symbols->class_id_to_module_symbols_class_subindex = hash_NewIntMap(
        1024 * 5
    );
    if (!symbols->class_id_to_module_symbols_class_subindex) {
        h64debugsymbols_Free(symbols);
        return NULL;
    }

    symbols->globalvar_id_to_module_symbols_index = hash_NewIntMap(
        1024 * 5
    );
    if (!symbols->globalvar_id_to_module_symbols_index) {
        h64debugsymbols_Free(symbols);
        return NULL;
    }
    symbols->globalvar_id_to_module_symbols_globalvar_subindex = (
        hash_NewIntMap(1024 * 5)
        );
    if (!symbols->globalvar_id_to_module_symbols_globalvar_subindex) {
        h64debugsymbols_Free(symbols);
        return NULL;
    }

    return symbols;
}

h64classsymbol *h64debugsymbols_GetClassSymbolById(
        h64debugsymbols *symbols, int64_t classid
        ) {
    uint64_t msymbols_index = 0;
    if (!hash_IntMapGet(
            symbols->class_id_to_module_symbols_index,
            classid, &msymbols_index)) {
        return NULL;
    }
    assert((int)msymbols_index < symbols->module_count);
    uint64_t msymbols_classindex = 0;
    if (!hash_IntMapGet(
            symbols->class_id_to_module_symbols_class_subindex,
            classid, &msymbols_classindex)) {
        return NULL;
    }
    assert(
        (int)msymbols_classindex < symbols->module_symbols[
            msymbols_index
        ]->classes_count
    );
    return &symbols->module_symbols[
        msymbols_index
    ]->classes_symbols[msymbols_classindex];
}

h64modulesymbols *h64debugsymbols_GetModuleSymbolsByFuncId(
        h64debugsymbols *symbols, int64_t funcid
        ) {
    uint64_t msymbols_index = 0;
    if (!hash_IntMapGet(
            symbols->func_id_to_module_symbols_index,
            funcid, &msymbols_index)) {
        return NULL;
    }
    assert((int)msymbols_index < symbols->module_count);
    return symbols->module_symbols[
        msymbols_index
    ];
}

h64modulesymbols *h64debugsymbols_GetModuleSymbolsByClassId(
        h64debugsymbols *symbols, int64_t classid
        ) {
    uint64_t msymbols_index = 0;
    if (!hash_IntMapGet(
            symbols->class_id_to_module_symbols_index,
            classid, &msymbols_index)) {
        return NULL;
    }
    assert((int)msymbols_index < symbols->module_count);
    return symbols->module_symbols[
        msymbols_index
    ];
}

h64funcsymbol *h64debugsymbols_GetFuncSymbolById(
        h64debugsymbols *symbols, int64_t funcid
        ) {
    uint64_t msymbols_index = 0;
    if (!hash_IntMapGet(
            symbols->func_id_to_module_symbols_index,
            funcid, &msymbols_index)) {
        return NULL;
    }
    assert((int)msymbols_index < symbols->module_count);
    uint64_t msymbols_funcindex = 0;
    if (!hash_IntMapGet(
            symbols->func_id_to_module_symbols_func_subindex,
            funcid, &msymbols_funcindex)) {
        return NULL;
    }
    assert(
        (int)msymbols_funcindex < symbols->module_symbols[
            msymbols_index
        ]->func_count
    );
    return &symbols->module_symbols[
        msymbols_index
    ]->func_symbols[msymbols_funcindex];
}

h64globalvarsymbol *h64debugsymbols_GetGlobalvarSymbolById(
        h64debugsymbols *symbols, int64_t globalid
        ) {
    uint64_t msymbols_index = 0;
    if (!hash_IntMapGet(
            symbols->globalvar_id_to_module_symbols_index,
            globalid, &msymbols_index)) {
        return NULL;
    }
    assert((int)msymbols_index < symbols->module_count);
    uint64_t msymbols_gvarindex = 0;
    if (!hash_IntMapGet(
            symbols->globalvar_id_to_module_symbols_globalvar_subindex,
            globalid, &msymbols_gvarindex)) {
        return NULL;
    }
    assert(
        (int)msymbols_gvarindex < symbols->module_symbols[
            msymbols_index
        ]->globalvar_count
    );
    return &symbols->module_symbols[
        msymbols_index
    ]->globalvar_symbols[msymbols_gvarindex];
}