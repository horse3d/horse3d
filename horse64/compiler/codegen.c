// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "bytecode.h"
#include "compileconfig.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>

#include "compiler/ast.h"
#include "compiler/asthelpers.h"
#include "compiler/astparser.h"
#include "compiler/asttransform.h"
#include "compiler/codegen.h"
#include "compiler/compileproject.h"
#include "compiler/main.h"
#include "compiler/varstorage.h"
#include "corelib/errors.h"
#include "hash.h"
#include "widechar.h"


static void get_assign_lvalue_storage(
        h64expression *expr,
        storageref **out_storageref
        ) {
    assert(expr->type == H64EXPRTYPE_ASSIGN_STMT);
    if (expr->assignstmt.lvalue->type ==
            H64EXPRTYPE_BINARYOP &&
            expr->assignstmt.lvalue->op.optype ==
                H64OP_ATTRIBUTEBYIDENTIFIER &&
            expr->assignstmt.lvalue->op.value2->storage.set) {
        *out_storageref = &(
            expr->assignstmt.lvalue->op.value2->storage.ref
        );
    } else {
        if (expr->assignstmt.lvalue->storage.set) {
            *out_storageref = &expr->assignstmt.lvalue->storage.ref;
        } else {
            assert(
                expr->assignstmt.lvalue->type == H64EXPRTYPE_BINARYOP &&
                (expr->assignstmt.lvalue->op.optype ==
                     H64OP_ATTRIBUTEBYIDENTIFIER ||
                 expr->assignstmt.lvalue->op.optype ==
                     H64OP_INDEXBYEXPR)
            );
            *out_storageref = NULL;
        }
    }
}

static int is_in_extends_arg(h64expression *expr) {
    h64expression *child = expr;
    h64expression *parent = expr->parent;
    while (parent) {
        if (parent->type == H64EXPRTYPE_CLASSDEF_STMT)
            return (
                child == parent->classdef.baseclass_ref
            );
        child = parent;
        parent = parent->parent;
    }
    return 0;
}

static int _newtemp_ex(h64expression *func, int deletepastline) {
    int i = 0;
    while (i < func->funcdef._storageinfo->codegen.extra_temps_count) {
        if (!func->funcdef._storageinfo->codegen.extra_temps_used[i]) {
            func->funcdef._storageinfo->codegen.extra_temps_used[i] = 1;
            func->funcdef._storageinfo->codegen.
                extra_temps_deletepastline[i] = (deletepastline != 0);
            return func->funcdef._storageinfo->
                lowest_guaranteed_free_temp + i;
        }
        i++;
    }
    int *new_used = realloc(
        func->funcdef._storageinfo->codegen.extra_temps_used,
        sizeof(*func->funcdef._storageinfo->codegen.extra_temps_used) * (
        func->funcdef._storageinfo->codegen.extra_temps_count + 1)
    );
    if (!new_used)
        return -1;
    func->funcdef._storageinfo->codegen.extra_temps_used = new_used;
    int *new_deletepastline = realloc(
        func->funcdef._storageinfo->codegen.extra_temps_deletepastline,
        sizeof(*func->funcdef._storageinfo->
               codegen.extra_temps_deletepastline) * (
        func->funcdef._storageinfo->codegen.extra_temps_count + 1)
    );
    if (!new_deletepastline)
        return -1;
    func->funcdef._storageinfo->codegen.extra_temps_deletepastline = (
        new_deletepastline
    );
    func->funcdef._storageinfo->codegen.extra_temps_used[
        func->funcdef._storageinfo->codegen.extra_temps_count
    ] = 1;
    func->funcdef._storageinfo->codegen.extra_temps_deletepastline[
        func->funcdef._storageinfo->codegen.extra_temps_count
    ] = (deletepastline != 0);
    func->funcdef._storageinfo->codegen.extra_temps_count++;
    if (func->funcdef._storageinfo->codegen.extra_temps_count >
            func->funcdef._storageinfo->
            codegen.max_extra_stack) {
        func->funcdef._storageinfo->codegen.max_extra_stack = (
            func->funcdef._storageinfo->codegen.extra_temps_count
        );
    }
    return func->funcdef._storageinfo->lowest_guaranteed_free_temp + (
        func->funcdef._storageinfo->codegen.extra_temps_count - 1
    );
}

int newmultilinetemp(h64expression *func) {
    return _newtemp_ex(func, 0);
}

void free1linetemps(h64expression *func) {
    int i = 0;
    while (i < func->funcdef._storageinfo->codegen.extra_temps_count) {
        if (func->funcdef._storageinfo->codegen.extra_temps_used[i] &&
                func->funcdef._storageinfo->codegen.
                    extra_temps_deletepastline[i]) {
            func->funcdef._storageinfo->codegen.extra_temps_used[i] = 0;
        }
        i++;
    }
}

int funccurrentstacktop(h64expression *func) {
    int _top = func->funcdef._storageinfo->lowest_guaranteed_free_temp;
    int i = 0;
    while (i < func->funcdef._storageinfo->codegen.extra_temps_count) {
        if (func->funcdef._storageinfo->codegen.extra_temps_used[i]) {
            _top = (func->funcdef._storageinfo->
                    lowest_guaranteed_free_temp + i + 1);
        }
        i++;
    }
    return _top;
}

void freemultilinetemp(
        h64expression *func, int temp
        ) {
    temp -= func->funcdef._storageinfo->lowest_guaranteed_free_temp;
    assert(temp >= 0 && temp <
           func->funcdef._storageinfo->codegen.extra_temps_count);
    assert(func->funcdef._storageinfo->codegen.extra_temps_used[temp]);
    assert(func->funcdef._storageinfo->codegen.
               extra_temps_deletepastline[temp] == 0);
    func->funcdef._storageinfo->codegen.extra_temps_used[temp] = 0;
}

int new1linetemp(
        h64expression *func, h64expression *expr, int ismainitem
        ) {
    if (ismainitem) {
        // Use temporary 'mandated' by parent if any:
        storageref *parent_store = NULL;
        if (expr && expr->parent &&
                expr->parent->type == H64EXPRTYPE_ASSIGN_STMT &&
                expr->parent->assignstmt.assignop == H64OP_ASSIGN) {
            get_assign_lvalue_storage(expr->parent, &parent_store);
        } else if (expr && expr->parent &&
                expr->parent->type == H64EXPRTYPE_VARDEF_STMT) {
            assert(expr->parent->storage.set);
            if (expr->parent->storage.ref.type == H64STORETYPE_STACKSLOT)
                return (int)expr->parent->storage.ref.id;
        }
        if (parent_store && parent_store->type ==
                H64STORETYPE_STACKSLOT)
            return parent_store->id;

        // If a binary or unary operator, see if we can reuse child storage:
        if (expr && (expr->type == H64EXPRTYPE_BINARYOP ||
                     expr->type == H64EXPRTYPE_UNARYOP)) {
            assert(expr->op.value1 != NULL);
            if (expr->op.value1->storage.eval_temp_id >=
                    func->funcdef._storageinfo->
                    lowest_guaranteed_free_temp) {
                return expr->op.value1->storage.eval_temp_id;
            }
            if (expr->type == H64EXPRTYPE_BINARYOP) {
                assert(expr->op.value2 != NULL);
                if (expr->op.value2->storage.eval_temp_id >=
                        func->funcdef._storageinfo->
                            lowest_guaranteed_free_temp) {
                    return expr->op.value2->storage.eval_temp_id;
                }
            }
        }
    }

    // Get new free temporary:
    assert(func->funcdef._storageinfo != NULL);
    return _newtemp_ex(func, 1);
}

int appendinstbyfuncid(
        h64program *p,
        int id,
        h64expression *correspondingexpr,
        // ^ FIXME: extract & attach debug info from this, like location
        void *ptr, size_t len
        ) {
    assert(id >= 0 && id < p->func_count);
    assert(!p->func[id].iscfunc);
    assert(((h64instructionany *)ptr)->type != H64INST_INVALID);
    char *instructionsnew = realloc(
        p->func[id].instructions,
        sizeof(*p->func[id].instructions) *
        (p->func[id].instructions_bytes + len)
    );
    if (!instructionsnew) {
        return 0;
    }
    p->func[id].instructions = instructionsnew;
    assert(p->func[id].instructions != NULL);
    memcpy(
        p->func[id].instructions + p->func[id].instructions_bytes,
        ptr, len
    );
    p->func[id].instructions_bytes += len;
    assert(p->func[id].instructions_bytes >= 0);
    return 1;
}

int appendinst(
        h64program *p,
        h64expression *func,
        h64expression *correspondingexpr,
        void *ptr, size_t len
        ) {
    assert(p != NULL);
    assert(func != NULL && (func->type == H64EXPRTYPE_FUNCDEF_STMT ||
           func->type == H64EXPRTYPE_INLINEFUNCDEF));
    int id = func->funcdef.bytecode_func_id;
    return appendinstbyfuncid(p, id, correspondingexpr, ptr, len);
}

void codegen_CalculateFinalFuncStack(
        h64program *program, h64expression *expr) {
    assert(expr != NULL && program != NULL);
    if (expr->type != H64EXPRTYPE_FUNCDEF_STMT)
        return;
    // Determine final amount of temporaries/stack slots used:
    h64funcsymbol *fsymbol = h64debugsymbols_GetFuncSymbolById(
        program->symbols, expr->funcdef.bytecode_func_id
    );
    expr->funcdef._storageinfo->lowest_guaranteed_free_temp +=
        expr->funcdef._storageinfo->codegen.max_extra_stack;
    fsymbol->closure_bound_count =
        expr->funcdef._storageinfo->closureboundvars_count;
    fsymbol->stack_temporaries_count =
        (expr->funcdef._storageinfo->lowest_guaranteed_free_temp -
         fsymbol->closure_bound_count -
         fsymbol->arg_count -
         (fsymbol->has_self_arg ? 1 : 0));
    program->func[expr->funcdef.bytecode_func_id].
        inner_stack_size = fsymbol->stack_temporaries_count;
    program->func[expr->funcdef.bytecode_func_id].
        input_stack_size = (
            fsymbol->closure_bound_count +
            fsymbol->arg_count +
            (fsymbol->has_self_arg ? 1 : 0)
        );
}

h64expression *_fakeclassinitfunc(
        asttransforminfo *rinfo, h64expression *classexpr
        ) {
    assert(classexpr != NULL &&
           classexpr->type == H64EXPRTYPE_CLASSDEF_STMT);
    classid_t classidx = classexpr->classdef.bytecode_class_id;
    assert(
        classidx >= 0 &&
        classidx < rinfo->pr->program->classes_count
    );
    assert(rinfo->pr->program->classes[classidx].hasvarinitfunc);

    // Make sure the map for registering it by class exists:
    if (!rinfo->pr->_tempclassesfakeinitfunc_map) {
        rinfo->pr->_tempclassesfakeinitfunc_map = (
            hash_NewBytesMap(1024)
        );
        if (!rinfo->pr->_tempclassesfakeinitfunc_map) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }
    }

    // If we got an entry already, return it:
    uintptr_t queryresult = 0;
    if (hash_BytesMapGet(
            rinfo->pr->_tempclassesfakeinitfunc_map,
            (const char *)&classidx, sizeof(classidx),
            &queryresult)) {
        assert(queryresult != 0);
        return (h64expression *)(void *)queryresult;
    }

    // Allocate new faked func expression and return it:
    h64expression *fakefunc = malloc(sizeof(*fakefunc));
    if (!fakefunc) {
        rinfo->hadoutofmemory = 1;
        return 0;
    }
    memset(fakefunc, 0, sizeof(*fakefunc));
    fakefunc->storage.eval_temp_id = -1;
    fakefunc->type = (
        H64EXPRTYPE_FUNCDEF_STMT
    );
    fakefunc->funcdef.name = strdup("$$clsinit");
    if (!fakefunc->funcdef.name) {
        oom:
        free(fakefunc->funcdef._storageinfo);
        free(fakefunc->funcdef.name);
        free(fakefunc);
        return NULL;
    }
    fakefunc->funcdef.bytecode_func_id = -1;
    fakefunc->funcdef._storageinfo = malloc(
        sizeof(*fakefunc->funcdef._storageinfo)
    );
    if (!fakefunc->funcdef._storageinfo)
        goto oom;
    memset(
        fakefunc->funcdef._storageinfo, 0,
        sizeof(*fakefunc->funcdef._storageinfo)
    );
    fakefunc->funcdef._storageinfo->closure_with_self = 1;
    fakefunc->funcdef._storageinfo->lowest_guaranteed_free_temp = 1;
    if (!hash_BytesMapSet(
            rinfo->pr->_tempclassesfakeinitfunc_map,
            (const char *)&classidx, sizeof(classidx),
            (uintptr_t)fakefunc
            ))
        goto oom;
    fakefunc->funcdef.bytecode_func_id = (
        rinfo->pr->program->classes[classidx].varinitfuncidx
    );
    fakefunc->storage.set = 1;
    fakefunc->storage.ref.type = (
        H64STORETYPE_GLOBALFUNCSLOT
    );
    fakefunc->storage.ref.id = (
        rinfo->pr->program->classes[classidx].varinitfuncidx
    );
    return fakefunc;
}

