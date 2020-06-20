
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "compiler/ast.h"
#include "compiler/asthelpers.h"
#include "compiler/astparser.h"
#include "compiler/asttransform.h"
#include "compiler/codegen.h"
#include "compiler/compileproject.h"
#include "compiler/varstorage.h"
#include "unicode.h"


static void get_assign_lvalue_storage(
        h64expression *expr,
        storageref **out_storageref
        ) {
    assert(expr->type == H64EXPRTYPE_ASSIGN_STMT);
    if (expr->assignstmt.lvalue->type ==
            H64EXPRTYPE_BINARYOP &&
            expr->assignstmt.lvalue->op.optype ==
                H64OP_MEMBERBYIDENTIFIER &&
            expr->assignstmt.lvalue->op.value2->storage.set) {
        *out_storageref = &(
            expr->assignstmt.lvalue->op.value2->storage.ref
        );
    } else {
        assert(expr->assignstmt.lvalue->storage.set);
        *out_storageref = &expr->assignstmt.lvalue->storage.ref;
    }
}

int newcalctemp(h64expression *func, h64expression *expr) {
    // Use temporary 'mandated' by parent if any:
    storageref *parent_store = NULL;
    if (expr && expr->parent &&
            expr->parent->type == H64EXPRTYPE_ASSIGN_STMT) {
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
        if (expr->op.value1->storage._exprstoredintemp >=
                func->funcdef._storageinfo->lowest_guaranteed_free_temp) {
            return expr->op.value1->storage._exprstoredintemp;
        }
        if (expr->type == H64EXPRTYPE_BINARYOP) {
            assert(expr->op.value2 != NULL);
            if (expr->op.value2->storage._exprstoredintemp >=
                    func->funcdef._storageinfo->lowest_guaranteed_free_temp) {
                return expr->op.value2->storage._exprstoredintemp;
            }
        }
    }

    // Get new free temporary:
    assert(func->funcdef._storageinfo != NULL);
    func->funcdef._storageinfo->_temp_calc_slots_used_right_now++;
    if (func->funcdef._storageinfo->_temp_calc_slots_used_right_now >
            func->funcdef._storageinfo->temp_calculation_slots)
        func->funcdef._storageinfo->temp_calculation_slots = (
            func->funcdef._storageinfo->_temp_calc_slots_used_right_now
        );
    return (
        (func->funcdef._storageinfo->_temp_calc_slots_used_right_now - 1) +
        func->funcdef._storageinfo->lowest_guaranteed_free_temp
    );
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
    assert(id >= 0 && id < p->func_count);
    char *instructionsnew = realloc(
        p->func[id].instructions,
        sizeof(*p->func[id].instructions) *
        (p->func[id].instructions_bytes + len)
    );
    if (!instructionsnew) {
        return 0;
    }
    p->func[id].instructions = instructionsnew;
    memcpy(
        p->func[id].instructions + p->func[id].instructions_bytes,
        ptr, len
    );
    p->func[id].instructions_bytes += len;
    return 1;
}

