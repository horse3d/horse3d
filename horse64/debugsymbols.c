
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

    if (symbols->func_name_to_id)
        hash_FreeMap(symbols->func_name_to_id);
    if (symbols->class_name_to_id) 
        hash_FreeMap(symbols->class_name_to_id);
    free(symbols);
}

void h64debugsymbols_ClearClassSymbol(
        h64classsymbol *csymbol
        ) {
    if (!csymbol)
        return;
    free(csymbol->name);
    free(csymbol->modulepath);
}
void h64debugsymbols_ClearFuncSymbol(
        h64funcsymbol *fsymbol
        ) {
    if (!fsymbol)
        return;
    free(fsymbol->name);
    free(fsymbol->modulepath);
    if (fsymbol->arg_kwarg_name) {
        int i = 0;
        while (i < fsymbol->arg_count) {
            free(fsymbol->arg_kwarg_name[i]);
            i++;
        }
    }
    free(fsymbol->arg_kwarg_name);
}

h64debugsymbols *h64debugsymbols_New() {
    h64debugsymbols *symbols = malloc(sizeof(*symbols));
    if (!symbols)
        return NULL;
    memset(symbols, 0, sizeof(*symbols));

    symbols->func_name_to_id = hash_NewStringMap(1024 * 5);
    if (!symbols->func_name_to_id) {
        h64debugsymbols_Free(symbols);
        return NULL;
    }

    symbols->class_name_to_id = hash_NewStringMap(1024 * 5);
    if (!symbols->class_name_to_id) {
        h64debugsymbols_Free(symbols);
        return NULL;
    }

    return symbols;
}