h64expression *_fakeglobalinitfunc(asttransforminfo *rinfo) {
    if (rinfo->pr->_tempglobalfakeinitfunc)
        return rinfo->pr->_tempglobalfakeinitfunc;
    rinfo->pr->_tempglobalfakeinitfunc = malloc(
        sizeof(*rinfo->pr->_tempglobalfakeinitfunc)
    );
    if (!rinfo->pr->_tempglobalfakeinitfunc)
        return NULL;
    memset(rinfo->pr->_tempglobalfakeinitfunc, 0,
           sizeof(*rinfo->pr->_tempglobalfakeinitfunc));
    rinfo->pr->_tempglobalfakeinitfunc->storage.eval_temp_id = -1;
    rinfo->pr->_tempglobalfakeinitfunc->type = (
        H64EXPRTYPE_FUNCDEF_STMT
    );
    rinfo->pr->_tempglobalfakeinitfunc->funcdef.name = strdup(
        "$$globalinit"
    );
    if (!rinfo->pr->_tempglobalfakeinitfunc->funcdef.name) {
        oom:
        free(rinfo->pr->_tempglobalfakeinitfunc->funcdef._storageinfo);
        free(rinfo->pr->_tempglobalfakeinitfunc->funcdef.name);
        free(rinfo->pr->_tempglobalfakeinitfunc);
        rinfo->pr->_tempglobalfakeinitfunc = NULL;
        return NULL;
    }
    rinfo->pr->_tempglobalfakeinitfunc->funcdef.bytecode_func_id = -1;
    rinfo->pr->_tempglobalfakeinitfunc->funcdef._storageinfo = (
        malloc(sizeof(
            *rinfo->pr->_tempglobalfakeinitfunc->funcdef._storageinfo
        ))
    );
    if (!rinfo->pr->_tempglobalfakeinitfunc->funcdef._storageinfo)
        goto oom;
    memset(
        rinfo->pr->_tempglobalfakeinitfunc->funcdef._storageinfo, 0,
        sizeof(*rinfo->pr->_tempglobalfakeinitfunc->funcdef._storageinfo)
    );
    int bytecode_id = h64program_RegisterHorse64Function(
        rinfo->pr->program, "$$globalinit",
        rinfo->pr->program->symbols->fileuri[
            rinfo->pr->program->symbols->mainfileuri_index
        ],
        0, NULL, 0,
        rinfo->pr->program->symbols->mainfile_module_path,
        "", -1
    );
    if (bytecode_id < 0)
        goto oom;
    rinfo->pr->program->func[bytecode_id].is_threadable = 0;
    rinfo->pr->_tempglobalfakeinitfunc->
        funcdef.bytecode_func_id = bytecode_id;
    rinfo->pr->program->globalinit_func_index = bytecode_id;
    rinfo->pr->_tempglobalfakeinitfunc->storage.set = 1;
    rinfo->pr->_tempglobalfakeinitfunc->storage.ref.type = (
        H64STORETYPE_GLOBALFUNCSLOT
    );
    rinfo->pr->_tempglobalfakeinitfunc->storage.ref.id = (
        bytecode_id
    );
    return rinfo->pr->_tempglobalfakeinitfunc;
}

struct _jumpinfo {
    int jumpid;
    int64_t offset;
};

static int _resolve_jumpid_to_jumpoffset(
        h64compileproject *prj,
        int jumpid, int64_t offset,
        struct _jumpinfo *jump_info,
        int jump_table_fill,
        int *out_oom,
        int16_t *out_jumpoffset
        ) {
    int64_t jumptargetoffset = -1;
    int z = 0;
    while (z < jump_table_fill) {
        if (jump_info[z].jumpid == jumpid) {
            jumptargetoffset = jump_info[z].offset;
            break;
        }
        z++;
    }
    if (jumptargetoffset < 0) {
        if (out_oom) *out_oom = 0;
        return 0;
    }
    jumptargetoffset -= offset;
    if (jumptargetoffset == 0) {
        prj->resultmsg->success = 0;
        char buf[256];
        snprintf(buf, sizeof(buf) - 1, "internal error: "
            "found jump instruction in func at "
            "instruction pos %" PRId64 " "
            "that has invalid zero relative offset - "
            "codegen bug?",
            (int64_t)offset
        );
        if (!result_AddMessage(
                prj->resultmsg,
                H64MSG_ERROR, buf,
                NULL, -1, -1
                )) {
            if (out_oom) *out_oom = 1;
            return 0;
        }
        if (out_oom) *out_oom = 0;
        return 0;
    }
    if (jumptargetoffset > 65535 || jumptargetoffset < -65535) {
        prj->resultmsg->success = 0;
        char buf[256];
        snprintf(buf, sizeof(buf) - 1,
            "found jump instruction in func at "
            "instruction pos %" PRId64 " "
            "that exceeds 16bit int range, this is not supported",
            (int64_t)offset
        );
        if (!result_AddMessage(
                prj->resultmsg,
                H64MSG_ERROR, buf,
                NULL, -1, -1
                )) {
            if (out_oom) *out_oom = 1;
            return 0;
        }
        if (out_oom) *out_oom = 0;
        return 0;
    }
    if (out_jumpoffset) *out_jumpoffset = jumptargetoffset;
    return 1;
}

static h64instruction_callsettop *_settop_inst(
        asttransforminfo *rinfo, h64expression *func,
        int64_t offset
        ) {
    return (h64instruction_callsettop *)(
        rinfo->pr->program->func[
            func->funcdef.bytecode_func_id
        ].instructions + offset
    );
}

static int _codegen_call_to(
        asttransforminfo *rinfo, h64expression *func,
        h64expression *callexpr,
        int calledexprstoragetemp, int resulttemp,
        int ignoreifnone
        ) {
    assert(callexpr->type == H64EXPRTYPE_CALL);
    int _argtemp = funccurrentstacktop(func);
    int posargcount = 0;
    int expandlastposarg = 0;
    int kwargcount = 0;
    int _reachedkwargs = 0;
    h64instruction_callsettop inst_callsettop = {0};
    inst_callsettop.type = H64INST_CALLSETTOP;
    inst_callsettop.topto = _argtemp;
    if (!appendinst(
            rinfo->pr->program, func, callexpr,
            &inst_callsettop, sizeof(inst_callsettop))) {
        rinfo->hadoutofmemory = 1;
        return 0;
    }
    int64_t callsettop_offset = (
        rinfo->pr->program->func[
            func->funcdef.bytecode_func_id
        ].instructions_bytes -
        sizeof(h64instruction_callsettop)
    );
    int i = 0;
    while (i < callexpr->inlinecall.arguments.arg_count) {
        assert(callexpr->inlinecall.arguments.arg_name != NULL);
        if (callexpr->inlinecall.arguments.arg_name[i])
            _reachedkwargs = 1;
        #ifndef NDEBUG
        if (callexpr->inlinecall.arguments.arg_value == NULL) {
            printf(
                "horsec: error: internal error: "
                "invalid call expression with arg count > 0, "
                "but arg_value array is NULL\n"
            );
            char *s = ast_ExpressionToJSONStr(callexpr, NULL);
            printf(
                "horsec: error: internal error: "
                "expr is: %s\n", s
            );
            free(s);
        }
        #endif
        assert(callexpr->inlinecall.arguments.arg_value != NULL);
        assert(callexpr->inlinecall.arguments.arg_value[i] != NULL);
        int ismultiarg = 0;
        if (_reachedkwargs) {
            kwargcount++;
            assert(callexpr->inlinecall.arguments.arg_name[i] != NULL);
            int64_t kwnameidx = (
                h64debugsymbols_AttributeNameToAttributeNameId(
                    rinfo->pr->program->symbols,
                    callexpr->inlinecall.arguments.arg_name[i],
                    0
                ));
            if (kwnameidx < 0) {
                char buf[256];
                snprintf(buf, sizeof(buf) - 1,
                    "internal error: cannot map kw arg name: %s",
                    callexpr->inlinecall.arguments.arg_name[i]
                );
                if (!result_AddMessage(
                        &rinfo->ast->resultmsg,
                        H64MSG_ERROR, buf,
                        rinfo->ast->fileuri,
                        callexpr->line, callexpr->column
                        )) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
                return 1;
            }
            h64instruction_setconst inst_setconst = {0};
            inst_setconst.type = H64INST_SETCONST;
            inst_setconst.slot = _argtemp;
            inst_setconst.content.type = H64VALTYPE_INT64;
            inst_setconst.content.int_value = kwnameidx;
            if (!appendinst(
                    rinfo->pr->program, func, callexpr,
                    &inst_setconst, sizeof(inst_setconst))) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
            _argtemp++;
            _settop_inst(rinfo, func, callsettop_offset)->topto++;
            h64instruction_valuecopy inst_vc = {0};
            inst_vc.type = H64INST_VALUECOPY;
            inst_vc.slotto = _argtemp;
            inst_vc.slotfrom = (
                callexpr->inlinecall.arguments.arg_value[i]->
                    storage.eval_temp_id);
            _argtemp++;
            _settop_inst(rinfo, func, callsettop_offset)->topto++;
            if (!appendinst(
                    rinfo->pr->program, func, callexpr,
                    &inst_vc, sizeof(inst_vc))) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
        } else if (i + 1 < callexpr->inlinecall.arguments.arg_count &&
                callexpr->inlinecall.arguments.arg_name[i]) {
            ismultiarg = 1;
        }
        if (!_reachedkwargs) {
            posargcount++;
            h64instruction_valuecopy inst_vc = {0};
            inst_vc.type = H64INST_VALUECOPY;
            inst_vc.slotto = _argtemp;
            inst_vc.slotfrom = (
                callexpr->inlinecall.arguments.arg_value[i]->
                    storage.eval_temp_id);
            _argtemp++;
            _settop_inst(rinfo, func, callsettop_offset)->topto++;
            if (!appendinst(
                    rinfo->pr->program, func, callexpr,
                    &inst_vc, sizeof(inst_vc))) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
            if (callexpr->inlinecall.arguments.
                    last_posarg_is_multiarg) {
                expandlastposarg = 1;
            }
        }
        i++;
    }
    int maxslotsused = _argtemp - (
        func->funcdef._storageinfo->lowest_guaranteed_free_temp
    );
    if (maxslotsused > func->funcdef._storageinfo->
            codegen.max_extra_stack)
        func->funcdef._storageinfo->codegen.max_extra_stack = (
            maxslotsused
        );
    int temp = resulttemp;
    if (temp < 0) {
        rinfo->hadoutofmemory = 1;
        return 0;
    }
    if (ignoreifnone) {
        h64instruction_callignoreifnone inst_call = {0};
        inst_call.type = H64INST_CALLIGNOREIFNONE;
        inst_call.returnto = temp;
        inst_call.slotcalledfrom = calledexprstoragetemp;
        inst_call.expandlastposarg = expandlastposarg;
        inst_call.posargs = posargcount;
        inst_call.kwargs = kwargcount;
        inst_call.async = (callexpr->inlinecall.is_async);
        if (!appendinst(
                rinfo->pr->program, func, callexpr,
                &inst_call, sizeof(inst_call))) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }
    } else {
        h64instruction_call inst_call = {0};
        inst_call.type = H64INST_CALL;
        inst_call.returnto = temp;
        inst_call.slotcalledfrom = calledexprstoragetemp;
        inst_call.expandlastposarg = expandlastposarg;
        inst_call.posargs = posargcount;
        inst_call.kwargs = kwargcount;
        inst_call.async = (callexpr->inlinecall.is_async);
        if (!appendinst(
                rinfo->pr->program, func, callexpr,
                &inst_call, sizeof(inst_call))) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }
    }
    callexpr->storage.eval_temp_id = temp;
    return 1;
}

