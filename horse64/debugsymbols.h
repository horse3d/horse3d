#ifndef HORSE64_DEBUGSYMBOLS_H_
#define HORSE64_DEBUGSYMBOLS_H_

#include <stdint.h>

typedef struct hashmap hashmap;

typedef struct h64debugsymbols {
    int fileuri_count;
    char **fileuri;

    hashmap *func_name_to_id;
    int func_count;
    char **func_name;
    char **func_modulepath;
    int *func_arg_count;
    char ***func_arg_kwarg_name;
    int *func_to_fileuri_index;
    int *func_instruction_count;
    int64_t **func_instruction_to_line;
    int64_t **func_instruction_to_column;

    hashmap *class_name_to_id;
    int classes_count;
    char **classes_name;
    char **classes_modulepath;
} h64debugsymbols;

void h64debugsymbols_Free(h64debugsymbols *symbols);

h64debugsymbols *h64debugsymbols_New();


#endif  // HORSE64_DEBUGSYMBOLS_H_
