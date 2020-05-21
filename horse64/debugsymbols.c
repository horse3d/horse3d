
#include <stdlib.h>
#include <string.h>

#include "bytecode.h"
#include "debugsymbols.h"
#include "hash.h"


void h64debugsymbols_Free(h64debugsymbols *symbols) {
    if (!symbols)
        return;

    int i = 0;
    while (i < symbols->func_count) {
        h64debugsymbols_ClearFuncSymbol(&symbols->func_symbols[i]);
        i++;
    }
    free(symbols->func_symbols);
    i = 0;
    while (i < symbols->classes_count) {
        h64debugsymbols_ClearClassSymbol(&symbols->classes_symbols[i]);
        i++;
    }
    free(symbols->classes_symbols);
    i = 0;
    while (i < symbols->global_member_count) {
        free(symbols->global_member_name[i]);
        i++;
    }
    free(symbols->global_member_name);

    if (symbols->func_namepath_to_func_id)
        hash_FreeMap(symbols->func_namepath_to_func_id);
    if (symbols->class_namepath_to_class_id)
        hash_FreeMap(symbols->class_namepath_to_class_id);
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

h64debugsymbols *h64debugsymbols_New() {
    h64debugsymbols *symbols = malloc(sizeof(*symbols));
    if (!symbols)
        return NULL;
    memset(symbols, 0, sizeof(*symbols));

    symbols->func_namepath_to_func_id = hash_NewStringMap(1024 * 5);
    if (!symbols->func_namepath_to_func_id) {
        h64debugsymbols_Free(symbols);
        return NULL;
    }

    symbols->class_namepath_to_class_id = hash_NewStringMap(1024 * 5);
    if (!symbols->class_namepath_to_class_id) {
        h64debugsymbols_Free(symbols);
        return NULL;
    }

    symbols->member_name_to_global_member_id = hash_NewStringMap(1024 * 5);
    if (!symbols->member_name_to_global_member_id) {
        h64debugsymbols_Free(symbols);
        return NULL;
    }

    return symbols;
}