int codegen_FinalBytecodeTransform(
        h64compileproject *prj
        ) {
    int haveerrors = 0;
    {
        int i = 0;
        while (i < prj->resultmsg->message_count) {
            if (prj->resultmsg->message[i].type == H64MSG_ERROR) {
                haveerrors = 1;
            }
            i++;
        }
    }
    if (!prj->resultmsg->success || haveerrors)
        return 1;

    int jump_table_alloc = 0;
    int jump_table_fill = 0;
    struct _jumpinfo *jump_info = NULL;
    h64program *pr = prj->program;

    int i = 0;
    while (i < pr->func_count) {
        if (pr->func[i].iscfunc) {
            i++;
            continue;
        }
        jump_table_fill = 0;

        assert(pr->func[i].instructions != NULL ||
               pr->func[i].instructions_bytes == 0);

        // Remove jumptarget instructions while extracting offsets:
        int64_t k = 0;
        while (k < pr->func[i].instructions_bytes) {
            h64instructionany *inst = (
                (h64instructionany *)((char*)pr->func[i].instructions + k)
            );
            assert(inst->type != H64INST_INVALID);
            if (inst->type == H64INST_JUMPTARGET) {
                if (jump_table_fill + 1 > jump_table_alloc) {
                    struct _jumpinfo *new_jump_info = realloc(
                        jump_info,
                        sizeof(*jump_info) *
                            ((jump_table_fill + 1) * 2 + 10)
                    );
                    if (!new_jump_info) {
                        free(jump_info);
                        return 0;
                    }
                    jump_table_alloc = (
                        (jump_table_fill + 1) * 2 + 10
                    );
                    jump_info = new_jump_info;
                }
                memset(
                    &jump_info[jump_table_fill], 0,
                    sizeof(*jump_info)
                );
                jump_info[jump_table_fill].offset = k;
                assert(k > 0);
                jump_info[jump_table_fill].jumpid = (
                    ((h64instruction_jumptarget *)inst)->jumpid
                );
                assert(k + (int)sizeof(h64instruction_jumptarget) <=
                       pr->func[i].instructions_bytes);
                memmove(
                    ((char*)pr->func[i].instructions) + k,
                    ((char*)pr->func[i].instructions) + k +
                        sizeof(h64instruction_jumptarget),
                    pr->func[i].instructions_bytes - (
                        k + sizeof(h64instruction_jumptarget)
                    )
                );
                pr->func[i].instructions_bytes -=
                    sizeof(h64instruction_jumptarget);
                jump_table_fill++;
                continue;
            }
            k += (int64_t)h64program_PtrToInstructionSize((char*)inst);
        }

        // Rewrite jumps to the actual offsets:
        k = 0;
        while (k < pr->func[i].instructions_bytes) {
            h64instructionany *inst = (
                (h64instructionany *)((char*)pr->func[i].instructions + k)
            );

            int32_t jumpid = -1;
            int32_t jumpid2 = -1;

            switch (inst->type) {
            case H64INST_CONDJUMP: {
                h64instruction_condjump *cjump = (
                    (h64instruction_condjump *)inst
                );
                jumpid = cjump->jumpbytesoffset;
                break;
            }
            case H64INST_JUMP: {
                h64instruction_jump *jump = (
                    (h64instruction_jump *)inst
                );
                jumpid = jump->jumpbytesoffset;
                break;
            }
            case H64INST_PUSHCATCHFRAME: {
                h64instruction_pushcatchframe *catchjump = (
                    (h64instruction_pushcatchframe *)inst
                );
                if ((catchjump->mode & CATCHMODE_JUMPONCATCH) != 0) {
                    jumpid = catchjump->jumponcatch;
                }
                if ((catchjump->mode & CATCHMODE_JUMPONFINALLY) != 0) {
                    jumpid2 = catchjump->jumponfinally;
                }
                break;
            }
            default:
                k += (int64_t)h64program_PtrToInstructionSize((char*)inst);
                continue;
            }
            assert(jumpid >= 0 || jumpid2 >= 0);

            // FIXME: use a faster algorithm here, maybe hash table?
            if (jumpid >= 0) {
                int hadoom = 0;
                int16_t offset = 0;
                int resolveworked = _resolve_jumpid_to_jumpoffset(
                    prj, jumpid, k, jump_info, jump_table_fill,
                    &hadoom, &offset
                );
                if (!resolveworked) {
                    free(jump_info);
                    return 0;
                }

                switch (inst->type) {
                case H64INST_CONDJUMP: {
                    h64instruction_condjump *cjump = (
                        (h64instruction_condjump *)inst
                    );
                    cjump->jumpbytesoffset = offset;
                    break;
                }
                case H64INST_JUMP: {
                    h64instruction_jump *jump = (
                        (h64instruction_jump *)inst
                    );
                    jump->jumpbytesoffset = offset;
                    break;
                }
                case H64INST_PUSHCATCHFRAME: {
                    h64instruction_pushcatchframe *catchjump = (
                        (h64instruction_pushcatchframe *)inst
                    );
                    catchjump->jumponcatch = offset;
                    break;
                }
                default:
                    fprintf(stderr, "horsec: error: internal error in "
                        "codegen jump translation: unhandled jump type\n");
                    free(jump_info);
                    return 0;
                }
            }
            if (jumpid2 >= 0) {
                int hadoom = 0;
                int16_t offset = 0;
                int resolveworked = _resolve_jumpid_to_jumpoffset(
                    prj, jumpid2, k, jump_info, jump_table_fill,
                    &hadoom, &offset
                );
                if (!resolveworked) {
                    free(jump_info);
                    return 0;
                }

                switch (inst->type) {
                case H64INST_PUSHCATCHFRAME: {
                    h64instruction_pushcatchframe *catchjump = (
                        (h64instruction_pushcatchframe *)inst
                    );
                    catchjump->jumponfinally = offset;
                    break;
                }
                default:
                    fprintf(stderr, "horsec: error: internal error in "
                        "codegen jump translation: unhandled jump type\n");
                    free(jump_info);
                    return 0;
                }
            }
            k += (int64_t)h64program_PtrToInstructionSize((char*)inst);
        }
        i++;
    }
    i = 0;
    while (i < pr->func_count) {
        if (pr->func[i].iscfunc) {
            i++;
            continue;
        }
        jump_table_fill = 0;

        int func_ends_in_return = 0;
        int64_t k = 0;
        while (k < pr->func[i].instructions_bytes) {
            h64instructionany *inst = (
                (h64instructionany *)((char*)pr->func[i].instructions + k)
            );
            size_t instsize = (
                h64program_PtrToInstructionSize((char*)inst)
            );
            if (k + (int)instsize >= pr->func[i].instructions_bytes &&
                    inst->type == H64INST_RETURNVALUE) {
                func_ends_in_return = 1;
            }
            k += (int64_t)instsize;
        }
        if (!func_ends_in_return) {
            // Add return to the end:
            if (pr->func[i].inner_stack_size <= 0)
                pr->func[i].inner_stack_size = 1;
            h64instruction_setconst inst_setnone = {0};
            inst_setnone.type = H64INST_SETCONST;
            inst_setnone.slot = 0;
            inst_setnone.content.type = H64VALTYPE_NONE;
            if (!appendinstbyfuncid(
                    pr, i, NULL,
                    &inst_setnone, sizeof(inst_setnone))) {
                return 0;
            }
            h64instruction_returnvalue inst_return = {0};
            inst_return.type = H64INST_RETURNVALUE;
            inst_return.returnslotfrom = 0;
            if (!appendinstbyfuncid(
                    pr, i, NULL,
                    &inst_return, sizeof(inst_return))) {
                return 0;
            }
        }

        i++;
    }
    free(jump_info);
    return 1;
}

