#ifndef HORSE64_BYTECODE_H_
#define HORSE64_BYTECODE_H_

#include <limits.h>
#include <stdint.h>

#include "gcvalue.h"

typedef struct h64debugsymbols h64debugsymbols;
typedef uint32_t unicodechar;

typedef enum instructiontype {
    H64INST_INVALID = 0,
    H64INST_STACKSETCONST = 1,
    H64INST_GLOBALSETCONST
} instructiontype;

typedef enum storagetype {
    H64STORETYPE_INVALID = 0,
    H64STORETYPE_STACKSLOT = 1,
    H64STORETYPE_GLOBALFUNCSLOT,
    H64STORETYPE_GLOBALCLASSSLOT,
    H64STORETYPE_GLOBALVARSLOT,
    H64STORETYPE_TOTAL_COUNT
} storagetype;

typedef struct storageref {
    uint8_t type;
    int64_t id;
} storageref;

typedef enum valuetype {
    H64VALTYPE_INVALID = 0,
    H64VALTYPE_INT64 = 1,
    H64VALTYPE_FLOAT64,
    H64VALTYPE_CFUNCREF,
    H64VALTYPE_SIMPLEFUNCREF,
    H64VALTYPE_CLOSUREFUNCREF,
    H64VALTYPE_EMPTYARG,
    H64VALTYPE_ERROR,
    H64VALTYPE_GCVAL,
    H64VALTYPE_SHORTSTR
} valuetype;

typedef struct valuecontent {
    uint8_t type;
    union {
        int64_t int_value;
        double float_value;
        void *ptr_value;
        struct {
            unicodechar shortstr_value[3];
            uint8_t shortstr_len;
        };
    };
} valuecontent;

typedef struct h64instructionany {
    uint8_t type;
} __attribute__((packed))h64instructionany;

typedef struct h64instruction_stacksetconst {
    uint8_t type;
    int64_t slot;
    valuecontent content;
} __attribute__((packed)) stacksetconst;

typedef struct h64instruction_globalsetconst {
    uint8_t type;
    int64_t slot;
    valuecontent content;
} __attribute__((packed)) h64instruction_globalsetconst;


#define H64CLASS_HASH_SIZE 16
#define H64CLASS_MAX_METHODS (INT_MAX / 4)

typedef struct h64classmemberinfo {
    int64_t nameid;
    int methodorvaridx;  // vars have H64CLASS_MAX_METHODS offset
} h64classmemberinfo;

typedef struct h64class {
    int methods_count;
    int64_t *method_global_name_idx;
    int64_t *method_func_idx;

    int vars_count;
    int64_t *vars_global_name_idx;

    h64classmemberinfo **global_name_to_member_hashmap;

    int hasvarinitfunc;
} h64class;

typedef struct h64func {
    int arg_count;
    int last_is_multiarg;

    int stack_slots_used;

    int iscfunc, is_threadable;

    int associated_class_index;

    union {
        struct {
            int instructions_bytes;
            h64instructionany *instructions;
        };
        struct {
            void *cfunc_ptr;
        };
    };
} h64func;

typedef struct h64globalvar {
    valuecontent content;
} h64globalvar;

typedef struct h64program {
    int globals_count;
    valuecontent *globals;

    int64_t classes_count;
    h64class *classes;

    int64_t func_count;
    h64func *func;
    int main_func_index;

    int64_t globalvar_count;
    h64globalvar *globalvar;

    h64debugsymbols *symbols;
} h64program;

h64program *h64program_New();

typedef struct h64vmthread h64vmthread;

static inline void h64program_ClearValueContent(
        valuecontent *content, int referencedfromnonheap
        ) {
    if (content->type == H64VALTYPE_GCVAL) {
        if (referencedfromnonheap) {
            ((h64gcvalue *)content->ptr_value)->externalreferencecount--;
        } else {
            ((h64gcvalue *)content->ptr_value)->heapreferencecount--;
        }
    }
}

void h64program_LookupClassMember(
    h64program *p, int64_t class_id, int64_t nameid,
    int *out_membervarid, int *out_memberfuncid
);

int h64program_RegisterClassVariable(
    h64program *p,
    int64_t class_id,
    const char *name
);

int h64program_RegisterCFunction(
    h64program *p,
    const char *name,
    int (*func)(h64vmthread *vmthread),
    const char *fileuri,
    int arg_count,
    char **arg_kwarg_name,
    int last_is_multiarg,
    const char *module_path,
    const char *library_name,
    int is_threadable,
    int associated_class_idx
);

int h64program_RegisterHorse64Function(
    h64program *p,
    const char *name,
    const char *fileuri,
    int arg_count,
    char **arg_kwarg_name,
    int last_posarg_is_multiarg,
    const char *module_path,
    const char *library_name,
    int associated_class_idx
);

int h64program_AddClass(
    h64program *p,
    const char *name,
    const char *fileuri,
    const char *module_path,
    const char *library_name
);

int h64program_AddGlobalvar(
    h64program *p,
    const char *name,
    int is_const,
    const char *fileuri,
    const char *module_path,
    const char *library_name
);

void h64program_Free(h64program *p);

void h64program_PrintBytecodeStats(h64program *p);

#endif  // HORSE64_BYTECODE_H_
