#ifndef HORSE64_DEBUGSYMBOLS_H_
#define HORSE64_DEBUGSYMBOLS_H_

#include <stdint.h>

typedef struct hashmap hashmap;

typedef struct h64funcsymbol {
    char *name;
    char *modulepath;
    int arg_count;
    char **arg_kwarg_name;
    int fileuri_index;
    int instruction_count;
    int64_t *instruction_to_line;
    int64_t *instruction_to_column;
} h64funcsymbol;

typedef struct h64classsymbol {
    char *name;
    char *modulemath;
} h64classsymbol;

typedef struct h64debugsymbols {
    int fileuri_count;
    char **fileuri;

    hashmap *func_name_to_id;
    int func_count;
    h64funcsymbol *func_symbols;

    hashmap *class_name_to_id;
    int classes_count;
    h64classsymbol *classes_symbols;
} h64debugsymbols;

void h64debugsymbols_ClearFuncSymbol(
    h64funcsymbol *fsymbol
);

void h64debugsymbols_Free(h64debugsymbols *symbols);

h64debugsymbols *h64debugsymbols_New();


#endif  // HORSE64_DEBUGSYMBOLS_H_
