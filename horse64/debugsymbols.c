
#include <stdlib.h>
#include <string.h>

#include "bytecode.h"
#include "debugsymbols.h"
#include "hash.h"

void h64debugsymbols_Free(h64debugsymbols *symbols) {
    if (!symbols)
        return;

    if (symbols->func_name_to_id)
        hash_FreeMap(symbols->func_name_to_id);
    if (symbols->class_name_to_id) 
        hash_FreeMap(symbols->class_name_to_id);
    free(symbols);
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