int _codegencallback_DoCodegen_visit_out(
        h64expression *expr, ATTR_UNUSED h64expression *parent, void *ud
        ) {
    asttransforminfo *rinfo = (asttransforminfo *)ud;
    codegen_CalculateFinalFuncStack(rinfo->pr->program, expr);

    h64expression *func = surroundingfunc(expr);
    if (!func) {
        h64expression *sclass = surroundingclass(expr, 0);
        if (sclass != NULL) {
            func = _fakeclassinitfunc(rinfo, sclass);
        } else {
            func = _fakeglobalinitfunc(rinfo);
        }
        if (!func) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }
    }

    if (expr->type == H64EXPRTYPE_LIST ||
            expr->type == H64EXPRTYPE_SET) {
        int isset = (expr->type == H64EXPRTYPE_SET);
        int listtmp = new1linetemp(
            func, expr, 1
        );
        if (listtmp < 0) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }
        if (!isset) {
            h64instruction_newlist inst = {0};
            inst.type = H64INST_NEWLIST;
            inst.slotto = listtmp;
            if (!appendinst(rinfo->pr->program, func, expr,
                            &inst, sizeof(inst))) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
        } else {
            h64instruction_newset inst = {0};
            inst.type = H64INST_NEWSET;
            inst.slotto = listtmp;
            if (!appendinst(rinfo->pr->program, func, expr,
                            &inst, sizeof(inst))) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
        }
        int64_t entry_count = (
            isset ? expr->constructorset.entry_count :
            expr->constructorlist.entry_count
        );
        int64_t add_name_idx =
            h64debugsymbols_AttributeNameToAttributeNameId(
                rinfo->pr->program->symbols, "add", 1
            );
        if (entry_count > 0) {
            int addfunctemp = new1linetemp(
                func, expr, 0
            );
            assert(addfunctemp >= func->funcdef._storageinfo->
                   lowest_guaranteed_free_temp);
            if (addfunctemp < 0) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
            h64instruction_getattributebyname instgetattr = {0};
            instgetattr.type = H64INST_GETATTRIBUTEBYNAME;
            instgetattr.slotto = addfunctemp;
            instgetattr.objslotfrom = listtmp;
            instgetattr.nameidx = add_name_idx;
            if (!appendinst(
                    rinfo->pr->program, func, expr,
                    &instgetattr, sizeof(instgetattr)
                    )) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
            int argsfloor = funccurrentstacktop(func);
            int i = 0;
            while (i < entry_count) {
                int item_slot = (
                    (isset ? expr->constructorset.entry[i]->
                        storage.eval_temp_id :
                        expr->constructorlist.entry[i]->
                        storage.eval_temp_id)
                );
                assert(item_slot >= 0);
                h64instruction_valuecopy instvcopy = {0};
                instvcopy.type = H64INST_VALUECOPY;
                instvcopy.slotto = argsfloor;
                instvcopy.slotfrom = item_slot;
                if (!appendinst(rinfo->pr->program, func, expr,
                                &instvcopy, sizeof(instvcopy))) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
                h64instruction_callsettop inststop = {0};
                inststop.type = H64INST_CALLSETTOP;
                inststop.topto = argsfloor + 1;
                if (!appendinst(rinfo->pr->program, func, expr,
                                &inststop, sizeof(inststop))) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
                h64instruction_call instcall = {0};
                instcall.type = H64INST_CALL;
                instcall.returnto = argsfloor;
                instcall.slotcalledfrom = addfunctemp;
                instcall.posargs = 1;
                instcall.kwargs = 0;
                instcall.async = 0;
                if (!appendinst(rinfo->pr->program, func, expr,
                                &instcall, sizeof(instcall))) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
                i++;
            }
            if ((argsfloor + 1) - func->funcdef._storageinfo->
                    lowest_guaranteed_free_temp >
                    func->funcdef._storageinfo->
                    codegen.max_extra_stack) {
                func->funcdef._storageinfo->codegen.max_extra_stack = (
                    (argsfloor + 1) - func->funcdef._storageinfo->
                    lowest_guaranteed_free_temp
                );
            }
        }
        expr->storage.eval_temp_id = listtmp;
    } else if (expr->type == H64EXPRTYPE_AWAIT_STMT) {
        assert(expr->awaitstmt.awaitedvalue->storage.eval_temp_id >= 0);
        h64instruction_awaititem inst = {0};
        inst.type = H64INST_AWAITITEM;
        inst.objslotawait = expr->awaitstmt.awaitedvalue->storage.eval_temp_id;
        if (!appendinst(rinfo->pr->program, func, expr,
                        &inst, sizeof(inst))) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }
    } else if (expr->type == H64EXPRTYPE_VECTOR ||
               expr->type == H64EXPRTYPE_MAP) {
        int ismap = (expr->type == H64EXPRTYPE_MAP);
        int vectortmp = new1linetemp(
            func, expr, 1
        );
        if (vectortmp < 0) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }
        if (ismap) {
            h64instruction_newvector inst = {0};
            inst.type = H64INST_NEWVECTOR;
            inst.slotto = vectortmp;
            if (!appendinst(rinfo->pr->program, func, expr,
                            &inst, sizeof(inst))) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
        } else {
            h64instruction_newmap inst = {0};
            inst.type = H64INST_NEWMAP;
            inst.slotto = vectortmp;
            if (!appendinst(rinfo->pr->program, func, expr,
                            &inst, sizeof(inst))) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
        }
        int64_t entry_count = (
            ismap ? expr->constructormap.entry_count :
            expr->constructorvector.entry_count
        );
        int keytmp = -1;
        if (ismap) {
            keytmp = new1linetemp(
                func, expr, 0
            );
            if (keytmp < 0) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
        }
        int i = 0;
        while (i < entry_count) {
            int item_slot = (
                (ismap ? expr->constructormap.value[i]->
                    storage.eval_temp_id :
                    expr->constructorvector.entry[i]->
                    storage.eval_temp_id)
            );
            assert(item_slot >= 0);
            int key_slot = (
                (ismap ? expr->constructormap.key[i]->
                    storage.eval_temp_id :
                    keytmp)
            );
            assert(key_slot >= 0);
            if (!ismap) {
                h64instruction_setconst instsc = {0};
                instsc.type = H64INST_SETCONST;
                instsc.slot = key_slot;
                instsc.content.type = H64VALTYPE_INT64;
                instsc.content.int_value = i;
            }
            h64instruction_setbyindexexpr instbyindexexpr = {0};
            instbyindexexpr.type = H64INST_SETBYINDEXEXPR;
            instbyindexexpr.slotobjto = vectortmp;
            instbyindexexpr.slotindexto = key_slot;
            instbyindexexpr.slotvaluefrom = item_slot;
            if (!appendinst(rinfo->pr->program, func, expr,
                    &instbyindexexpr, sizeof(instbyindexexpr
                    ))) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
            i++;
        }
        expr->storage.eval_temp_id = vectortmp;
    } else if (expr->type == H64EXPRTYPE_LITERAL) {
        int temp = new1linetemp(func, expr, 1);
        if (temp < 0) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }
        h64instruction_setconst inst = {0};
        inst.type = H64INST_SETCONST;
        inst.slot = temp;
        memset(&inst.content, 0, sizeof(inst.content));
        if (expr->literal.type == H64TK_CONSTANT_INT) {
            inst.content.type = H64VALTYPE_INT64;
            inst.content.int_value = expr->literal.int_value;
        } else if (expr->literal.type == H64TK_CONSTANT_FLOAT) {
            inst.content.type = H64VALTYPE_FLOAT64;
            inst.content.float_value = expr->literal.float_value;
        } else if (expr->literal.type == H64TK_CONSTANT_BOOL) {
            inst.content.type = H64VALTYPE_BOOL;
            inst.content.int_value = expr->literal.int_value;
        } else if (expr->literal.type == H64TK_CONSTANT_NONE) {
            inst.content.type = H64VALTYPE_NONE;
        } else if (expr->literal.type == H64TK_CONSTANT_BYTES) {
            inst.content.type = H64VALTYPE_SHORTBYTES;
            uint64_t len = expr->literal.str_value_len;
            if (strlen(expr->literal.str_value) <
                    VALUECONTENT_SHORTBYTESLEN) {
                memcpy(
                    inst.content.shortbytes_value,
                    expr->literal.str_value, len
                );
                inst.content.type = H64VALTYPE_SHORTBYTES;
                inst.content.shortbytes_len = len;
            } else {
                inst.content.type = H64VALTYPE_CONSTPREALLOCBYTES;
                inst.content.constpreallocbytes_value = malloc(len);
                if (!inst.content.constpreallocbytes_value) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
                inst.content.constpreallocbytes_len = len;
                memcpy(
                    inst.content.constpreallocbytes_value,
                    expr->literal.str_value, len
                );
            }
        } else if (expr->literal.type == H64TK_CONSTANT_STRING) {
            inst.content.type = H64VALTYPE_SHORTSTR;
            assert(expr->literal.str_value != NULL);
            int64_t out_len = 0;
            int abortinvalid = 0;
            int abortoom = 0;
            h64wchar *result = utf8_to_utf32_ex(
                expr->literal.str_value,
                expr->literal.str_value_len,
                NULL, 0,
                NULL, NULL, &out_len, 1, 0,
                &abortinvalid, &abortoom
            );
            if (!result) {
                if (abortoom) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
                char buf[256];
                snprintf(buf, sizeof(buf) - 1,
                    "internal error: utf8 to utf32 "
                    "conversion unexpectedly failed"
                );
                if (!result_AddMessage(
                        &rinfo->ast->resultmsg,
                        H64MSG_ERROR, buf,
                        rinfo->ast->fileuri,
                        expr->line, expr->column
                        )) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
                return 1;
            }
            assert(!abortinvalid);
            assert(!abortoom);
            if (out_len <= VALUECONTENT_SHORTSTRLEN) {
                memcpy(
                    inst.content.shortstr_value,
                    result, out_len * sizeof(*result)
                );
                inst.content.type = H64VALTYPE_SHORTSTR;
                inst.content.shortstr_len = out_len;
            } else {
                inst.content.type = H64VALTYPE_CONSTPREALLOCSTR;
                inst.content.constpreallocstr_value = malloc(
                    out_len * sizeof(*result)
                );
                if (!inst.content.constpreallocstr_value) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
                inst.content.constpreallocstr_len = out_len;
                memcpy(
                    inst.content.constpreallocstr_value,
                    result, out_len * sizeof(*result)
                );
            }
            free(result);
        } else {
            char buf[256];
            snprintf(buf, sizeof(buf) - 1,
                "internal error: unhandled literal type %d",
                (int)expr->literal.type
            );
            if (!result_AddMessage(
                    &rinfo->ast->resultmsg,
                    H64MSG_ERROR, buf,
                    rinfo->ast->fileuri,
                    expr->line, expr->column
                    )) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
            return 1;
        }
        if (!appendinst(rinfo->pr->program, func, expr,
                        &inst, sizeof(inst))) {
            if (inst.content.type == H64VALTYPE_CONSTPREALLOCSTR)
                free(inst.content.constpreallocstr_value);
            rinfo->hadoutofmemory = 1;
            return 0;
        }
        expr->storage.eval_temp_id = temp;
    } else if (expr->type == H64EXPRTYPE_WHILE_STMT ||
            expr->type == H64EXPRTYPE_DO_STMT ||
            expr->type == H64EXPRTYPE_FUNCDEF_STMT ||
            expr->type == H64EXPRTYPE_IF_STMT ||
            expr->type == H64EXPRTYPE_FOR_STMT ||
            expr->type == H64EXPRTYPE_WITH_STMT ||
            (expr->type == H64EXPRTYPE_UNARYOP &&
             expr->op.optype == H64OP_NEW)) {
        // Already handled in visit_in
    } else if (expr->type == H64EXPRTYPE_BINARYOP &&
            expr->op.optype == H64OP_ATTRIBUTEBYIDENTIFIER &&
            (expr->parent == NULL ||
             expr->parent->type != H64EXPRTYPE_ASSIGN_STMT ||
             expr->parent->assignstmt.lvalue != expr) &&
            !expr->op.value2->storage.set
            ) {
        if (is_in_extends_arg(expr)) {
            // Nothing to do if in 'extends' clause, since that
            // has all been resolved already by varstorage handling.
            return 1;
        }
        // Regular get by member that needs to be evaluated at runtime:
        assert(expr->op.value2->type == H64EXPRTYPE_IDENTIFIERREF);
        int64_t idx = h64debugsymbols_AttributeNameToAttributeNameId(
            rinfo->pr->program->symbols,
            expr->op.value2->identifierref.value, 0
        );
        int temp = new1linetemp(func, expr, 1);
        if (temp < 0) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }
        if (idx < 0) {
            // FIXME: hard-code an error raise
            fprintf(stderr, "fix invalid member\n");
        } else {
            h64instruction_getattributebyname inst_getattr = {0};
            inst_getattr.type = H64INST_GETATTRIBUTEBYNAME;
            inst_getattr.slotto = temp;
            inst_getattr.objslotfrom = expr->op.value1->storage.eval_temp_id;
            inst_getattr.nameidx = idx;
            if (!appendinst(
                    rinfo->pr->program, func, expr,
                    &inst_getattr, sizeof(inst_getattr))) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
        }
        expr->storage.eval_temp_id = temp;
    } else if (expr->type == H64EXPRTYPE_BINARYOP) {
        // Other binary op instances that aren't get by member,
        // unless it doesn't need to be handled anyway:
        if (expr->op.optype == H64OP_ATTRIBUTEBYIDENTIFIER) {
            if (expr->storage.set &&
                    expr->storage.eval_temp_id < 0) {
                // Might be a pre-resolved global module access,
                // in which case operand 2 should have been processed.
                assert(expr->op.value2->storage.eval_temp_id >= 0);
                expr->storage.eval_temp_id = (
                    expr->op.value2->storage.eval_temp_id
                );
            }
            assert(
                (expr->storage.set &&
                 expr->storage.eval_temp_id >= 0) || (
                expr->parent != NULL &&
                expr->parent->type == H64EXPRTYPE_ASSIGN_STMT &&
                expr->parent->assignstmt.lvalue == expr
                )
            );
            return 1;  // bail out, handled by parent assign statement
        }
        if (expr->op.optype == H64OP_INDEXBYEXPR) {
            if (expr->parent != NULL &&
                expr->parent->type == H64EXPRTYPE_ASSIGN_STMT &&
                expr->parent->assignstmt.lvalue == expr) {
                return 1;  // similar to getbymember this is
                           // handled by parent assign statement
            }
        }
        int temp = new1linetemp(func, expr, 1);
        if (temp < 0) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }
        h64instruction_binop inst_binop = {0};
        inst_binop.type = H64INST_BINOP;
        inst_binop.optype = expr->op.optype;
        inst_binop.slotto = temp;
        inst_binop.arg1slotfrom = expr->op.value1->storage.eval_temp_id;
        inst_binop.arg2slotfrom = expr->op.value2->storage.eval_temp_id;
        if (!appendinst(
                rinfo->pr->program, func, expr,
                &inst_binop, sizeof(inst_binop))) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }
        expr->storage.eval_temp_id = temp;
    } else if (expr->type == H64EXPRTYPE_UNARYOP &&
            expr->op.optype != H64OP_NEW) {
        int temp = new1linetemp(func, expr, 1);
        if (temp < 0) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }
        h64instruction_unop inst_unop = {0};
        inst_unop.type = H64INST_UNOP;
        inst_unop.optype = expr->op.optype;
        inst_unop.slotto = temp;
        inst_unop.argslotfrom = expr->op.value1->storage.eval_temp_id;
        if (!appendinst(
                rinfo->pr->program, func, expr,
                &inst_unop, sizeof(inst_unop))) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }
        expr->storage.eval_temp_id = temp;
    } else if (expr->type == H64EXPRTYPE_CALL) {
        int calledexprstoragetemp = (
            expr->inlinecall.value->storage.eval_temp_id
        );
        int temp = new1linetemp(func, expr, 1);
        if (temp < 0) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }
        if (!_codegen_call_to(
                rinfo, func, expr, calledexprstoragetemp, temp, 0
                )) {
            return 0;
        }

        expr->storage.eval_temp_id = temp;
    } else if (expr->type == H64EXPRTYPE_CLASSDEF_STMT ||
            expr->type == H64EXPRTYPE_CALL_STMT ||
            expr->type == H64EXPRTYPE_IMPORT_STMT) {
        // Nothing to do with those!
    } else if (expr->type == H64EXPRTYPE_IDENTIFIERREF ||
            expr->type == H64EXPRTYPE_WITH_CLAUSE) {
        if (expr->type == H64EXPRTYPE_IDENTIFIERREF) {
            // Special cases where we'll not handle it here:
            if (is_in_extends_arg(expr)) {
                // Nothing to do if in 'extends' clause, since that
                // has all been resolved already by varstorage handling.
                return 1;
            }
            if (expr->parent != NULL && (
                    (expr->parent->type == H64EXPRTYPE_ASSIGN_STMT &&
                    expr->parent->assignstmt.lvalue == expr) ||
                    (expr->parent->type == H64EXPRTYPE_BINARYOP &&
                    expr->parent->op.optype == H64OP_ATTRIBUTEBYIDENTIFIER &&
                    expr->parent->op.value2 == expr &&
                    !expr->storage.set &&
                    expr->parent->parent->type == H64EXPRTYPE_ASSIGN_STMT &&
                    expr->parent == expr->parent->parent->assignstmt.lvalue)
                    )) {
                // This identifier is assigned to, will be handled elsewhere
                return 1;
            } else if (expr->parent != NULL &&
                    expr->parent->type == H64EXPRTYPE_BINARYOP &&
                    expr->parent->op.optype == H64OP_ATTRIBUTEBYIDENTIFIER &&
                    expr->parent->op.value2 == expr &&
                    !expr->storage.set
                    ) {
                // A runtime-resolved get by identifier, handled elsewhere
                return 1;
            }
            if (expr->identifierref.resolved_to_expr &&
                    expr->identifierref.resolved_to_expr->type ==
                    H64EXPRTYPE_IMPORT_STMT)
                return 1;  // nothing to do with those
        }
        assert(expr->storage.set);
        if (expr->storage.ref.type == H64STORETYPE_STACKSLOT) {
            expr->storage.eval_temp_id = expr->storage.ref.id;
        } else {
            int temp = new1linetemp(func, expr, 1);
            if (temp < 0) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
            expr->storage.eval_temp_id = temp;
            if (expr->storage.ref.type == H64STORETYPE_GLOBALVARSLOT) {
                h64instruction_getglobal inst_getglobal = {0};
                inst_getglobal.type = H64INST_GETGLOBAL;
                inst_getglobal.slotto = temp;
                inst_getglobal.globalfrom = expr->storage.ref.id;
                if (!appendinst(
                        rinfo->pr->program, func, expr,
                        &inst_getglobal, sizeof(inst_getglobal))) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
            } else if (expr->storage.ref.type ==
                    H64STORETYPE_GLOBALFUNCSLOT) {
                h64instruction_getfunc inst_getfunc = {0};
                inst_getfunc.type = H64INST_GETFUNC;
                inst_getfunc.slotto = temp;
                inst_getfunc.funcfrom = expr->storage.ref.id;
                if (!appendinst(
                        rinfo->pr->program, func, expr,
                        &inst_getfunc, sizeof(inst_getfunc))) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
            } else if (expr->storage.ref.type ==
                    H64STORETYPE_GLOBALCLASSSLOT) {
                h64instruction_getclass inst_getclass = {0};
                inst_getclass.type = H64INST_GETCLASS;
                inst_getclass.slotto = temp;
                inst_getclass.classfrom = expr->storage.ref.id;
                if (!appendinst(
                        rinfo->pr->program, func, expr,
                        &inst_getclass, sizeof(inst_getclass))) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
            } else {
                char buf[256];
                snprintf(buf, sizeof(buf) - 1,
                    "internal error: unhandled storage type %d",
                    (int)expr->storage.ref.type
                );
                if (!result_AddMessage(
                        &rinfo->ast->resultmsg,
                        H64MSG_ERROR, buf,
                        rinfo->ast->fileuri,
                        expr->line, expr->column
                        )) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
                return 1;
            }
        }
        if (expr->type == H64EXPRTYPE_WITH_CLAUSE) {
            assert(expr->withclause.withitem_value != NULL);
            if (expr->withclause.withitem_value->storage.
                    eval_temp_id != expr->storage.eval_temp_id) {
                h64instruction_valuecopy vcopy = {0};
                vcopy.type = H64INST_VALUECOPY;
                vcopy.slotfrom = (
                    expr->withclause.withitem_value->storage.
                    eval_temp_id
                );
                vcopy.slotto = expr->storage.eval_temp_id;
                if (!appendinst(
                        rinfo->pr->program, func, expr,
                        &vcopy, sizeof(vcopy))) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
            }
        }
    } else if (expr->type == H64EXPRTYPE_VARDEF_STMT &&
               expr->vardef.value == NULL) {
        // Empty variable definition, nothing to do
        return 1;
    } else if (expr->type == H64EXPRTYPE_RETURN_STMT) {
        int returntemp = -1;
        if (expr->returnstmt.returned_expression) {
            returntemp = expr->returnstmt.returned_expression->
                storage.eval_temp_id;
            assert(returntemp >= 0);
        } else {
            returntemp = new1linetemp(func, expr, 1);
            if (returntemp < 0) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
            h64instruction_setconst inst_setconst = {0};
            inst_setconst.type = H64INST_SETCONST;
            inst_setconst.content.type = H64VALTYPE_NONE;
            if (!appendinst(rinfo->pr->program, func, expr,
                            &inst_setconst, sizeof(inst_setconst))) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
        }
        h64instruction_returnvalue inst_returnvalue = {0};
        inst_returnvalue.type = H64INST_RETURNVALUE;
        inst_returnvalue.returnslotfrom = returntemp;
        if (!appendinst(
                rinfo->pr->program, func, expr,
                &inst_returnvalue, sizeof(inst_returnvalue))
                ) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }
    } else if (expr->type == H64EXPRTYPE_VARDEF_STMT ||
            (expr->type == H64EXPRTYPE_ASSIGN_STMT && (
             expr->assignstmt.lvalue->type ==
                H64EXPRTYPE_IDENTIFIERREF ||
             (expr->assignstmt.lvalue->type ==
                  H64EXPRTYPE_BINARYOP &&
              expr->assignstmt.lvalue->op.optype ==
                  H64OP_ATTRIBUTEBYIDENTIFIER &&
              expr->assignstmt.lvalue->op.value2->type ==
                  H64EXPRTYPE_IDENTIFIERREF &&
              expr->assignstmt.lvalue->op.value2->storage.set) ||
             (expr->assignstmt.lvalue->type ==
                  H64EXPRTYPE_BINARYOP &&
              expr->assignstmt.lvalue->op.optype ==
                  H64OP_INDEXBYEXPR &&
              expr->assignstmt.lvalue->op.value1->
                  storage.eval_temp_id >= 0 &&
              expr->assignstmt.lvalue->op.value2->
                  storage.eval_temp_id >= 0)))) {
        // Assigning directly to a variable (rather than a member,
        // map value, or the like)
        int assignfromtemporary = -1;
        storageref *str = NULL;
        int complexsetter_tmp = -1;
        if (expr->type == H64EXPRTYPE_VARDEF_STMT) {
            assert(expr->storage.set);
            str = &expr->storage.ref;
            if (expr->vardef.value != NULL) {
                assert(expr->vardef.value->storage.eval_temp_id >= 0);
                assignfromtemporary = expr->vardef.value->
                    storage.eval_temp_id;
            } else {
                assignfromtemporary = new1linetemp(func, expr, 1);
                if (assignfromtemporary < 0) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
                h64instruction_setconst inst = {0};
                inst.type = H64INST_SETCONST;
                inst.slot = assignfromtemporary;
                inst.content.type = H64VALTYPE_NONE;
                if (!appendinst(
                        rinfo->pr->program, func, expr,
                        &inst, sizeof(inst))) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
            }
        } else if (expr->type == H64EXPRTYPE_ASSIGN_STMT) {
            get_assign_lvalue_storage(
                expr, &str
            );  // Get the storage info of our assignment target
            storageref _complexsetter_buf = {0};
            int iscomplexassign = 0;
            if (str == NULL) {  // No storage, must be complex assign:
                iscomplexassign = 1;
                assert(
                    expr->assignstmt.lvalue->type ==
                        H64EXPRTYPE_BINARYOP && (
                    expr->assignstmt.lvalue->op.optype ==
                        H64OP_ATTRIBUTEBYIDENTIFIER ||
                    expr->assignstmt.lvalue->op.optype == H64OP_INDEXBYEXPR
                    )
                );
                // This assigns to a member or indexed thing,
                // e.g. a[b] = c  or  a.b = c.
                //
                // -> We need an extra temporary to hold the in-between
                // value for this case, since there is no real target
                // storage ready. (Since we'll assign it with a special
                // setbymember/setbyindexexpr instruction, rather than
                // by copying directly into the target.)
                assert(!expr->assignstmt.lvalue->storage.set);
                assert(expr->assignstmt.lvalue->storage.eval_temp_id < 0);
                complexsetter_tmp = new1linetemp(func, expr, 0);
                if (complexsetter_tmp < 0) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
                _complexsetter_buf.type = H64STORETYPE_STACKSLOT;
                _complexsetter_buf.id = complexsetter_tmp;
                str = &_complexsetter_buf;
            }
            assert(str != NULL);
            assignfromtemporary = (
                expr->assignstmt.rvalue->storage.eval_temp_id
            );
            if (expr->assignstmt.assignop != H64OP_ASSIGN) {
                // This assign op does some sort of arithmetic!
                int oldvaluetemp = -1;
                if (str->type == H64STORETYPE_GLOBALVARSLOT) {
                    oldvaluetemp = new1linetemp(func, expr, 0);
                    if (oldvaluetemp < 0) {
                        rinfo->hadoutofmemory = 1;
                        return 0;
                    }
                    h64instruction_getglobal inst = {0};
                    inst.type = H64INST_GETGLOBAL;
                    inst.globalfrom = str->id;
                    inst.slotto = oldvaluetemp;
                    if (!appendinst(
                            rinfo->pr->program, func, expr,
                            &inst, sizeof(inst))) {
                        rinfo->hadoutofmemory = 1;
                        return 0;
                    }
                } else if (!iscomplexassign) {
                    assert(str->type == H64STORETYPE_STACKSLOT);
                    oldvaluetemp = str->id;
                } else {
                    // We actually need to get this the complex way:
                    oldvaluetemp = new1linetemp(func, expr, 0);
                    assert(expr->assignstmt.lvalue->type ==
                           H64EXPRTYPE_BINARYOP);
                    if (expr->assignstmt.lvalue->op.optype ==
                            H64OP_ATTRIBUTEBYIDENTIFIER) {
                        assert(
                            expr->assignstmt.lvalue->op.value2->type ==
                            H64EXPRTYPE_IDENTIFIERREF
                        );
                        int64_t nameid = (
                            h64debugsymbols_AttributeNameToAttributeNameId(
                                rinfo->pr->program->symbols,
                                expr->assignstmt.lvalue->op.value2->
                                    identifierref.value, 0
                            ));
                        if (nameid >= 0) {
                            h64instruction_getattributebyname inst = {0};
                            inst.type = H64INST_GETATTRIBUTEBYNAME;
                            inst.objslotfrom = (
                                expr->assignstmt.lvalue->op.value1->
                                    storage.eval_temp_id);
                            inst.nameidx = nameid;
                            inst.slotto = oldvaluetemp;
                            if (!appendinst(
                                    rinfo->pr->program, func, expr,
                                    &inst, sizeof(inst))) {
                                rinfo->hadoutofmemory = 1;
                                return 0;
                            }
                        } else {
                            // FIXME: hardcode attribute error
                            assert(0);
                        }
                    } else {
                        assert(expr->assignstmt.lvalue->op.optype ==
                               H64OP_INDEXBYEXPR);
                        h64instruction_binop inst = {0};
                        inst.type = H64INST_BINOP;
                        inst.optype = H64OP_INDEXBYEXPR;
                        inst.arg1slotfrom = (
                            expr->assignstmt.lvalue->op.value1->
                                storage.eval_temp_id);
                        inst.arg2slotfrom = (
                            expr->assignstmt.lvalue->op.value2->
                                storage.eval_temp_id);
                        inst.slotto = oldvaluetemp;
                        if (!appendinst(
                                rinfo->pr->program, func, expr,
                                &inst, sizeof(inst))) {
                            rinfo->hadoutofmemory = 1;
                            return 0;
                        }
                    }
                }
                int mathop = operator_AssignOpToMathOp(
                    expr->assignstmt.assignop
                );
                assert(mathop != H64OP_INVALID);
                h64instruction_binop inst_assignmath = {0};
                inst_assignmath.type = H64INST_BINOP;
                inst_assignmath.optype = mathop;
                inst_assignmath.arg1slotfrom = oldvaluetemp;
                inst_assignmath.arg2slotfrom = assignfromtemporary;
                inst_assignmath.slotto = oldvaluetemp;
                if (!appendinst(
                        rinfo->pr->program, func, expr,
                        &inst_assignmath, sizeof(inst_assignmath))) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
                assignfromtemporary = oldvaluetemp;
            }
        }
        assert(assignfromtemporary >= 0);
        assert(str->type == H64STORETYPE_GLOBALVARSLOT ||
               str->type == H64STORETYPE_STACKSLOT ||
               str->type == H64STORETYPE_VARATTRSLOT);
        if (str->type == H64STORETYPE_GLOBALVARSLOT) {
            h64instruction_setglobal inst = {0};
            inst.type = H64INST_SETGLOBAL;
            inst.globalto = str->id;
            inst.slotfrom = assignfromtemporary;
            if (!appendinst(rinfo->pr->program, func, expr,
                            &inst, sizeof(inst))) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
        } else if (str->type == H64STORETYPE_VARATTRSLOT) {
            assert(surroundingclass(expr, 1) != NULL);
            assert(func->funcdef._storageinfo->closure_with_self != 0);
            h64instruction_setbyattributeidx inst = {0};
            inst.type = H64INST_SETBYATTRIBUTEIDX;
            inst.slotobjto = 0;  // 0 must always be 'self'
            inst.varattrto = (attridx_t)str->id;
            inst.slotvaluefrom = assignfromtemporary;
            if (!appendinst(rinfo->pr->program, func, expr,
                            &inst, sizeof(inst))) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
        } else if (assignfromtemporary != str->id ||
                complexsetter_tmp >= 0) {
            assert(str->type == H64STORETYPE_STACKSLOT);
            if (complexsetter_tmp >= 0) {
                // This assigns to a member or indexed thing,
                // e.g. a[b] = c  or  a.b = c
                //
                // (Please note this excludes cases where a.b refers
                // to an import since those have storage 'flattened'
                // and set directly by the scope resolver. Therefore,
                // this code path is only cases where a.b needs to be
                // a true dynamic runtime getmember.)
                assert(expr->assignstmt.lvalue->type ==
                       H64EXPRTYPE_BINARYOP);
                if (expr->assignstmt.lvalue->op.optype ==
                        H64OP_ATTRIBUTEBYIDENTIFIER) {
                    h64instruction_setbyattributename inst = {0};
                    inst.type = H64INST_SETBYATTRIBUTENAME;
                    assert(expr->assignstmt.lvalue->
                           op.value1->storage.eval_temp_id >= 0);
                    inst.slotobjto = (
                        expr->assignstmt.lvalue->op.value1->
                        storage.eval_temp_id
                    );
                    assert(expr->assignstmt.lvalue->
                           op.value2->storage.eval_temp_id < 0);
                    assert(expr->assignstmt.lvalue->op.value2->type ==
                           H64EXPRTYPE_IDENTIFIERREF);
                    int64_t nameidx = (
                        h64debugsymbols_AttributeNameToAttributeNameId(
                            rinfo->pr->program->symbols,
                            expr->assignstmt.lvalue->op.value2->
                                identifierref.value, 0
                        )
                    );
                    if (nameidx < 0) {
                        // FIXME: hardcode AttributeError raise
                        assert(0);
                    } else {
                        inst.nameidx = nameidx;
                        inst.slotvaluefrom = assignfromtemporary;
                        if (!appendinst(
                                rinfo->pr->program, func, expr,
                                &inst, sizeof(inst))) {
                            rinfo->hadoutofmemory = 1;
                            return 0;
                        }
                    }
                } else {
                    assert(expr->assignstmt.lvalue->op.optype ==
                           H64OP_INDEXBYEXPR);
                    h64instruction_setbyindexexpr inst = {0};
                    inst.type = H64INST_SETBYINDEXEXPR;
                    assert(expr->assignstmt.lvalue->
                           op.value1->storage.eval_temp_id >= 0);
                    inst.slotobjto = (
                        expr->assignstmt.lvalue->op.value1->
                        storage.eval_temp_id
                    );
                    assert(expr->assignstmt.lvalue->
                           op.value2->storage.eval_temp_id >= 0);
                    inst.slotindexto = (
                        expr->assignstmt.lvalue->op.value2->
                        storage.eval_temp_id
                    );
                    inst.slotvaluefrom = assignfromtemporary;
                    if (!appendinst(
                            rinfo->pr->program, func, expr,
                            &inst, sizeof(inst))) {
                        rinfo->hadoutofmemory = 1;
                        return 0;
                    }
                }
            } else {
                // Simple assignment of form a = b
                h64instruction_valuecopy inst = {0};
                inst.type = H64INST_VALUECOPY;
                inst.slotto = str->id;
                inst.slotfrom = assignfromtemporary;
                if (!appendinst(rinfo->pr->program, func, expr,
                                &inst, sizeof(inst))) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
            }
        }
    } else {
        char buf[256];
        snprintf(buf, sizeof(buf) - 1,
            "internal error: unhandled expr type %d",
            (int)expr->type
        );
        if (!result_AddMessage(
                &rinfo->ast->resultmsg,
                H64MSG_ERROR, buf,
                rinfo->ast->fileuri,
                expr->line, expr->column
                )) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }
        return 1;
    }

    if (IS_STMT(expr->type))
        free1linetemps(func);

    return 1;
}

