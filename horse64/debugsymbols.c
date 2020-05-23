
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "bytecode.h"
#include "debugsymbols.h"
#include "hash.h"


void h64debugsymbols_Free(h64debugsymbols *symbols) {
    if (!symbols)
        return;

    int i = 0;
    while (i < symbols->global_member_count) {
        free(symbols->global_member_name[i]);
        i++;
    }
    free(symbols->global_member_name);
    i = 0;
    while (i < symbols->module_count) {
        h64debugsymbols_ClearModule(&symbols->module_symbols[i]);
        i++;
    }
    free(symbols->module_symbols);
    i = 0;
    while (i < symbols->fileuri_count) {
        free(symbols->fileuri[i]);
        i++;
    }
    free(symbols->fileuri);

    if (symbols->modulepath_to_modulesymbol_id)
        hash_FreeMap(symbols->modulepath_to_modulesymbol_id);
    if (symbols->member_name_to_global_member_id)
        hash_FreeMap(symbols->member_name_to_global_member_id);
    free(symbols);
}

void h64debugsymbols_ClearClassSymbol(
        h64classsymbol *csymbol
        ) {
    if (!csymbol)
        return;
    free(csymbol->name);
    free(csymbol->modulepath);
    free(csymbol->libraryname);
}

void h64debugsymbols_ClearFuncSymbol(
        h64funcsymbol *fsymbol
        ) {
    if (!fsymbol)
        return;
    free(fsymbol->name);
    free(fsymbol->modulepath);
    free(fsymbol->libraryname);
    if (fsymbol->arg_kwarg_name) {
        int i = 0;
        while (i < fsymbol->arg_count) {
            free(fsymbol->arg_kwarg_name[i]);
            i++;
        }
    }
    free(fsymbol->arg_kwarg_name);
}

int64_t h64debugsymbols_MemberNameToMemberNameId(
        h64debugsymbols *symbols, const char *name,
        int addifnotpresent
        ) {
    if (!name || !symbols)
        return -1;
    uint64_t number = 0;
    if (!hash_StringMapGet(
            symbols->member_name_to_global_member_id,
            name, &number)) {
        if (addifnotpresent) {
            int64_t new_id = symbols->global_member_count;
            char **new_name_list = realloc(
                symbols->global_member_name,
                sizeof(*symbols->global_member_name) *
                (symbols->global_member_count + 1)
            );
            if (!new_name_list)
                return -1;
            symbols->global_member_name = new_name_list;
            symbols->global_member_name[new_id] = strdup(name);
            if (!symbols->global_member_name[new_id])
                return -1;
            symbols->global_member_count++;
            if (!hash_StringMapSet(
                    symbols->member_name_to_global_member_id,
                    name, (uint64_t)new_id)) {
                free(symbols->global_member_name[new_id]);
                symbols->global_member_count--;
                return -1;
            }
            return new_id;
        }
        return -1;
    }
    return (int)number;
}

h64modulesymbols *_h64debugsymbols_GetModuleInternal(
        h64debugsymbols *symbols, const char *modpath,
        int addifnotpresent
        ) {
    if (!symbols)
        return NULL;

    const char *emptystr = "";
    if (!modpath)
        modpath = emptystr;

    assert(symbols->modulepath_to_modulesymbol_id != NULL);
    uint64_t number = 0;
    if (!hash_StringMapGet(
            symbols->modulepath_to_modulesymbol_id, modpath,
            &number)) {
        if (!addifnotpresent)
            return NULL;

        h64modulesymbols *new_symbols = realloc(
            symbols->module_symbols,
            sizeof(*new_symbols) * (symbols->module_count + 1)
        );
        if (!new_symbols)
            return NULL;
        symbols->module_symbols = new_symbols;

        h64modulesymbols *msymbols = &(
            symbols->module_symbols[symbols->module_count]
        );
        memset(msymbols, 0, sizeof(*msymbols));

        msymbols->func_name_to_func_id = hash_NewStringMap(64);
        if (!msymbols->func_name_to_func_id) {
            h64debugsymbols_ClearModule(msymbols);
            return NULL;
        }

        msymbols->class_name_to_class_id = hash_NewStringMap(64);
        if (!msymbols->class_name_to_class_id) {
            h64debugsymbols_ClearModule(msymbols);
            return NULL;
        }

        msymbols->module_path = strdup(modpath);
        if (!msymbols->module_path) {
            h64debugsymbols_ClearModule(msymbols);
            return NULL;
        }

        number = (uintptr_t)msymbols;
        if (!hash_StringMapSet(
                symbols->modulepath_to_modulesymbol_id, modpath,
                number)) {
            h64debugsymbols_ClearModule(msymbols);
            return NULL;
        }
        symbols->module_count++;
        return msymbols;
    }
    assert(number != 0);
    return (h64modulesymbols*)(uintptr_t)number;
}

h64modulesymbols *h64debugsymbols_GetModule(
        h64debugsymbols *symbols, const char *modpath,
        int addifnotpresent
        ) {
    if (!modpath || strlen(modpath) == 0)
        return NULL;

    return _h64debugsymbols_GetModuleInternal(
        symbols, modpath, addifnotpresent);
}

h64modulesymbols *h64debugsymbols_GetBuiltinModule(
        h64debugsymbols *symbols
        ) {
    return _h64debugsymbols_GetModuleInternal(
        symbols, NULL, 1);
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

    if (msymbols->func_name_to_func_id)
        hash_FreeMap(msymbols->func_name_to_func_id);
    if (msymbols->class_name_to_class_id)
        hash_FreeMap(msymbols->class_name_to_class_id);

    free(msymbols->module_path);
}

h64debugsymbols *h64debugsymbols_New() {
    h64debugsymbols *symbols = malloc(sizeof(*symbols));
    if (!symbols)
        return NULL;
    memset(symbols, 0, sizeof(*symbols));

    symbols->modulepath_to_modulesymbol_id = hash_NewStringMap(1024);
    if (!symbols->modulepath_to_modulesymbol_id) {
        h64debugsymbols_Free(symbols);
        return NULL;
    }

    symbols->member_name_to_global_member_id = hash_NewStringMap(1024 * 5);
    if (!symbols->member_name_to_global_member_id) {
        h64debugsymbols_Free(symbols);
        return NULL;
    }

    h64modulesymbols *msymbols = h64debugsymbols_GetBuiltinModule(symbols);
    if (!msymbols) {
        h64debugsymbols_Free(symbols);
        return NULL;
    }

    return symbols;
}
