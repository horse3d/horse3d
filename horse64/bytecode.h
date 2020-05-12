#ifndef HORSE64_BYTECODE_H_
#define HORSE64_BYTECODE_H_

#include <stdint.h>

typedef enum instructiontype {
    H64INST_INVALID = 0,
    H64INST_STACKGROW = 1,
    H64INST_STACKSETCONST = 2
} instructiontype;

typedef enum valuetype {
    H64VALTYPE_INVALID = 0,
    H64VALTYPE_INT64 = 1,
    H64VALTYPE_FLOAT64 = 2,
    H64VALTYPE_CFUNCREF = 3
} valuetype;

typedef struct valuecontent {
    uint8_t valuetype;
    union {
        int64_t int_value;
        double float_value;
    };
} valuecontent;

typedef struct h64instruction {
    uint16_t type;
    union {
        struct stackgrow {
            int size;
        } stackgrow;
        struct stacksetconst {
            valuecontent content;
        } stacksetconst;
    };
} h64instruction;

typedef struct h64class {

} h64class;

typedef struct h64func {
    int arg_count;
    int last_is_multiarg;

    int stack_slots_used;

    int instruction_count;
    h64instruction *instruction;
} h64func;

typedef struct h64debugsymbols {
    int fileuri_count;
    char **fileuri;

    int func_count;
    char **func_name;
    int *func_to_fileuri_index;
    int *func_instruction_count;
    int64_t **func_instruction_to_line;
    int64_t **func_instruction_to_column;
} h64debugsymbols;

typedef struct h64program {
    int globals_count;
    valuecontent *globals;

    int classes_count;
    h64class *classes;

    int func_count;
    h64func *func;

    h64debugsymbols *symbols;
} h64program;

h64program *h64program_New();

#endif  // HORSE64_BYTECODE_H_