static int _enforce_dostmt_limit_in_func(
        asttransforminfo *rinfo, h64expression *func
        ) {
    if (func->funcdef._storageinfo->dostmts_used + 1 >=
            INT16_MAX - 1) {
        rinfo->hadunexpectederror = 1;
        if (!result_AddMessage(
                rinfo->pr->resultmsg,
                H64MSG_ERROR, "exceeded maximum of "
                "do or with statements in one function",
                NULL, -1, -1
                )) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }
        return 0;
    }
    return 1;
}

int _codegencallback_DoCodegen_visit_in(
        h64expression *expr, ATTR_UNUSED h64expression *parent, void *ud
        ) {
    asttransforminfo *rinfo = (asttransforminfo *)ud;

    h64expression *func = surroundingfunc(expr);
    if (!func) {
        h64expression *sclass = surroundingclass(expr, 0);
        if (sclass != NULL && expr->type != H64EXPRTYPE_FUNCDEF_STMT) {
            return 1;
        }
        func = _fakeglobalinitfunc(rinfo);
        if (!func && expr->type != H64EXPRTYPE_FUNCDEF_STMT) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }
    }

    if (IS_STMT(expr->type))
        free1linetemps(func);

    if (expr->type == H64EXPRTYPE_WHILE_STMT) {
        rinfo->dont_descend_visitation = 1;
        int32_t jumpid_start = (
            func->funcdef._storageinfo->jump_targets_used
        );
        func->funcdef._storageinfo->jump_targets_used++;
        int32_t jumpid_end = (
            func->funcdef._storageinfo->jump_targets_used
        );
        func->funcdef._storageinfo->jump_targets_used++;

        h64instruction_jumptarget inst_jumptarget = {0};
        inst_jumptarget.type = H64INST_JUMPTARGET;
        inst_jumptarget.jumpid = jumpid_start;
        if (!appendinst(
                rinfo->pr->program, func, expr,
                &inst_jumptarget, sizeof(inst_jumptarget))) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }

        rinfo->dont_descend_visitation = 0;
        int result = ast_VisitExpression(
            expr->whilestmt.conditional, expr,
            &_codegencallback_DoCodegen_visit_in,
            &_codegencallback_DoCodegen_visit_out,
            _asttransform_cancel_visit_descend_callback,
            rinfo
        );
        rinfo->dont_descend_visitation = 1;
        if (!result)
            return 0;

        h64instruction_condjump inst_condjump = {0};
        inst_condjump.type = H64INST_CONDJUMP;
        inst_condjump.conditionalslot = (
            expr->whilestmt.conditional->storage.eval_temp_id
        );
        inst_condjump.jumpbytesoffset = jumpid_end;
        if (!appendinst(
                rinfo->pr->program, func, expr,
                &inst_condjump, sizeof(inst_condjump))) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }

        int i = 0;
        while (i < expr->whilestmt.stmt_count) {
            rinfo->dont_descend_visitation = 0;
            result = ast_VisitExpression(
                expr->whilestmt.stmt[i], expr,
                &_codegencallback_DoCodegen_visit_in,
                &_codegencallback_DoCodegen_visit_out,
                _asttransform_cancel_visit_descend_callback,
                rinfo
            );
            rinfo->dont_descend_visitation = 1;
            if (!result)
                return 0;
            i++;
        }

        h64instruction_jump inst_jump = {0};
        inst_jump.type = H64INST_JUMP;
        inst_jump.jumpbytesoffset = jumpid_start;
        if (!appendinst(
                rinfo->pr->program, func, expr,
                &inst_jump, sizeof(inst_jump))) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }

        h64instruction_jumptarget inst_jumptargetend = {0};
        inst_jumptargetend.type = H64INST_JUMPTARGET;
        inst_jumptargetend.jumpid = jumpid_end;
        if (!appendinst(
                rinfo->pr->program, func, expr,
                &inst_jumptargetend, sizeof(inst_jumptargetend))) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }
        rinfo->dont_descend_visitation = 1;
        return 1;
    } else if (expr->type == H64EXPRTYPE_FUNCDEF_STMT) {
        rinfo->dont_descend_visitation = 1;

        int func_id = expr->funcdef.bytecode_func_id;

        // Handling of keyword arguments:
        int argtmp = (
            rinfo->pr->program->func[func_id].associated_class_index > 0 ?
            1 : 0
        );
        int i = 0;
        while (i < expr->funcdef.arguments.arg_count) {
            if (expr->funcdef.arguments.arg_value[i] != NULL) {
                assert(i + 1 >= expr->funcdef.arguments.arg_count ||
                       expr->funcdef.arguments.arg_value[i + 1] != NULL);
                assert(i > 0 || !expr->funcdef.arguments.
                           last_posarg_is_multiarg);
                int jump_past_id = (
                    func->funcdef._storageinfo->jump_targets_used
                );
                func->funcdef._storageinfo->jump_targets_used++;

                int operand2tmp = new1linetemp(
                    func, expr, 0
                );
                if (operand2tmp < 0) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }

                h64instruction_setconst inst_sconst = {0};
                inst_sconst.type = H64INST_SETCONST;
                inst_sconst.content.type = H64VALTYPE_UNSPECIFIED_KWARG;
                inst_sconst.slot = operand2tmp;

                h64instruction_binop inst_binop = {0};
                inst_binop.type = H64INST_BINOP;
                inst_binop.optype = H64OP_CMP_EQUAL;
                inst_binop.slotto = operand2tmp;
                inst_binop.arg1slotfrom = argtmp;
                inst_binop.arg2slotfrom = operand2tmp;
                if (!appendinst(
                        rinfo->pr->program, func, expr,
                        &inst_binop, sizeof(inst_binop))) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }

                h64instruction_condjump cjump = {0};
                cjump.type = H64INST_CONDJUMP;
                cjump.jumpbytesoffset = jump_past_id;
                if (!appendinst(
                        rinfo->pr->program, func, expr,
                        &cjump, sizeof(cjump))) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }

                free1linetemps(func);

                rinfo->dont_descend_visitation = 0;
                int result = ast_VisitExpression(
                    expr->funcdef.arguments.arg_value[i], expr,
                    &_codegencallback_DoCodegen_visit_in,
                    &_codegencallback_DoCodegen_visit_out,
                    _asttransform_cancel_visit_descend_callback,
                    rinfo
                );
                rinfo->dont_descend_visitation = 1;
                if (!result)
                    return 0;
                assert(expr->funcdef.arguments.arg_value[i]->
                    storage.eval_temp_id >= 0);

                h64instruction_valuecopy vc;
                vc.type = H64INST_VALUECOPY;
                vc.slotto = argtmp;
                vc.slotfrom = expr->funcdef.arguments.arg_value[i]->
                    storage.eval_temp_id;
                if (!appendinst(
                        rinfo->pr->program, func, expr,
                        &vc, sizeof(vc))) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }

                free1linetemps(func);
                h64instruction_jumptarget jumpt = {0};
                jumpt.type = H64INST_JUMPTARGET;
                jumpt.jumpid = jump_past_id;
                if (!appendinst(
                        rinfo->pr->program, func, expr,
                        &jumpt, sizeof(jumpt))) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
            }
            argtmp++;
            i++;
        }
        free1linetemps(func);

        i = 0;
        while (i < expr->funcdef.stmt_count) {
            rinfo->dont_descend_visitation = 0;
            int result = ast_VisitExpression(
                expr->funcdef.stmt[i], expr,
                &_codegencallback_DoCodegen_visit_in,
                &_codegencallback_DoCodegen_visit_out,
                _asttransform_cancel_visit_descend_callback,
                rinfo
            );
            rinfo->dont_descend_visitation = 1;
            if (!result)
                return 0;
            free1linetemps(func);
            i++;
        }

        free1linetemps(func);
        rinfo->dont_descend_visitation = 1;
        return 1;
    } else if (expr->type == H64EXPRTYPE_UNARYOP &&
               expr->op.optype == H64OP_NEW) {
        rinfo->dont_descend_visitation = 1;

        // This should have been enforced by the parser:
        assert(expr->op.value1->type == H64EXPRTYPE_CALL);

        // Visit all arguments of constructor call:
        int i = 0;
        while (i < expr->op.value1->funcdef.arguments.arg_count) {
            if (!ast_VisitExpression(
                    expr->op.value1->funcdef.arguments.arg_value[i],
                    expr,
                    &_codegencallback_DoCodegen_visit_in,
                    &_codegencallback_DoCodegen_visit_out,
                    _asttransform_cancel_visit_descend_callback,
                    rinfo
                    ))
                return 0;
            i++;
        }

        int objslot = -1;
        if (expr->op.value1->inlinecall.value->type !=
                    H64EXPRTYPE_IDENTIFIERREF ||
                expr->op.value1->inlinecall.value->storage.set ||
                expr->op.value1->inlinecall.value->storage.ref.type !=
                    H64STORETYPE_GLOBALCLASSSLOT) {
            // Not mapping to a class type we can obviously identify
            // at compile time. -> must obtain this at runtime.

            // Visit called object (= constructed type) to get temporary:
            rinfo->dont_descend_visitation = 0;
            int result = ast_VisitExpression(
                expr->op.value1->inlinecall.value, expr,
                &_codegencallback_DoCodegen_visit_in,
                &_codegencallback_DoCodegen_visit_out,
                _asttransform_cancel_visit_descend_callback,
                rinfo
            );
            rinfo->dont_descend_visitation = 1;
            if (!result)
                return 0;

            // The temporary cannot be a final variable, since if
            // the constructor errors that would leave us with
            // an invalid incomplete object possibly still accessible
            // by "rescue" code accessing that variable:
            objslot = expr->op.value1->inlinecall.value->
                storage.eval_temp_id;
            if (objslot < 0) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
            assert(objslot >= 0);
            if (objslot < func->funcdef._storageinfo->
                    lowest_guaranteed_free_temp) {
                // This is a fixed variable or argument.
                objslot = new1linetemp(func, expr, 0);
                assert(objslot >= func->funcdef._storageinfo->
                       lowest_guaranteed_free_temp);
            }

            // Convert it to object instance:
            h64instruction_newinstancebyref inst_newinstbyref = {0};
            inst_newinstbyref.type = H64INST_NEWINSTANCEBYREF;
            inst_newinstbyref.slotto = objslot;
            inst_newinstbyref.classtypeslotfrom = (
                expr->op.value1->inlinecall.value->storage.eval_temp_id
            );
            if (!appendinst(
                    rinfo->pr->program, func, expr,
                    &inst_newinstbyref, sizeof(inst_newinstbyref))) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
        } else {
            // Apparently we already know the class id at compile time,
            // so instantiate it directly:

            // Slot to hold resulting object instance:
            objslot = new1linetemp(func, expr, 0);
            if (objslot < 0) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
            assert(objslot >= 0);
            assert(
                objslot >= func->funcdef._storageinfo->
                lowest_guaranteed_free_temp
            );  // must not be variable

            // Create object instance directly from given class:
            h64instruction_newinstance inst_newinst = {0};
            inst_newinst.type = H64INST_NEWINSTANCE;
            inst_newinst.slotto = objslot;
            inst_newinst.classidcreatefrom = ((int64_t)
                expr->op.value1->inlinecall.value->storage.ref.id
            );
            if (!appendinst(
                    rinfo->pr->program, func, expr,
                    &inst_newinst, sizeof(inst_newinst))) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
        }
        // Prepare unused temporary for constructor call:
        int temp = new1linetemp(func, expr, 0);
        if (temp < 0) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }
        // Place constructor in unused result temporary:
        h64instruction_getconstructor inst_getconstr = {0};
        inst_getconstr.type = H64INST_GETCONSTRUCTOR;
        inst_getconstr.slotto = temp;
        inst_getconstr.objslotfrom = objslot;
        if (!appendinst(
                rinfo->pr->program, func, expr,
                &inst_getconstr, sizeof(inst_getconstr))) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }
        // Generate call to actual constructor:
        assert(func != NULL && expr != NULL);
        if (!_codegen_call_to(
                rinfo, func, expr->op.value1, temp, temp, 1
                )) {
            return 0;
        }
        // Move object to result:
        int resulttemp = new1linetemp(func, expr, 1);
        if (resulttemp < 0) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }
        if (objslot != resulttemp) {
            h64instruction_valuecopy inst_vc = {0};
            inst_vc.type = H64INST_VALUECOPY;
            inst_vc.slotto = resulttemp;
            inst_vc.slotfrom = objslot;
            if (!appendinst(
                    rinfo->pr->program, func, expr,
                    &inst_vc, sizeof(inst_vc))) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
        }

        free1linetemps(func);
        rinfo->dont_descend_visitation = 1;
        expr->storage.eval_temp_id = resulttemp;
        return 1;
    } else if (expr->type == H64EXPRTYPE_WITH_STMT) {
        rinfo->dont_descend_visitation = 1;

        int32_t jumpid_finally = (
            func->funcdef._storageinfo->jump_targets_used
        );
        func->funcdef._storageinfo->jump_targets_used++;

        // First, set all the temporaries of the "with'ed" values to none:
        assert(expr->withstmt.withclause_count >= 1);
        int32_t i = 0;
        while (i < expr->withstmt.withclause_count) {
            assert(
                expr->withstmt.withclause[i]->
                storage.eval_temp_id >= 0 || (
                expr->withstmt.withclause[i]->storage.set &&
                expr->withstmt.withclause[i]->storage.ref.type ==
                    H64STORETYPE_STACKSLOT)
            );
            h64instruction_setconst inst_setconst = {0};
            inst_setconst.type = H64INST_SETCONST;
            inst_setconst.slot = (
                expr->withstmt.withclause[i]->storage.eval_temp_id >= 0 ?
                expr->withstmt.withclause[i]->storage.eval_temp_id :
                expr->withstmt.withclause[i]->storage.ref.id
            );
            memset(
                &inst_setconst.content, 0, sizeof(inst_setconst.content)
            );
            inst_setconst.content.type = H64VALTYPE_NONE;
            if (!appendinst(
                    rinfo->pr->program, func, expr,
                    &inst_setconst, sizeof(inst_setconst))) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
            i++;
        }

        // Ok, before setting any of the true values,
        // set up the error catch frame in case the init already errors:
        if (!_enforce_dostmt_limit_in_func(rinfo, func))
            return 0;
        int16_t dostmtid = (
            func->funcdef._storageinfo->dostmts_used
        );
        func->funcdef._storageinfo->dostmts_used++;
        h64instruction_pushcatchframe inst_pushframe = {0};
        inst_pushframe.type = H64INST_PUSHCATCHFRAME;
        inst_pushframe.sloterrorto = -1;
        inst_pushframe.jumponcatch = -1;
        inst_pushframe.jumponfinally = jumpid_finally;
        inst_pushframe.mode = CATCHMODE_JUMPONFINALLY;
        inst_pushframe.frameid = dostmtid;
        if (!appendinst(
                rinfo->pr->program, func, expr,
                &inst_pushframe, sizeof(inst_pushframe))) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }
        h64instruction_addcatchtype addctype = {0};
        addctype.type = H64INST_ADDCATCHTYPE;
        addctype.frameid = dostmtid;
        addctype.classid = (classid_t)H64STDERROR_ERROR;
        if (!appendinst(
                rinfo->pr->program, func, expr,
                &addctype, sizeof(addctype))) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }

        // Visit with'ed values to generate their code:
        i = 0;
        while (i < expr->withstmt.withclause_count) {
            rinfo->dont_descend_visitation = 0;
            int result = ast_VisitExpression(
                expr->withstmt.withclause[i], expr,
                &_codegencallback_DoCodegen_visit_in,
                &_codegencallback_DoCodegen_visit_out,
                _asttransform_cancel_visit_descend_callback,
                rinfo
            );
            rinfo->dont_descend_visitation = 1;
            if (!result)
                return 0;
            i++;
        }

        // Inner code contents:
        i = 0;
        while (i < expr->withstmt.stmt_count) {
            rinfo->dont_descend_visitation = 0;
            int result = ast_VisitExpression(
                expr->withstmt.stmt[i], expr,
                &_codegencallback_DoCodegen_visit_in,
                &_codegencallback_DoCodegen_visit_out,
                _asttransform_cancel_visit_descend_callback,
                rinfo
            );
            rinfo->dont_descend_visitation = 1;
            if (!result)
                return 0;
            free1linetemps(func);
            i++;
        }

        // NOTE: jumptofinally is needed even if finally block follows
        // immediately, such that horsevm knows that the finally
        // was already triggered. (Important if another error were
        // to happen.)
        h64instruction_jumptofinally inst_jumptofinally = {0};
        inst_jumptofinally.type = H64INST_JUMPTOFINALLY;
        inst_jumptofinally.frameid = dostmtid;
        if (!appendinst(
                rinfo->pr->program, func, expr,
                &inst_jumptofinally, sizeof(inst_jumptofinally))) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }

        // Start of finally block:
        h64instruction_jumptarget inst_jumpfinally = {0};
        inst_jumpfinally.type = H64INST_JUMPTARGET;
        inst_jumpfinally.jumpid = jumpid_finally;
        if (!appendinst(
                rinfo->pr->program, func, expr,
                &inst_jumpfinally, sizeof(inst_jumpfinally))) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }

        // Call .close() on all objects that have that property.
        // However, we need to wrap them all with tiny do/finally clauses
        // such that even when one of them fails, the others still
        // attempt to run afterwards.
        int16_t *_withclause_catchframeid = (
            malloc(sizeof(*_withclause_catchframeid) *
                   expr->withstmt.withclause_count)
        );
        if (!_withclause_catchframeid) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }
        int32_t *_withclause_jumpfinallyid = (
            malloc(sizeof(*_withclause_jumpfinallyid) *
                   expr->withstmt.withclause_count)
        );
        if (!_withclause_jumpfinallyid) {
            free(_withclause_catchframeid);
            rinfo->hadoutofmemory = 1;
            return 0;
        }
        memset(
            _withclause_catchframeid, 0,
            sizeof(*_withclause_catchframeid) *
                expr->withstmt.withclause_count
        );
        memset(
            _withclause_jumpfinallyid, 0,
            sizeof(*_withclause_jumpfinallyid) *
                expr->withstmt.withclause_count
        );
        // Add nested do {first.close()} finally {second.close() ...}
        i = 0;
        while (i < expr->withstmt.withclause_count) {
            int gotfinally = 0;
            if (i + 1 < expr->withstmt.withclause_count) {
                if (!_enforce_dostmt_limit_in_func(rinfo, func)) {
                    free(_withclause_catchframeid);
                    free(_withclause_jumpfinallyid);
                    return 0;
                }
                gotfinally = 1;
                _withclause_catchframeid[i] = (
                    func->funcdef._storageinfo->dostmts_used
                );
                func->funcdef._storageinfo->dostmts_used++;
                _withclause_jumpfinallyid[i] = (
                    func->funcdef._storageinfo->jump_targets_used
                );
                func->funcdef._storageinfo->jump_targets_used++;
                h64instruction_pushcatchframe inst_pushframe2 = {0};
                inst_pushframe2.type = H64INST_PUSHCATCHFRAME;
                inst_pushframe2.sloterrorto = -1;
                inst_pushframe2.jumponcatch = -1;
                inst_pushframe2.jumponfinally = (
                    _withclause_jumpfinallyid[i]
                );
                inst_pushframe2.frameid = (
                    _withclause_catchframeid[i]
                );
                inst_pushframe2.mode = CATCHMODE_JUMPONFINALLY;
                if (!appendinst(
                        rinfo->pr->program, func, expr,
                        &inst_pushframe2, sizeof(inst_pushframe2))) {
                    free(_withclause_catchframeid);
                    free(_withclause_jumpfinallyid);
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
                h64instruction_addcatchtype addctype2 = {0};
                addctype2.type = H64INST_ADDCATCHTYPE;
                addctype2.frameid = (
                    _withclause_catchframeid[i]
                );
                addctype2.classid = (classid_t)H64STDERROR_ERROR;
                if (!appendinst(
                        rinfo->pr->program, func, expr,
                        &addctype2, sizeof(addctype2))) {
                    free(_withclause_catchframeid);
                    free(_withclause_jumpfinallyid);
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
            }
            int32_t jump_past_hasattr_id = (
                func->funcdef._storageinfo->jump_targets_used
            );
            func->funcdef._storageinfo->jump_targets_used++;
            // Check if value has .close() attribute, and call it:
            int64_t closeidx = (
                h64debugsymbols_AttributeNameToAttributeNameId(
                    rinfo->pr->program->symbols, "close", 0
                )
            );
            if (closeidx >= 0) {
                h64instruction_hasattrjump hasattrcheck = {0};
                hasattrcheck.type = H64INST_HASATTRJUMP;
                hasattrcheck.jumpbytesoffset = (
                    jump_past_hasattr_id
                );
                hasattrcheck.nameidxcheck = closeidx;
                hasattrcheck.slotvaluecheck = (
                    expr->withstmt.withclause[i]->storage.eval_temp_id
                );
                if (!appendinst(
                        rinfo->pr->program, func, expr,
                        &hasattrcheck, sizeof(hasattrcheck))) {
                    free(_withclause_catchframeid);
                    free(_withclause_jumpfinallyid);
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
                // It has .close() attribute, so get & call it:
                int16_t slotid = new1linetemp(
                    func, NULL, 0
                );
                h64instruction_getattributebyname abyname = {0};
                abyname.type = H64INST_GETATTRIBUTEBYNAME;
                abyname.objslotfrom = (
                    expr->withstmt.withclause[i]->storage.eval_temp_id
                );
                abyname.slotto = slotid;
                abyname.nameidx = closeidx;
                if (!appendinst(
                        rinfo->pr->program, func, expr,
                        &abyname, sizeof(abyname))) {
                    free(_withclause_catchframeid);
                    free(_withclause_jumpfinallyid);
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
                h64instruction_call callclose = {0};
                callclose.type = H64INST_CALL;
                callclose.slotcalledfrom = slotid;
                callclose.async = 0;
                callclose.expandlastposarg = 0;
                callclose.kwargs = 0;
                callclose.posargs = 0;
                callclose.returnto = slotid;
                if (!appendinst(
                        rinfo->pr->program, func, expr,
                        &callclose, sizeof(callclose))) {
                    free(_withclause_catchframeid);
                    free(_withclause_jumpfinallyid);
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
                free1linetemps(func);
            } else {
                h64instruction_jump skipattrcheck = {0};
                skipattrcheck.type = H64INST_JUMP;
                skipattrcheck.jumpbytesoffset = (
                    jump_past_hasattr_id
                );
                if (!appendinst(
                        rinfo->pr->program, func, expr,
                        &skipattrcheck, sizeof(skipattrcheck))) {
                    free(_withclause_catchframeid);
                    free(_withclause_jumpfinallyid);
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
            }
            h64instruction_jumptarget pastchecktarget = {0};
            pastchecktarget.type = H64INST_JUMPTARGET;
            pastchecktarget.jumpid = jump_past_hasattr_id;
            if (gotfinally) {
                h64instruction_jumptofinally nowtofinally = {0};
                nowtofinally.type = H64INST_JUMPTOFINALLY;
                nowtofinally.frameid = (
                    _withclause_catchframeid[i]
                );
                if (!appendinst(
                        rinfo->pr->program, func, expr,
                        &nowtofinally, sizeof(nowtofinally))) {
                    free(_withclause_catchframeid);
                    free(_withclause_jumpfinallyid);
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
                h64instruction_jumptarget finallytarget = {0};
                finallytarget.type = H64INST_JUMPTARGET;
                finallytarget.jumpid = (
                    _withclause_jumpfinallyid[i]
                );
            }
            i++;
        }
        // Pop all the catch frames again in reverse, at the end:
        i = expr->withstmt.withclause_count - 1;
        while (i >= 0) {
            int gotfinally = 0;
            if (i + 1 < expr->withstmt.withclause_count)
                gotfinally = 1;
            if (gotfinally) {
                h64instruction_popcatchframe inst_popcatch = {0};
                inst_popcatch.type = H64INST_POPCATCHFRAME;
                inst_popcatch.frameid = (
                    _withclause_catchframeid[i]
                );
                if (!appendinst(
                        rinfo->pr->program, func, expr,
                        &inst_popcatch, sizeof(inst_popcatch))) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
            }
            i--;
        }

        // End of entire block here.
        h64instruction_popcatchframe inst_popcatch = {0};
        inst_popcatch.type = H64INST_POPCATCHFRAME;
        inst_popcatch.frameid = dostmtid;
        if (!appendinst(
                rinfo->pr->program, func, expr,
                &inst_popcatch, sizeof(inst_popcatch))) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }

        free1linetemps(func);
        rinfo->dont_descend_visitation = 1;
        return 1;
    } else if (expr->type == H64EXPRTYPE_DO_STMT) {
        rinfo->dont_descend_visitation = 1;

        int32_t jumpid_catch = -1;
        int32_t jumpid_finally = -1;
        int32_t jumpid_end = (
            func->funcdef._storageinfo->jump_targets_used
        );
        func->funcdef._storageinfo->jump_targets_used++;

        if (!_enforce_dostmt_limit_in_func(rinfo, func))
            return 0;
        int16_t dostmtid = (
            func->funcdef._storageinfo->dostmts_used
        );
        func->funcdef._storageinfo->dostmts_used++;

        h64instruction_pushcatchframe inst_pushframe = {0};
        inst_pushframe.type = H64INST_PUSHCATCHFRAME;
        inst_pushframe.sloterrorto = -1;
        inst_pushframe.jumponcatch = -1;
        inst_pushframe.jumponfinally = -1;
        inst_pushframe.frameid = dostmtid;
        if (expr->dostmt.errors_count > 0) {
            assert(!expr->storage.set ||
                   expr->storage.ref.type ==
                   H64STORETYPE_STACKSLOT);
            int error_tmp = -1;
            if (expr->storage.set) {
                error_tmp = expr->storage.ref.id;
            }
            inst_pushframe.sloterrorto = error_tmp;
            inst_pushframe.mode |= CATCHMODE_JUMPONCATCH;
            jumpid_catch = (
                func->funcdef._storageinfo->jump_targets_used
            );
            func->funcdef._storageinfo->jump_targets_used++;
            inst_pushframe.jumponcatch = jumpid_catch;
        }
        if (expr->dostmt.has_finally_block) {
            inst_pushframe.mode |= CATCHMODE_JUMPONFINALLY;
            jumpid_finally = (
                func->funcdef._storageinfo->jump_targets_used
            );
            func->funcdef._storageinfo->jump_targets_used++;
            inst_pushframe.jumponfinally = jumpid_finally;
        }
        if (!appendinst(
                rinfo->pr->program, func, expr,
                &inst_pushframe, sizeof(inst_pushframe))) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }

        int error_reuse_tmp = -1;
        int i = 0;
        while (i < expr->dostmt.errors_count) {
            assert(expr->dostmt.errors[i]->storage.set);
            int error_tmp = -1;
            if (expr->dostmt.errors[i]->storage.ref.type ==
                    H64STORETYPE_STACKSLOT) {
                error_tmp = (int)(
                    expr->dostmt.errors[i]->storage.ref.id
                );
            } else if (expr->dostmt.errors[i]->storage.ref.type ==
                       H64STORETYPE_GLOBALCLASSSLOT) {
                h64instruction_addcatchtype addctype = {0};
                addctype.type = H64INST_ADDCATCHTYPE;
                addctype.frameid = dostmtid;
                addctype.classid = (
                    expr->dostmt.errors[i]->
                        storage.ref.id
                );
                if (!appendinst(
                        rinfo->pr->program, func, expr,
                        &addctype, sizeof(addctype))) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
                i++;
                continue;
            } else {
                assert(expr->dostmt.errors[i]->storage.ref.type ==
                       H64STORETYPE_GLOBALVARSLOT);
                if (error_reuse_tmp < 0) {
                    error_reuse_tmp = new1linetemp(
                        func, expr, 0
                    );
                    if (error_reuse_tmp < 0) {
                        rinfo->hadoutofmemory = 1;
                        return 0;
                    }
                }
                error_tmp = error_reuse_tmp;
                h64instruction_getglobal inst_getglobal = {0};
                inst_getglobal.type = H64INST_GETGLOBAL;
                inst_getglobal.slotto = error_tmp;
                inst_getglobal.globalfrom = expr->dostmt.errors[i]->
                    storage.ref.id;
                if (!appendinst(
                        rinfo->pr->program, func, expr,
                        &inst_getglobal, sizeof(inst_getglobal))) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
            }
            assert(error_tmp >= 0);
            h64instruction_addcatchtypebyref addctyperef = {0};
            addctyperef.type = H64INST_ADDCATCHTYPEBYREF;
            addctyperef.slotfrom = error_tmp;
            addctyperef.frameid = dostmtid;
            if (!appendinst(
                    rinfo->pr->program, func, expr,
                    &addctyperef, sizeof(addctyperef))) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
            i++;
        }

        i = 0;
        while (i < expr->dostmt.dostmt_count) {
            rinfo->dont_descend_visitation = 0;
            int result = ast_VisitExpression(
                expr->dostmt.dostmt[i], expr,
                &_codegencallback_DoCodegen_visit_in,
                &_codegencallback_DoCodegen_visit_out,
                _asttransform_cancel_visit_descend_callback,
                rinfo
            );
            rinfo->dont_descend_visitation = 1;
            if (!result)
                return 0;
            free1linetemps(func);
            i++;
        }
        if ((inst_pushframe.mode & CATCHMODE_JUMPONFINALLY) == 0) {
            h64instruction_popcatchframe inst_popcatch = {0};
            inst_popcatch.type = H64INST_POPCATCHFRAME;
            inst_popcatch.frameid = dostmtid;
            if (!appendinst(
                    rinfo->pr->program, func, expr,
                    &inst_popcatch, sizeof(inst_popcatch))) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
            if ((inst_pushframe.mode & CATCHMODE_JUMPONCATCH) != 0) {
                h64instruction_jump inst_jump = {0};
                inst_jump.type = H64INST_JUMP;
                inst_jump.jumpbytesoffset = jumpid_end;
                if (!appendinst(
                        rinfo->pr->program, func, expr,
                        &inst_jump, sizeof(inst_jump))) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
            }
        } else {
            // NOTE: this is needed even when the finally block follows
            // immediately, such that horsevm knows that the finally
            // was already triggered. (Important if another error were
            // to happen.)
            h64instruction_jumptofinally inst_jumptofinally = {0};
            inst_jumptofinally.type = H64INST_JUMPTOFINALLY;
            inst_jumptofinally.frameid = dostmtid;
            if (!appendinst(
                    rinfo->pr->program, func, expr,
                    &inst_jumptofinally, sizeof(inst_jumptofinally))) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
        }

        if ((inst_pushframe.mode & CATCHMODE_JUMPONCATCH) != 0) {
            h64instruction_jumptarget inst_jumpcatch = {0};
            inst_jumpcatch.type = H64INST_JUMPTARGET;
            inst_jumpcatch.jumpid = jumpid_catch;
            if (!appendinst(
                    rinfo->pr->program, func, expr,
                    &inst_jumpcatch, sizeof(inst_jumpcatch))) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }

            i = 0;
            while (i < expr->dostmt.rescuestmt_count) {
                rinfo->dont_descend_visitation = 0;
                int result = ast_VisitExpression(
                    expr->dostmt.rescuestmt[i], expr,
                    &_codegencallback_DoCodegen_visit_in,
                    &_codegencallback_DoCodegen_visit_out,
                    _asttransform_cancel_visit_descend_callback,
                    rinfo
                );
                rinfo->dont_descend_visitation = 1;
                if (!result)
                    return 0;
                free1linetemps(func);
                i++;
            }
            if ((inst_pushframe.mode & CATCHMODE_JUMPONFINALLY) == 0) {
                // No finally follows, so we need to clean up the
                // error frame here.
                h64instruction_popcatchframe inst_popcatch = {0};
                inst_popcatch.type = H64INST_POPCATCHFRAME;
                inst_popcatch.frameid = dostmtid;
                if (!appendinst(
                        rinfo->pr->program, func, expr,
                        &inst_popcatch, sizeof(inst_popcatch))) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
            } else {
                // Just let execution continue since it'll roll into
                // the finally block that is generated right below.
            }
        }

        if ((inst_pushframe.mode & CATCHMODE_JUMPONFINALLY) != 0) {
            h64instruction_jumptarget inst_jumpfinally = {0};
            inst_jumpfinally.type = H64INST_JUMPTARGET;
            inst_jumpfinally.jumpid = jumpid_finally;
            if (!appendinst(
                    rinfo->pr->program, func, expr,
                    &inst_jumpfinally, sizeof(inst_jumpfinally))) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }

            i = 0;
            while (i < expr->dostmt.finallystmt_count) {
                rinfo->dont_descend_visitation = 0;
                int result = ast_VisitExpression(
                    expr->dostmt.finallystmt[i], expr,
                    &_codegencallback_DoCodegen_visit_in,
                    &_codegencallback_DoCodegen_visit_out,
                    _asttransform_cancel_visit_descend_callback,
                    rinfo
                );
                rinfo->dont_descend_visitation = 1;
                if (!result)
                    return 0;
                free1linetemps(func);
                i++;
            }
            h64instruction_popcatchframe inst_popcatch = {0};
            inst_popcatch.type = H64INST_POPCATCHFRAME;
            inst_popcatch.frameid = dostmtid;
            if (!appendinst(
                    rinfo->pr->program, func, expr,
                    &inst_popcatch, sizeof(inst_popcatch))) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
        }

        h64instruction_jumptarget inst_jumpend = {0};
        inst_jumpend.type = H64INST_JUMPTARGET;
        inst_jumpend.jumpid = jumpid_end;
        if (!appendinst(
                rinfo->pr->program, func, expr,
                &inst_jumpend, sizeof(inst_jumpend))) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }

        free1linetemps(func);
        rinfo->dont_descend_visitation = 1;
        return 1;
    } else if (expr->type == H64EXPRTYPE_FOR_STMT) {
        rinfo->dont_descend_visitation = 1;
        int32_t jumpid_start = (
            func->funcdef._storageinfo->jump_targets_used
        );
        func->funcdef._storageinfo->jump_targets_used++;
        int32_t jumpid_end = (
            func->funcdef._storageinfo->jump_targets_used
        );
        func->funcdef._storageinfo->jump_targets_used++;

        int itertemp = newmultilinetemp(func);
        if (itertemp < 0) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }

        // Visit container value to get the slot of where it's stored:
        rinfo->dont_descend_visitation = 0;
        assert(expr->forstmt.iterated_container->
               storage.eval_temp_id <= 0);
        expr->forstmt.iterated_container->storage.eval_temp_id = -1;
        int result = ast_VisitExpression(
            expr->forstmt.iterated_container, expr,
            &_codegencallback_DoCodegen_visit_in,
            &_codegencallback_DoCodegen_visit_out,
            _asttransform_cancel_visit_descend_callback,
            rinfo
        );
        if (!result) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }
        rinfo->dont_descend_visitation = 1;
        int containertemp = -1;
        assert(expr->forstmt.iterated_container->
                   storage.eval_temp_id >= 0);
        containertemp = (
            expr->forstmt.iterated_container->storage.eval_temp_id
        );

        h64instruction_newiterator inst_newiter = {0};
        inst_newiter.type = H64INST_NEWITERATOR;
        inst_newiter.slotiteratorto = itertemp;
        inst_newiter.slotcontainerfrom = containertemp;

        if (!appendinst(
                rinfo->pr->program, func, expr,
                &inst_newiter, sizeof(inst_newiter))) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }

        h64instruction_jumptarget inst_jumpstart = {0};
        inst_jumpstart.type = H64INST_JUMPTARGET;
        inst_jumpstart.jumpid = jumpid_start;
        if (!appendinst(
                rinfo->pr->program, func, expr,
                &inst_jumpstart, sizeof(inst_jumpstart))) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }

        assert(
            expr->storage.set &&
            expr->storage.ref.type == H64STORETYPE_STACKSLOT
        );
        h64instruction_iterate inst_iterate = {0};
        inst_iterate.type = H64INST_ITERATE;
        inst_iterate.slotvalueto = expr->storage.ref.id;
        inst_iterate.slotiteratorfrom = itertemp;
        inst_iterate.jumponend = jumpid_end;
        if (!appendinst(
                rinfo->pr->program, func, expr,
                &inst_iterate, sizeof(inst_iterate))) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }

        int i = 0;
        while (i < expr->forstmt.stmt_count) {
            rinfo->dont_descend_visitation = 0;
            int result = ast_VisitExpression(
                expr->forstmt.stmt[i], expr,
                &_codegencallback_DoCodegen_visit_in,
                &_codegencallback_DoCodegen_visit_out,
                _asttransform_cancel_visit_descend_callback,
                rinfo
            );
            rinfo->dont_descend_visitation = 1;
            if (!result)
                return 0;
            i++;
        }

        h64instruction_jump inst_jump = {0};
        inst_jump.type = H64INST_JUMP;
        inst_jump.jumpbytesoffset = jumpid_start;
        if (!appendinst(
                rinfo->pr->program, func, expr,
                &inst_jump, sizeof(inst_jump))) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }

        h64instruction_jumptarget inst_jumpend = {0};
        inst_jumpend.type = H64INST_JUMPTARGET;
        inst_jumpend.jumpid = jumpid_end;
        if (!appendinst(
                rinfo->pr->program, func, expr,
                &inst_jumpend, sizeof(inst_jumpend))) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }

        freemultilinetemp(func, itertemp);
        rinfo->dont_descend_visitation = 1;
        return 1;
    } else if (expr->type == H64EXPRTYPE_IF_STMT) {
        rinfo->dont_descend_visitation = 1;
        int32_t jumpid_end = (
            func->funcdef._storageinfo->jump_targets_used
        );
        func->funcdef._storageinfo->jump_targets_used++;

        struct h64ifstmt *current_clause = &expr->ifstmt;
        assert(current_clause->conditional != NULL);
        while (current_clause != NULL) {
            int32_t jumpid_nextclause = -1;
            if (current_clause->followup_clause) {
                jumpid_nextclause = (
                    func->funcdef._storageinfo->jump_targets_used
                );
                func->funcdef._storageinfo->jump_targets_used++;
            }

            assert(
                !current_clause->conditional ||
                current_clause->conditional->parent == expr
            );
            if (current_clause->conditional) {
                rinfo->dont_descend_visitation = 0;
                int result = ast_VisitExpression(
                    current_clause->conditional, expr,
                    &_codegencallback_DoCodegen_visit_in,
                    &_codegencallback_DoCodegen_visit_out,
                    _asttransform_cancel_visit_descend_callback,
                    rinfo
                );
                rinfo->dont_descend_visitation = 1;
                if (!result)
                    return 0;

                h64instruction_condjump inst_condjump = {0};
                inst_condjump.type = H64INST_CONDJUMP;
                inst_condjump.conditionalslot = (
                    current_clause->conditional->storage.eval_temp_id
                );
                inst_condjump.jumpbytesoffset = (
                    current_clause->followup_clause != NULL ?
                    jumpid_nextclause : jumpid_end
                );
                if (!appendinst(
                        rinfo->pr->program, func, expr,
                        &inst_condjump, sizeof(inst_condjump))) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
            }

            int i = 0;
            while (i < current_clause->stmt_count) {
                rinfo->dont_descend_visitation = 0;
                int result = ast_VisitExpression(
                    current_clause->stmt[i], expr,
                    &_codegencallback_DoCodegen_visit_in,
                    &_codegencallback_DoCodegen_visit_out,
                    _asttransform_cancel_visit_descend_callback,
                    rinfo
                );
                rinfo->dont_descend_visitation = 1;
                if (!result)
                    return 0;
                i++;
            }

            if (current_clause->followup_clause != NULL) {
                h64instruction_jump inst_jump = {0};
                inst_jump.type = H64INST_JUMP;
                inst_jump.jumpbytesoffset = jumpid_end;
                if (!appendinst(
                        rinfo->pr->program, func, expr,
                        &inst_jump, sizeof(inst_jump))) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
            }

            h64instruction_jumptarget inst_jumptarget = {0};
            inst_jumptarget.type = H64INST_JUMPTARGET;
            if (current_clause->followup_clause == NULL) {
                inst_jumptarget.jumpid = jumpid_end;
            } else {
                inst_jumptarget.jumpid = jumpid_nextclause;
            }
            if (!appendinst(
                    rinfo->pr->program, func, expr,
                    &inst_jumptarget, sizeof(inst_jumptarget))) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
            rinfo->dont_descend_visitation = 1;
            current_clause = current_clause->followup_clause;
        }
        rinfo->dont_descend_visitation = 1;
        return 1;
    }

    return 1;
}

