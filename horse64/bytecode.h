// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_BYTECODE_H_
#define HORSE64_BYTECODE_H_

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

#include "gcvalue.h"

#define MAX_EXCEPTION_STACK_FRAMES 10

typedef struct h64debugsymbols h64debugsymbols;
typedef uint32_t unicodechar;

typedef enum instructiontype {
    H64INST_INVALID = 0,
    H64INST_SETCONST = 1,
    H64INST_SETGLOBAL,
    H64INST_GETGLOBAL,
    H64INST_GETFUNC,
    H64INST_GETCLASS,
    H64INST_VALUECOPY,
    H64INST_BINOP,
    H64INST_UNOP,
    H64INST_CALL,
    H64INST_SETTOP,
    H64INST_RETURNVALUE,
    H64INST_JUMPTARGET,
    H64INST_CONDJUMP,
    H64INST_JUMP,
    H64INST_NEWITERATOR,
    H64INST_ITERATE,
    H64INST_PUSHCATCHFRAME,
    H64INST_ADDCATCHTYPEBYREF,
    H64INST_ADDCATCHTYPE,
    H64INST_POPCATCHFRAME,
    H64INST_GETMEMBER,
    H64INST_JUMPTOFINALLY,
    H64INST_NEWLIST,
    H64INST_ADDTOLIST,
    H64INST_NEWSET,
    H64INST_ADDTOSET,
    H64INST_NEWMAP,
    H64INST_PUTMAP,
    H64INST_NEWVECTOR,
    H64INST_PUTVECTOR,
    H64INST_TOTAL_COUNT
} instructiontype;

const char *bytecode_InstructionTypeToStr(instructiontype itype);

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

typedef struct h64exceptioninfo {
    int stack_frame_count;
    int64_t stack_frame_funcid[MAX_EXCEPTION_STACK_FRAMES];
    int64_t stack_frame_byteoffset[MAX_EXCEPTION_STACK_FRAMES];
    char *msg;
    int64_t exception_class_id;
} h64exceptioninfo;

typedef enum valuetype {
    H64VALTYPE_INVALID = 0,
    H64VALTYPE_INT64 = 1,
    H64VALTYPE_FLOAT64,
    H64VALTYPE_BOOL,
    H64VALTYPE_NONE,
    H64VALTYPE_CFUNCREF,
    H64VALTYPE_CLASSREF,
    H64VALTYPE_SIMPLEFUNCREF,
    H64VALTYPE_CLOSUREFUNCREF,
    H64VALTYPE_EMPTYARG,
    H64VALTYPE_EXCEPTION,
    H64VALTYPE_GCVAL,
    H64VALTYPE_SHORTSTR,
    H64VALTYPE_CONSTPREALLOCSTR,
    H64VALTYPE_UNSPECIFIED_KWARG,
} valuetype;

#define VALUECONTENT_SHORTSTRLEN 2

typedef struct valuecontent {
    uint8_t type;
    union {
        int64_t int_value;
        double float_value;
        void *ptr_value;
        struct {
            unicodechar shortstr_value[
                VALUECONTENT_SHORTSTRLEN + 1
            ];
            uint8_t shortstr_len;
        };
        struct {
            unicodechar *constpreallocstr_value;
            int64_t constpreallocstr_len;
        };
        struct {
            int64_t exception_class_id;
            h64exceptioninfo *einfo;
        };
    };
} __attribute__((packed)) valuecontent;

typedef struct h64instructionany {
    uint8_t type;
} __attribute__((packed)) h64instructionany;

typedef struct h64instruction_setconst {
    uint8_t type;
    int16_t slot;
    valuecontent content;
} __attribute__((packed)) h64instruction_setconst;

typedef struct h64instruction_setglobal {
    uint8_t type;
    int64_t globalto;
    int16_t slotfrom;
} __attribute__((packed)) h64instruction_setglobal;

typedef struct h64instruction_getglobal {
    uint8_t type;
    int16_t slotto;
    int64_t globalfrom;
} __attribute__((packed)) h64instruction_getglobal;

typedef struct h64instruction_getfunc {
    uint8_t type;
    int16_t slotto;
    int64_t funcfrom;
} __attribute__((packed)) h64instruction_getfunc;

typedef struct h64instruction_getclass {
    uint8_t type;
    int16_t slotto;
    int64_t classfrom;
} __attribute__((packed)) h64instruction_getclass;

typedef struct h64instruction_valuecopy {
    uint8_t type;
    int16_t slotto, slotfrom;
} __attribute__((packed)) h64instruction_valuecopy;

typedef struct h64instruction_binop {
    uint8_t type;
    uint8_t optype;
    int16_t slotto, arg1slotfrom, arg2slotfrom;
} __attribute__((packed)) h64instruction_binop;

typedef struct h64instruction_unop {
    uint8_t type;
    uint8_t optype;
    int16_t slotto, argslotfrom;
} __attribute__((packed)) h64instruction_unop;

typedef struct h64instruction_call {
    uint8_t type;
    int16_t returnto, slotcalledfrom;
    uint8_t expandlastposarg;
    int16_t posargs, kwargs;
} __attribute__((packed)) h64instruction_call;

