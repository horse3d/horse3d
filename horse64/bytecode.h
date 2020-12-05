// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_BYTECODE_H_
#define HORSE64_BYTECODE_H_

#include "compileconfig.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/globallimits.h"

#define MAX_ERROR_STACK_FRAMES 10

typedef struct h64debugsymbols h64debugsymbols;
typedef uint32_t h64wchar;
typedef struct h64gcvalue h64gcvalue;
typedef struct valuecontent valuecontent;

typedef enum instructiontype {
    H64INST_INVALID = 0,
    H64INST_SETCONST = 1,
    H64INST_SETGLOBAL,
    H64INST_GETGLOBAL,
    H64INST_SETBYINDEXEXPR,
    H64INST_SETBYATTRIBUTENAME,
    H64INST_SETBYATTRIBUTEIDX,
    H64INST_GETFUNC,
    H64INST_GETCLASS,
    H64INST_VALUECOPY,
    H64INST_BINOP,
    H64INST_UNOP,
    H64INST_CALL,
    H64INST_CALLIGNOREIFNONE,
    H64INST_SETTOP,
    H64INST_CALLSETTOP,
    H64INST_RETURNVALUE,
    H64INST_JUMPTARGET,
    H64INST_CONDJUMP,
    H64INST_JUMP,
    H64INST_NEWITERATOR,
    H64INST_ITERATE,
    H64INST_PUSHRESCUEFRAME,
    H64INST_ADDRESCUETYPEBYREF,
    H64INST_ADDRESCUETYPE,
    H64INST_POPRESCUEFRAME,
    H64INST_GETATTRIBUTEBYNAME,
    H64INST_JUMPTOFINALLY,
    H64INST_NEWLIST,
    H64INST_NEWSET,
    H64INST_NEWMAP,
    H64INST_NEWVECTOR,
    H64INST_NEWINSTANCEBYREF,
    H64INST_NEWINSTANCE,
    H64INST_GETCONSTRUCTOR,
    H64INST_AWAITITEM,
    H64INST_CREATEPIPE,
    H64INST_HASATTRJUMP,
    H64INST_RAISE,
    H64INST_RAISEBYREF,
    H64INST_TOTAL_COUNT
} instructiontype;

const char *bytecode_InstructionTypeToStr(instructiontype itype);

typedef enum storagetype {
    H64STORETYPE_INVALID = 0,
    H64STORETYPE_STACKSLOT = 1,
    H64STORETYPE_GLOBALFUNCSLOT,
    H64STORETYPE_GLOBALCLASSSLOT,
    H64STORETYPE_GLOBALVARSLOT,
    H64STORETYPE_VARATTRSLOT,
    H64STORETYPE_TOTAL_COUNT
} storagetype;

typedef struct storageref {
    uint8_t type;
    int64_t id;
} storageref;

typedef struct h64errorinfo {
    int64_t stack_frame_funcid[MAX_ERROR_STACK_FRAMES];
    int64_t stack_frame_byteoffset[MAX_ERROR_STACK_FRAMES];
    h64wchar *msg;
    int64_t msglen;
    classid_t error_class_id;
    int refcount;
} h64errorinfo;

#include "valuecontentstruct.h"


typedef int16_t jumpoffset_t;

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

typedef struct h64instruction_setbyindexexpr {
    uint8_t type;
    int16_t slotobjto;
    int16_t slotindexto;
    int16_t slotvaluefrom;
} __attribute__((packed)) h64instruction_setbyindexexpr;

typedef struct h64instruction_setbyattributename {
    uint8_t type;
    int16_t slotobjto;
    int64_t nameidx;
    int16_t slotvaluefrom;
} __attribute__((packed)) h64instruction_setbyattributename;

typedef struct h64instruction_setbyattributeidx {
    uint8_t type;
    int16_t slotobjto;
    attridx_t varattrto;
    int16_t slotvaluefrom;
} __attribute__((packed)) h64instruction_setbyattributeidx;