static int _codegen_calc_tempclassfakeinitfuncstack_cb(
        ATTR_UNUSED hashmap *map, const char *bytes,
        uint64_t byteslen, uint64_t number,
        void *userdata
        ) {
    struct asttransforminfo *fiterinfo = (
        (struct asttransforminfo *)userdata
    );
    classid_t classidx;
    assert(byteslen == sizeof(classidx));
    memcpy(&classidx, bytes, byteslen);
    h64expression *func = (h64expression *)(uintptr_t)number;
    assert(func != NULL);
    assert(fiterinfo->pr->program != NULL);
    codegen_CalculateFinalFuncStack(
        fiterinfo->pr->program, func
    );
    return 1;
}

int codegen_GenerateBytecodeForFile(
        h64compileproject *project, h64misccompileroptions *miscoptions,
        h64ast *resolved_ast
        ) {
    if (!project || !resolved_ast)
        return 0;

    if (miscoptions->compiler_stage_debug) {
        fprintf(
            stderr, "horsec: debug: codegen_GenerateBytecodeForFile "
                "start on %s (pr->resultmsg.success: %d)\n",
            resolved_ast->fileuri, project->resultmsg->success
        );
    }

    // Do actual codegen step:
    int transformresult = asttransform_Apply(
        project, resolved_ast,
        &_codegencallback_DoCodegen_visit_in,
        &_codegencallback_DoCodegen_visit_out,
        NULL
    );
    if (!transformresult)
        return 0;
    // Ensure final stack is calculated for "made-up" func expressions:
    {
        asttransforminfo rinfo = {0};
        rinfo.pr = project;
        rinfo.ast = resolved_ast;
        if (project->_tempglobalfakeinitfunc) {
            codegen_CalculateFinalFuncStack(
                project->program, project->_tempglobalfakeinitfunc
            );
        }
        if (project->_tempclassesfakeinitfunc_map) {
            int iterresult = hash_BytesMapIterate(
                project->_tempclassesfakeinitfunc_map,
                &_codegen_calc_tempclassfakeinitfuncstack_cb,
                &rinfo
            );
            if (!iterresult || rinfo.hadoutofmemory ||
                    rinfo.hadunexpectederror) {
                if (!result_AddMessage(
                        project->resultmsg,
                        H64MSG_ERROR, "unexpected _codegen_calc_"
                        "tempclassfakeinitfuncstack_cb iteration "
                        "failure",
                        NULL, -1, -1
                        )) {
                    // Nothing we can do
                }
                return 0;
            }
        }
    }

    // Transform jump instructions to final offsets:
    if (!codegen_FinalBytecodeTransform(
            project
            )) {
        project->resultmsg->success = 0;
        char buf[256];
        snprintf(buf, sizeof(buf) - 1,
            "internal error: jump offset calculation "
            "failed, out of memory or codegen bug?"
        );
        if (!result_AddMessage(
                project->resultmsg,
                H64MSG_ERROR, buf,
                NULL, -1, -1
                )) {
            // Nothing we can do
        }
        return 0;  // since always OOM if no major compiler bug,
                   // so return OOM indication
    }

    if (miscoptions->compiler_stage_debug) {
        fprintf(
            stderr, "horsec: debug: codegen_GenerateBytecodeForFile "
                "completed on %s (pr->resultmsg.success: %d)\n",
            resolved_ast->fileuri, project->resultmsg->success
        );
    }

    return 1;
}