typedef struct h64instruction_settop {
    uint8_t type;
    int16_t topto;
} __attribute__ ((packed)) h64instruction_settop;

typedef struct h64instruction_returnvalue {
    uint8_t type;
    int16_t returnslotfrom;
} __attribute__ ((packed)) h64instruction_returnvalue;

typedef struct h64instruction_jumptarget {
    uint8_t type;
    int32_t jumpid;
} __attribute__ ((packed)) h64instruction_jumptarget;

typedef struct h64instruction_condjump {
    uint8_t type;
    int32_t jumpbytesoffset;
    int16_t conditionalslot;
} __attribute__ ((packed)) h64instruction_condjump;

typedef struct h64instruction_jump {
    uint8_t type;
    int32_t jumpbytesoffset;
} __attribute__ ((packed)) h64instruction_jump;

typedef struct h64instruction_newiterator {
    uint8_t type;
    int16_t slotiteratorto, slotcontainerfrom;
} __attribute__ ((packed)) h64instruction_newiterator;

typedef struct h64instruction_iterate {
    uint8_t type;
    int16_t slotvalueto, slotiteratorfrom, jumponend;
} __attribute__ ((packed)) h64instruction_iterate;

#define CATCHMODE_JUMPONCATCH 1
#define CATCHMODE_JUMPONFINALLY 2

typedef struct h64instruction_pushcatchframe {
    uint8_t type;
    uint8_t mode;
    int16_t slotexceptionto, jumponcatch, jumponfinally;
} __attribute__ ((packed)) h64instruction_pushcatchframe;

typedef struct h64instruction_addcatchtypebyref {
    uint8_t type;
    int16_t slotfrom;
} __attribute__ ((packed)) h64instruction_addcatchtypebyref;

typedef struct h64instruction_addcatchtype {
    uint8_t type;
    int64_t classid;
} __attribute__ ((packed)) h64instruction_addcatchtype;

typedef struct h64instruction_popcatchframe {
    uint8_t type;
} __attribute__ ((packed)) h64instruction_popcatchframe;

typedef struct h64instruction_getmember {
    uint8_t type;
    int16_t slotto;
    int16_t objslotfrom;
    int64_t nameidx;
} __attribute__ ((packed)) h64instruction_getmember;

typedef struct h64instruction_jumptofinally {
    uint8_t type;
} __attribute__ ((packed)) h64instruction_jumptofinally;

typedef struct h64instruction_newlist {
    uint8_t type;
    int16_t slotto;
} __attribute__ ((packed)) h64instruction_newlist;

typedef struct h64instruction_addtolist {
    uint8_t type;
    int16_t slotlistto;
    int16_t slotaddfrom;
} __attribute__ ((packed)) h64instruction_addtolist;

typedef struct h64instruction_newset {
    uint8_t type;
    int16_t slotto;
} __attribute__ ((packed)) h64instruction_newset;

typedef struct h64instruction_addtoset {
    uint8_t type;
    int16_t slotsetto;
    int16_t slotaddfrom;
} __attribute__ ((packed)) h64instruction_addtoset;

typedef struct h64instruction_newvector {
    uint8_t type;
    int16_t slotto;
} __attribute__ ((packed)) h64instruction_newvector;

typedef struct h64instruction_putvector {
    uint8_t type;
    int16_t slotvectorto;
    int64_t putindex;
    int16_t slotputfrom;
} __attribute__ ((packed)) h64instruction_putvector;

typedef struct h64instruction_newmap {
    uint8_t type;
    int16_t slotto;
} __attribute__ ((packed)) h64instruction_newmap;

typedef struct h64instruction_putmap {
    uint8_t type;
    int16_t slotmapto;
    int16_t slotputkeyfrom;
    int16_t slotputvaluefrom;
} __attribute__ ((packed)) h64instruction_putmap;


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
    int base_class_global_id;

    int vars_count;
    int64_t *vars_global_name_idx;

    h64classmemberinfo **global_name_to_member_hashmap;

    int hasvarinitfunc;
} h64class;

typedef struct h64func {
    int input_stack_size, inner_stack_size;
    int iscfunc, is_threadable;

    char *cfunclookup;  // path to identify C extension func

    int associated_class_index;

    union {
        struct {
            int instructions_bytes;
            char *instructions;
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
    int64_t globals_count;
    valuecontent *globals;

    int64_t classes_count;
    h64class *classes;

    int64_t func_count;
    h64func *func;

    int64_t main_func_index;
    int64_t globalinit_func_index;
    int64_t to_str_name_index;
    int64_t length_name_index;
    int64_t init_name_index;
    int64_t destroy_name_index;
    int64_t clone_name_index;
    int64_t equals_name_index;
    int64_t hash_name_index;

    int64_t globalvar_count;
    h64globalvar *globalvar;

    h64debugsymbols *symbols;
} h64program;

h64program *h64program_New();

typedef struct h64vmthread h64vmthread;

size_t h64program_PtrToInstructionSize(
    char *ptr
);

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

void valuecontent_Free(valuecontent *content);

void h64program_FreeInstructions(
    char *instructionbytes, int instructionbytes_len
);

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

int bytecode_fileuriindex(h64program *p, const char *fileuri);

#endif  // HORSE64_BYTECODE_H_