void codegen_CalculateFinalFuncStack(
        h64program *program, h64expression *expr) {
    if (expr->type != H64EXPRTYPE_FUNCDEF_STMT)
        return;
    // Determine final amount of temporaries/stack slots used:
    h64funcsymbol *fsymbol = h64debugsymbols_GetFuncSymbolById(
        program->symbols, expr->funcdef.bytecode_func_id
    );
    expr->funcdef._storageinfo->lowest_guaranteed_free_temp +=
        expr->funcdef._storageinfo->temp_calculation_slots;
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

int _codegencallback_DoCodegen_visit_out(
        h64expression *expr, h64expression *parent, void *ud
        ) {
    asttransforminfo *rinfo = (asttransforminfo *)ud;
    codegen_CalculateFinalFuncStack(rinfo->pr->program, expr);

    h64expression *func = surroundingfunc(expr);
    if (!func) {
        h64expression *sclass = surroundingclass(expr, 0);
        if (sclass != NULL) {
            return 1;
        }
        func = _fakeglobalinitfunc(rinfo);
        if (!func) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }
    }

    if (expr->type == H64EXPRTYPE_LITERAL) {
        int temp = newcalctemp(func, expr);
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
        } else if (expr->literal.type == H64TK_CONSTANT_STRING) {
            inst.content.type = H64VALTYPE_SHORTSTR;
            assert(expr->literal.str_value != NULL);
            int64_t out_len = 0;
            int abortinvalid = 0;
            int abortoom = 0;
            unicodechar *result = utf8_to_utf32_ex(
                expr->literal.str_value,
                strlen(expr->literal.str_value),
                NULL, NULL, &out_len, 1,
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
            rinfo->hadoutofmemory = 1;
            return 0;
        }
        expr->storage._exprstoredintemp = temp;
    } else if (expr->type == H64EXPRTYPE_BINARYOP && (
            expr->op.optype != H64OP_MEMBERBYIDENTIFIER ||
            !expr->op.value1->storage.set)) {
        int temp = newcalctemp(func, expr);
        h64instruction_binop inst_binop = {0};
        inst_binop.type = H64INST_BINOP;
        inst_binop.optype = expr->op.optype;
        inst_binop.slotto = temp;
        inst_binop.arg1slotfrom = expr->op.value1->storage._exprstoredintemp;
        inst_binop.arg2slotfrom = expr->op.value2->storage._exprstoredintemp;
        if (!appendinst(
                rinfo->pr->program, func, expr,
                &inst_binop, sizeof(inst_binop))) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }
        expr->storage._exprstoredintemp = temp;
    } else if (expr->type == H64EXPRTYPE_CALL) {
        int calledexprstoragetemp = (
            expr->inlinecall.value->storage._exprstoredintemp
        );
        int _argtemp = (
            func->funcdef._storageinfo->_temp_calc_slots_used_right_now
        ) + func->funcdef._storageinfo->lowest_guaranteed_free_temp;
        int preargs_tempceiling = _argtemp;
        int posargcount = 0;
        int expandlastposarg = 0;
        int kwargcount = 0;
        int _reachedkwargs = 0;
        h64instruction_settop inst_settop = {0};
        inst_settop.type = H64INST_SETTOP;
        inst_settop.topto = _argtemp;
        if (!appendinst(
                rinfo->pr->program, func, expr,
                &inst_settop, sizeof(inst_settop))) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }
        int i = 0;
        while (i < expr->inlinecall.arguments.arg_count) {
            if (expr->inlinecall.arguments.arg_name[i])
                _reachedkwargs = 1;
            int ismultiarg = 0;
            if (_reachedkwargs) {
                kwargcount++;
                assert(expr->inlinecall.arguments.arg_name[i] != NULL);
                int64_t kwnameidx = (
                    h64debugsymbols_MemberNameToMemberNameId(
                        rinfo->pr->program->symbols,
                        expr->inlinecall.arguments.arg_name[i],
                        0
                    ));
                if (kwnameidx < 0) {
                    char buf[256];
                    snprintf(buf, sizeof(buf) - 1,
                        "internal error: cannot map kw arg name: %s",
                        expr->inlinecall.arguments.arg_name[i]
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
                h64instruction_setconst inst_setconst = {0};
                inst_setconst.type = H64INST_SETCONST;
                inst_setconst.slot = _argtemp;
                inst_setconst.content.type = H64VALTYPE_INT64;
                inst_setconst.content.int_value = kwnameidx;
                if (!appendinst(
                        rinfo->pr->program, func, expr,
                        &inst_setconst, sizeof(inst_setconst))) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
                _argtemp++;
                h64instruction_valuecopy inst_vc = {0};
                inst_vc.type = H64INST_VALUECOPY;
                inst_vc.slotto = _argtemp;
                inst_vc.slotfrom = (
                    expr->inlinecall.arguments.arg_value[i]->
                        storage._exprstoredintemp);
                _argtemp++;
                if (!appendinst(
                        rinfo->pr->program, func, expr,
                        &inst_vc, sizeof(inst_vc))) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
            } else if (i + 1 < expr->inlinecall.arguments.arg_count &&
                    expr->inlinecall.arguments.arg_name[i]) {
                ismultiarg = 1;
            }
            if (!_reachedkwargs) {
                posargcount++;
                h64instruction_valuecopy inst_vc = {0};
                inst_vc.type = H64INST_VALUECOPY;
                inst_vc.slotto = _argtemp;
                inst_vc.slotfrom = (
                    expr->inlinecall.arguments.arg_value[i]->
                        storage._exprstoredintemp);
                _argtemp++;
                if (!appendinst(
                        rinfo->pr->program, func, expr,
                        &inst_vc, sizeof(inst_vc))) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
                if (expr->inlinecall.arguments.last_posarg_is_multiarg) {
                    expandlastposarg = 1;
                }
            }
            i++;
        }
        int maxslotsused = (_argtemp - preargs_tempceiling);
        if (maxslotsused > func->funcdef._storageinfo->
                temp_calculation_slots)
            func->funcdef._storageinfo->temp_calculation_slots = (
                maxslotsused
            );
        h64instruction_call inst_call = {0};
        inst_call.type = H64INST_CALL;
        inst_call.returnto = preargs_tempceiling;
        int temp = newcalctemp(func, expr);
        inst_call.returnto = temp;
        inst_call.slotcalledfrom = calledexprstoragetemp;
        inst_call.expandlastposarg = expandlastposarg;
        inst_call.posargs = posargcount;
        inst_call.kwargs = kwargcount;
        if (!appendinst(
                rinfo->pr->program, func, expr,
                &inst_call, sizeof(inst_call))) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }
        expr->storage._exprstoredintemp = temp;
    } else if (expr->type == H64EXPRTYPE_CLASSDEF_STMT ||
            expr->type == H64EXPRTYPE_CALL_STMT ||
            expr->type == H64EXPRTYPE_IMPORT_STMT) {
        // Nothing to do with those!
    } else if (expr->type == H64EXPRTYPE_IDENTIFIERREF) {
        if (expr->parent != NULL && (
                (expr->parent->type == H64EXPRTYPE_ASSIGN_STMT &&
                 expr->parent->assignstmt.lvalue == expr) ||
                (expr->parent->type == H64EXPRTYPE_BINARYOP &&
                 expr->parent->op.optype == H64OP_MEMBERBYIDENTIFIER &&
                 expr->parent->op.value2 == expr &&
                 !expr->storage.set &&
                 expr->parent->parent->type == H64EXPRTYPE_ASSIGN_STMT &&
                 expr->parent == expr->parent->parent->assignstmt.lvalue)
                )) {
            // This identifier is assigned to, will be handled elsewhere
            return 1;
        } else if (expr->parent != NULL &&
                expr->parent->type == H64EXPRTYPE_BINARYOP &&
                expr->parent->op.optype == H64OP_MEMBERBYIDENTIFIER &&
                expr->parent->op.value2 == expr
                ) {
            // A runtime-resolved get by identifier, handled elsewhere
            return 1;
        }
        if (expr->identifierref.resolved_to_expr &&
                expr->identifierref.resolved_to_expr->type ==
                H64EXPRTYPE_IMPORT_STMT)
            return 1;  // nothing to do with those
        assert(expr->storage.set);
        if (expr->storage.ref.type == H64STORETYPE_STACKSLOT) {
            expr->storage._exprstoredintemp = expr->storage.ref.id;
        } else {
            int temp = newcalctemp(func, expr);
            expr->storage._exprstoredintemp = temp;
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
    } else if (expr->type == H64EXPRTYPE_VARDEF_STMT &&
               expr->vardef.value == NULL) {
        // Empty variable definition, nothing to do
        return 1;
    } else if ((expr->type == H64EXPRTYPE_VARDEF_STMT &&
                expr->vardef.value != NULL) ||
            (expr->type == H64EXPRTYPE_ASSIGN_STMT && (
             expr->assignstmt.lvalue->type ==
                H64EXPRTYPE_IDENTIFIERREF ||
             (expr->assignstmt.lvalue->type ==
                  H64EXPRTYPE_BINARYOP &&
              expr->assignstmt.lvalue->op.optype ==
                  H64OP_MEMBERBYIDENTIFIER &&
              expr->assignstmt.lvalue->op.value2->type ==
                  H64EXPRTYPE_IDENTIFIERREF &&
              expr->assignstmt.lvalue->op.value2->storage.set)))) {
        // Assigning directly to a variable (rather than a member,
        // map value, or the like)
        int assignfromtemporary = -1;
        storageref *str = NULL;
        if (expr->type == H64EXPRTYPE_VARDEF_STMT) {
            assert(expr->storage.set);
            str = &expr->storage.ref;
            assignfromtemporary = expr->vardef.value->
                storage.ref.id;
        } else if (expr->type == H64EXPRTYPE_ASSIGN_STMT) {
            get_assign_lvalue_storage(
                expr, &str
            );
            assignfromtemporary = (
                expr->assignstmt.rvalue->storage._exprstoredintemp
            );
        }
        assert(assignfromtemporary >= 0);
        assert(str->type == H64STORETYPE_GLOBALVARSLOT ||
               str->type == H64STORETYPE_STACKSLOT);
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
        } else if (assignfromtemporary != str->id) {
            assert(str->type == H64STORETYPE_STACKSLOT);
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

    if (IS_STMT(expr->type)) {
        func->funcdef._storageinfo->_temp_calc_slots_used_right_now = 0;
    }

    return 1;
}

int codegen_GenerateBytecodeForFile(
        h64compileproject *project, h64ast *resolved_ast
        ) {
    if (!project || !resolved_ast)
        return 0;

    // Do actual codegen step:
    int transformresult = asttransform_Apply(
        project, resolved_ast,
        NULL, &_codegencallback_DoCodegen_visit_out,
        NULL
    );
    if (!transformresult)
        return 0;

    return 1;
}