typedef struct h64instruction_getfunc {
    uint8_t type;
    int16_t slotto;
    int64_t funcfrom;
} __attribute__((packed)) h64instruction_getfunc;

typedef struct h64instruction_getclass {
    uint8_t type;
    int16_t slotto;
    classid_t classfrom;
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

#define CALLFLAG_UNPACKLASTPOSARG 1
#define CALLFLAG_ASYNC 2
#define CALLFLAG_PARALLELASYNC 4

typedef struct h64instruction_call {
    uint8_t type;
    int16_t returnto, slotcalledfrom;
    uint8_t flags;
    int16_t posargs, kwargs;
} __attribute__((packed)) h64instruction_call;

typedef struct h64instruction_callignoreifnone {
    uint8_t type;
    int16_t returnto, slotcalledfrom;
    uint8_t flags;
    int16_t posargs, kwargs;
} __attribute__((packed)) h64instruction_callignoreifnone;

typedef struct h64instruction_settop {
    uint8_t type;
    int16_t topto;
} __attribute__ ((packed)) h64instruction_settop;

typedef struct h64instruction_callsettop {
    uint8_t type;
    int16_t topto;
} __attribute__((packed)) h64instruction_callsettop;

typedef struct h64instruction_returnvalue {
    uint8_t type;
    int16_t returnslotfrom;
} __attribute__ ((packed)) h64instruction_returnvalue;

typedef struct h64instruction_jumptarget {
    uint8_t type;
    jumpoffset_t jumpid;
} __attribute__ ((packed)) h64instruction_jumptarget;

typedef struct h64instruction_condjump {
    uint8_t type;
    jumpoffset_t jumpbytesoffset;
    int16_t conditionalslot;
} __attribute__ ((packed)) h64instruction_condjump;

typedef struct h64instruction_jump {
    uint8_t type;
    jumpoffset_t jumpbytesoffset;
} __attribute__ ((packed)) h64instruction_jump;

typedef struct h64instruction_newiterator {
    uint8_t type;
    int16_t slotiteratorto, slotcontainerfrom;
} __attribute__ ((packed)) h64instruction_newiterator;

typedef struct h64instruction_iterate {
    uint8_t type;
    int16_t slotvalueto, slotiteratorfrom, jumponend;
} __attribute__ ((packed)) h64instruction_iterate;

#define RESCUEMODE_JUMPONRESCUE 1
#define RESCUEMODE_JUMPONFINALLY 2

typedef struct h64instruction_pushrescueframe {
    uint8_t type;
    uint8_t mode;
    int16_t sloterrorto;
    jumpoffset_t jumponrescue, jumponfinally;
    int16_t frameid;
} __attribute__ ((packed)) h64instruction_pushrescueframe;

typedef struct h64instruction_addrescuetypebyref {
    uint8_t type;
    int16_t slotfrom;
    int16_t frameid;
} __attribute__ ((packed)) h64instruction_addrescuetypebyref;

typedef struct h64instruction_addrescuetype {
    uint8_t type;
    classid_t classid;
    int16_t frameid;
} __attribute__ ((packed)) h64instruction_addrescuetype;

typedef struct h64instruction_poprescueframe {
    uint8_t type;
    int16_t frameid;
} __attribute__ ((packed)) h64instruction_poprescueframe;

typedef struct h64instruction_jumptofinally {
    uint8_t type;
    int16_t frameid;
} __attribute__ ((packed)) h64instruction_jumptofinally;

typedef struct h64instruction_getattributebyname {
    uint8_t type;
    int16_t slotto;
    int16_t objslotfrom;
    int64_t nameidx;
} __attribute__ ((packed)) h64instruction_getattributebyname;

typedef struct h64instruction_newlist {
    uint8_t type;
    int16_t slotto;
} __attribute__ ((packed)) h64instruction_newlist;

typedef struct h64instruction_newset {
    uint8_t type;
    int16_t slotto;
} __attribute__ ((packed)) h64instruction_newset;

typedef struct h64instruction_newvector {
    uint8_t type;
    int16_t slotto;
} __attribute__ ((packed)) h64instruction_newvector;

typedef struct h64instruction_newmap {
    uint8_t type;
    int16_t slotto;
} __attribute__ ((packed)) h64instruction_newmap;

typedef struct h64instruction_newinstancebyref {
    uint8_t type;
    int16_t slotto;
    int16_t classtypeslotfrom;
} __attribute__ ((packed)) h64instruction_newinstancebyref;

typedef struct h64instruction_newinstance {
    uint8_t type;
    int16_t slotto;
    classid_t classidcreatefrom;
} __attribute__ ((packed)) h64instruction_newinstance;

typedef struct h64instruction_getconstructor {
    uint8_t type;
    int16_t slotto;
    int16_t objslotfrom;
} __attribute__ ((packed)) h64instruction_getconstructor;

typedef struct h64instruction_awaititem {
    uint8_t type;
    int16_t objslotawait;
} __attribute__ ((packed)) h64instruction_awaititem;

typedef struct h64instruction_hasattrjump {
    uint8_t type;
    jumpoffset_t jumpbytesoffset;
    int16_t slotvaluecheck;
    int64_t nameidxcheck;
} __attribute__ ((packed)) h64instruction_hasattrjump;

typedef struct h64instruction_raise {
    uint8_t type;
    classid_t error_class_id;
    int16_t sloterrormsgobj;
} __attribute__ ((packed)) h64instruction_raise;

typedef struct h64instruction_raisebyref {
    uint8_t type;
    int16_t sloterrorobj;
    int16_t sloterrormsgobj;
} __attribute__ ((packed)) h64instruction_raisebyref;


#define H64CLASS_HASH_SIZE 32
#define H64CLASS_METHOD_OFFSET (H64LIMIT_MAX_CLASS_VARATTRS)

typedef struct h64classattributeinfo {
    int64_t nameid;
    attridx_t methodorvaridx;  // vars have H64CLASS_METHOD_OFFSET offset
} h64classattributeinfo;

#define VARATTR_FLAGS_CONST 0x1
#define VARATTR_FLAGS_READONLY 0x2

typedef struct h64class {
    classid_t base_class_global_id;
    int is_error, is_threadable, user_set_parallel;

    attridx_t funcattr_count;
    int64_t *funcattr_global_name_idx;
    funcid_t *funcattr_func_idx;
    attridx_t varattr_count;
    int64_t *varattr_global_name_idx;
    uint8_t *varattr_flags;

    h64classattributeinfo **global_name_to_attribute_hashmap;

    int hasvarinitfunc;
    funcid_t varinitfuncidx;
} h64class;

typedef struct h64func {
    int input_stack_size, inner_stack_size;
    int iscfunc, is_threadable, user_set_parallel;
    int kwarg_count;
    int64_t *kwargnameindexes;
    int16_t async_progress_struct_size;

    char *cfunclookup;  // path to identify C extension func

    classid_t associated_class_index;

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
    uint8_t is_simple_constant, is_const;
} h64globalvar;

typedef struct h64program {
    int64_t globals_count;
    valuecontent *globals;

    classid_t classes_count;
    h64class *classes;

    funcid_t func_count;
    h64func *func;

    funcid_t main_func_index;
    funcid_t globalinitsimple_func_index;
    funcid_t globalinit_func_index;
    funcid_t print_func_index;
    funcid_t containeradd_func_index;
    funcid_t has_attr_func_idx;

    int64_t as_str_name_index;
    int64_t to_str_name_index;
    int64_t len_name_index;
    int64_t init_name_index;
    int64_t on_destroy_name_index;
    int64_t equals_name_index;
    int64_t to_hash_name_index;
    int64_t add_name_index;
    int64_t del_name_index;
    int64_t is_a_name_index;

    classid_t _io_file_class_idx;  // used by io module

    globalvarid_t globalvar_count;
    h64globalvar *globalvar;

    h64debugsymbols *symbols;
} h64program;

ATTR_UNUSED static char *builtin_type_attributes[] = {
    "as_str", "to_str", "len", "init", "on_destroy",
    "equals", "to_hash", "add", "del", "is_a", NULL
};

ATTR_UNUSED static int isbuiltinattrname(const char *name) {
    int i = 0;
    while (builtin_type_attributes[i]) {
        if (strcmp(builtin_type_attributes[i], name) == 0)
            return 1;
        i++;
    }
    return 0;
}

h64program *h64program_New();

typedef struct h64vmthread h64vmthread;

size_t h64program_PtrToInstructionSize(
    char *ptr
);

#include "gcvalue.h"  // Keep it here since valuecontent def must come before

void valuecontent_Free(valuecontent *content);

void h64program_FreeInstructions(
    char *instructionbytes, int instructionbytes_len
);

attridx_t h64program_LookupClassAttribute(
    h64program *p, classid_t class_id, int64_t nameid
);

attridx_t h64program_LookupClassAttributeByName(
    h64program *p, classid_t class_id, const char *name
);

attridx_t h64program_RegisterClassVariable(
    h64program *p, classid_t class_id,
    const char *name, void *tmp_expr_ptr
);

attridx_t h64program_ClassNameToMemberIdx(
    h64program *p, classid_t class_id, int64_t nameidx
);

funcid_t h64program_RegisterCFunction(
    h64program *p,
    const char *name,
    int (*func)(h64vmthread *vmthread),
    const char *fileuri,
    int arg_count,
    const char **arg_kwarg_name,
    const char *module_path,
    const char *library_name,
    int is_threadable,
    classid_t associated_class_idx
);

funcid_t h64program_RegisterHorse64Function(
    h64program *p,
    const char *name,
    const char *fileuri,
    int arg_count,
    const char **arg_kwarg_name,
    const char *module_path,
    const char *library_name,
    classid_t associated_class_idx
);

classid_t h64program_AddClass(
    h64program *p,
    const char *name,
    const char *fileuri,
    const char *module_path,
    const char *library_name
);

globalvarid_t h64program_AddGlobalvar(
    h64program *p,
    const char *name,
    int is_const,
    const char *fileuri,
    const char *module_path,
    const char *library_name
);

void h64program_FreeClassAttributeHashmap(
    h64program *p, classid_t class_id
);

int h64program_AllocClassAttributeHashmap(
    h64program *p, classid_t class_id
);

int h64program_RebuildClassAttributeHashmap(
    h64program *p, classid_t class_id
);

void h64program_Free(h64program *p);

void h64program_PrintBytecodeStats(h64program *p);

#include "gcvalue.h"

ATTR_UNUSED static inline void DELREF_NONHEAP(valuecontent *content) {
    if (content->type == H64VALTYPE_GCVAL) {
        ((h64gcvalue *)content->ptr_value)->externalreferencecount--;
    } else if (content->type == H64VALTYPE_ERROR) {
        if (content->einfo)
            content->einfo->refcount--;
    }
}

ATTR_UNUSED static inline void ADDREF_NONHEAP(valuecontent *content) {
    if (content->type == H64VALTYPE_GCVAL) {
        ((h64gcvalue *)content->ptr_value)->externalreferencecount++;
    } else if (content->type == H64VALTYPE_ERROR) {
        if (content->einfo)
            content->einfo->refcount++;
    }
}

ATTR_UNUSED static inline void DELREF_HEAP(valuecontent *content) {
    if (content->type == H64VALTYPE_GCVAL) {
        ((h64gcvalue *)content->ptr_value)->heapreferencecount--;
    } else if (content->type == H64VALTYPE_ERROR) {
        if (content->einfo)
            content->einfo->refcount--;
    }
}

ATTR_UNUSED static inline void ADDREF_HEAP(valuecontent *content) {
    if (content->type == H64VALTYPE_GCVAL) {
        ((h64gcvalue *)content->ptr_value)->heapreferencecount++;
    } else if (content->type == H64VALTYPE_ERROR) {
        if (content->einfo)
            content->einfo->refcount++;
    }
}


#endif  // HORSE64_BYTECODE_H_
