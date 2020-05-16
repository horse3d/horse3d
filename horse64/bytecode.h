#ifndef HORSE64_BYTECODE_H_
#define HORSE64_BYTECODE_H_

#include <stdint.h>

typedef struct h64debugsymbols h64debugsymbols;

typedef enum instructiontype {
    H64INST_INVALID = 0,
    H64INST_STACKGROW = 1,
    H64INST_STACKSETCONST = 2
} instructiontype;

typedef enum valuetype {
    H64VALTYPE_INVALID = 0,
    H64VALTYPE_INT64 = 1,
    H64VALTYPE_FLOAT64 = 2,
    H64VALTYPE_CFUNCREF = 3,
    H64VALTYPE_EMPTYARG = 4
} valuetype;

typedef struct valuecontent {
    uint8_t valuetype;
    union {
        int64_t int_value;
        double float_value;
        char shortstr_value[16];
        char *str_value;
        void *ptr_value;
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

    int iscfunc;

    int associated_class_index;

    union {
        struct {
            int instruction_count;
            h64instruction *instruction;
        };
        struct {
            void *cfunc_ptr;
        };
    };
} h64func;

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

typedef struct h64vmthread h64vmthread;

int h64program_RegisterCFunction(
    h64program *p,
    const char *name,
    int (*func)(h64vmthread *vmthread),
    const char *fileuri,
    int arg_count,
    char **arg_kwarg_name,
    int last_is_multiarg,
    const char *module_path,
    int associated_class_name
);

int h64program_AddClass(
    h64program *p,
    const char *name,
    int (*func)(h64vmthread *vmthread),
    const char *fileuri,
    const char *module_path
);

void h64program_Free(h64program *p);

#endif  // HORSE64_BYTECODE_H_
