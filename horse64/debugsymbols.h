#ifndef HORSE64_DEBUGSYMBOLS_H_
#define HORSE64_DEBUGSYMBOLS_H_

#include <stdint.h>

typedef struct hashmap hashmap;

typedef struct h64funcsymbol {
    char *name;
    char *modulepath, *libraryname;
    int arg_count;
    char **arg_kwarg_name;
    int fileuri_index;
    int instruction_count;
    int64_t *instruction_to_line;
    int64_t *instruction_to_column;
} h64funcsymbol;

typedef struct h64classsymbol {
    char *name;
    char *modulepath, *libraryname;
    int fileuri_index;
} h64classsymbol;

typedef struct h64modulesymbols {
    char *module_path;

    hashmap *func_name_to_func_id;
    int func_count;
    h64funcsymbol *func_symbols;

    hashmap *class_name_to_class_id;
    int classes_count;
    h64classsymbol *classes_symbols;
} h64modulesymbols;

typedef struct h64debugsymbols {
    int fileuri_count;
    char **fileuri;

    hashmap *modulepath_to_modulesymbol_id;
    int module_count;
    h64modulesymbols *module_symbols;

    hashmap *member_name_to_global_member_id;
    int64_t global_member_count;
    char **global_member_name;
} h64debugsymbols;

int64_t h64debugsymbols_MemberNameToMemberNameId(
    h64debugsymbols *symbols, const char *name,
    int addifnotpresent
);

void h64debugsymbols_ClearFuncSymbol(
    h64funcsymbol *fsymbol
);

void h64debugsymbols_ClearClassSymbol(
    h64classsymbol *csymbol
);

h64modulesymbols *h64debugsymbols_GetModule(
    h64debugsymbols *symbols, const char *modpath,
    int addifnotpresent
);

h64modulesymbols *h64debugsymbols_GetBuiltinModule(
    h64debugsymbols *symbols
);

void h64debugsymbols_ClearModule(h64modulesymbols *msymbols);

void h64debugsymbols_Free(h64debugsymbols *symbols);

h64debugsymbols *h64debugsymbols_New();


#endif  // HORSE64_DEBUGSYMBOLS_H_
