// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "bytecode.h"
#include "gcvalue.h"
#include "pipe.h"
#include "stack.h"
#include "valuecontentstruct.h"
#include "vmexec.h"

typedef struct h64pipe {
    
    
} h64pipe;


#define DUPOBJECT_SETTO_OBJINSTANCE 1
#define DUPOBJECT_SETTO_LIST 2
#define DUPOBJECT_SETTO_MAP 3
#define DUPOBJECT_SETTO_SET 4

typedef struct _dopipe_dupobjectinfo {
    uint8_t setto_type;
    h64gcvalue *setto_obj;
    union {
        struct {
            int64_t setto_attrslot;
        };
        struct {
            int64_t map_bucketidx;
            int map_bucketslot;
        };
    };
    h64gcvalue *setfrom_obj;
} _dopipe_dupobjectinfo;


int _pipe_DoPipeObject(
        h64vmthread *source_thread,
        h64vmthread *target_thread,
        int64_t slot_from, int64_t slot_to,
        h64gcvalue **object_instances_transferlist,
        int *object_instances_transferlist_count,
        int *object_instances_transferlist_alloc,
        int *object_instances_transferlist_onheap
        ) {
    _dopipe_dupobjectinfo _dupobjlistbuf[128];
    _dopipe_dupobjectinfo *dup_todo_list = _dupobjlistbuf;
    int dup_todo_list_heap = 0;
    int dup_todo_list_fill = 0;
    int dup_todo_list_alloc = 128;

    valuecontent *vcsource = STACK_ENTRY(
        source_thread->stack,
        slot_from - source_thread->stack->current_func_floor
    );
    valuecontent *vctarget = STACK_ENTRY(
        target_thread->stack,
        slot_to - target_thread->stack->current_func_floor
    );
    DELREF_NONHEAP(vctarget);
    valuecontent_Free(target_thread, vctarget);
    memset(vctarget, 0, sizeof(*vctarget));

    if (likely(vcsource->type == H64VALTYPE_INT64 ||
            vcsource->type == H64VALTYPE_FLOAT64 ||
            vcsource->type == H64VALTYPE_NONE ||
            vcsource->type == H64VALTYPE_BOOL ||
            vcsource->type == H64VALTYPE_SHORTSTR ||
            vcsource->type == H64VALTYPE_SHORTBYTES ||
            vcsource->type == H64VALTYPE_CLASSREF ||
            vcsource->type == H64VALTYPE_FUNCREF ||
            vcsource->type == H64VALTYPE_UNSPECIFIED_KWARG)) {
        memcpy(vctarget, vcsource, sizeof(*vcsource));
        ADDREF_NONHEAP(vctarget);
    } else if (vcsource->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue*)vcsource->ptr_value)->type ==
            H64GCVALUETYPE_OBJINSTANCE) {
        assert(0);
    } else if (vcsource->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue*)vcsource->ptr_value)->type ==
            H64GCVALUETYPE_SET) {
        assert(0);
    } else if (vcsource->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue*)vcsource->ptr_value)->type ==
            H64GCVALUETYPE_LIST) {
        assert(0);
    } else if (vcsource->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue*)vcsource->ptr_value)->type ==
            H64GCVALUETYPE_MAP) {
        assert(0);
    } else {
        assert(0);
    }
    return 1;
}