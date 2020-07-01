#include "compileconfig.h"

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

int newmultilinetemp(h64expression *func, ATTR_UNUSED h64expression *expr) {
    assert(func->funcdef._storageinfo->codegen.oneline_temps_used_now == 0);
    int i = 0;
    while (i < func->funcdef._storageinfo->codegen.perm_temps_count) {
        if (!func->funcdef._storageinfo->codegen.perm_temps_used[i]) {
            func->funcdef._storageinfo->codegen.perm_temps_used[i] = 1;
            return func->funcdef._storageinfo->
                lowest_guaranteed_free_temp + i;
        }
        i++;
    }
    int *new_used = realloc(
        func->funcdef._storageinfo->codegen.perm_temps_used,
        sizeof(*func->funcdef._storageinfo->codegen.perm_temps_used) * (
        func->funcdef._storageinfo->codegen.perm_temps_count + 1)
    );
    if (!new_used)
        return -1;
    func->funcdef._storageinfo->codegen.perm_temps_used = new_used;
    func->funcdef._storageinfo->codegen.perm_temps_used[
        func->funcdef._storageinfo->codegen.perm_temps_count
    ] = 1;
    func->funcdef._storageinfo->codegen.perm_temps_count++;
    return func->funcdef._storageinfo->lowest_guaranteed_free_temp + (
        func->funcdef._storageinfo->codegen.perm_temps_count - 1
    );
}

void freemultilinetemp(
        h64expression *func, int temp
        ) {
    temp -= func->funcdef._storageinfo->lowest_guaranteed_free_temp;
    assert(temp >= 0 && temp <
           func->funcdef._storageinfo->codegen.perm_temps_count);
    assert(func->funcdef._storageinfo->codegen.perm_temps_used[temp]);
    func->funcdef._storageinfo->codegen.perm_temps_used[temp] = 0;
}

int new1linetemp(h64expression *func, h64expression *expr) {
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
                func->funcdef._storageinfo->lowest_guaranteed_free_temp) {
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

    // Get new free temporary:
    assert(func->funcdef._storageinfo != NULL);
    func->funcdef._storageinfo->codegen.oneline_temps_used_now++;
    if (func->funcdef._storageinfo->codegen.oneline_temps_used_now >
            func->funcdef._storageinfo->codegen.max_oneline_slots)
        func->funcdef._storageinfo->codegen.max_oneline_slots = (
            func->funcdef._storageinfo->codegen.oneline_temps_used_now
        );
    return (
        (func->funcdef._storageinfo->codegen.oneline_temps_used_now - 1) +
        func->funcdef._storageinfo->lowest_guaranteed_free_temp
    );
}

int appendinstbyfuncid(
        h64program *p,
        int id, h64expression *correspondingexpr,
        void *ptr, size_t len
        ) {
    assert(id >= 0 && id < p->func_count);
    assert(!p->func[id].iscfunc);
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
    if (expr->type != H64EXPRTYPE_FUNCDEF_STMT)
        return;
    // Determine final amount of temporaries/stack slots used:
    h64funcsymbol *fsymbol = h64debugsymbols_GetFuncSymbolById(
        program->symbols, expr->funcdef.bytecode_func_id
    );
    expr->funcdef._storageinfo->lowest_guaranteed_free_temp +=
        expr->funcdef._storageinfo->codegen.max_oneline_slots +
        expr->funcdef._storageinfo->codegen.perm_temps_count;
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

struct _jumpinfo {
    int jumpid;
    int64_t offset;
};

int codegen_FinalBytecodeTransform(
        h64compileproject *prj, h64program *pr
        ) {
    int jump_table_alloc = 0;
    int jump_table_fill = 0;
    int *jump_id = NULL;
    struct _jumpinfo *jump_info = NULL;

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
            default:
                k += (int64_t)h64program_PtrToInstructionSize((char*)inst);
                continue;
            }
            assert(jumpid >= 0);

            // FIXME: use a faster algorithm here, maybe hash table?
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
                free(jump_info);
                return 0;
            }
            jumptargetoffset -= k;
            if (jumptargetoffset == 0) {
                prj->resultmsg->success = 0;
                char buf[256];
                snprintf(buf, sizeof(buf) - 1, "internal error: "
                    "found jump instruction in func with global id=%d "
                    "that has invalid zero relative offset - "
                    "codegen bug?",
                    (int)i
                );
                if (!result_AddMessage(
                        prj->resultmsg,
                        H64MSG_ERROR, buf,
                        NULL, -1, -1
                        )) {
                    return 0;
                }
                return 1;
            }
            if (jumptargetoffset > 65535 || jumptargetoffset < -65535) {
                prj->resultmsg->success = 0;
                char buf[256];
                snprintf(buf, sizeof(buf) - 1,
                    "found jump instruction in func with global id=%d "
                    "that exceeds 16bit int range, this is not spuported",
                    (int)i
                );
                if (!result_AddMessage(
                        prj->resultmsg,
                        H64MSG_ERROR, buf,
                        NULL, -1, -1
                        )) {
                    return 0;
                }
                return 1;
            }

            switch (inst->type) {
            case H64INST_CONDJUMP: {
                h64instruction_condjump *cjump = (
                    (h64instruction_condjump *)inst
                );
                cjump->jumpbytesoffset = jumptargetoffset;
                break;
            }
            case H64INST_JUMP: {
                h64instruction_jump *jump = (
                    (h64instruction_jump *)inst
                );
                jump->jumpbytesoffset = jumptargetoffset;
                break;
            }
            default:
                fprintf(stderr, "horsec: error: internal error in "
                    "codegen jump translation: unhandled jump type\n");
                free(jump_info);
                return 0;
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
            return 1;
        }
        func = _fakeglobalinitfunc(rinfo);
        if (!func) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }
    }

    if (expr->type == H64EXPRTYPE_LITERAL) {
        int temp = new1linetemp(func, expr);
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
    } else if (expr->type == H64EXPRTYPE_WHILE_STMT) {
        // Already handled in visit_in
    } else if (expr->type == H64EXPRTYPE_BINARYOP && (
            expr->op.optype != H64OP_MEMBERBYIDENTIFIER ||
            !expr->op.value1->storage.set)) {
        int temp = new1linetemp(func, expr);
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
    } else if (expr->type == H64EXPRTYPE_CALL) {
        int calledexprstoragetemp = (
            expr->inlinecall.value->storage.eval_temp_id
        );
        int _argtemp = (
            func->funcdef._storageinfo->codegen.oneline_temps_used_now
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
                        storage.eval_temp_id);
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
                        storage.eval_temp_id);
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
                codegen.max_oneline_slots)
            func->funcdef._storageinfo->codegen.max_oneline_slots = (
                maxslotsused
            );
        h64instruction_call inst_call = {0};
        inst_call.type = H64INST_CALL;
        inst_call.returnto = preargs_tempceiling;
        int temp = new1linetemp(func, expr);
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
        expr->storage.eval_temp_id = temp;
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
            expr->storage.eval_temp_id = expr->storage.ref.id;
        } else {
            int temp = new1linetemp(func, expr);
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
    } else if (expr->type == H64EXPRTYPE_VARDEF_STMT &&
               expr->vardef.value == NULL) {
        // Empty variable definition, nothing to do
        return 1;
    } else if (expr->type == H64EXPRTYPE_RETURN_STMT) {
        int returntemp = -1;
        if (expr->returnstmt.returned_expression) {
            returntemp = expr->storage.eval_temp_id;
        } else {
            returntemp = new1linetemp(func, expr);
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
                expr->assignstmt.rvalue->storage.eval_temp_id
            );
            if (expr->assignstmt.assignop != H64OP_ASSIGN) {
                int oldvaluetemp = -1;
                if (str->type == H64STORETYPE_GLOBALVARSLOT) {
                    oldvaluetemp = new1linetemp(func, expr);
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
                } else {
                    assert(str->type == H64STORETYPE_STACKSLOT);
                    oldvaluetemp = str->id;
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
    } else if (expr->type == H64EXPRTYPE_FUNCDEF_STMT) {
        // Handled on _visit_in
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
        func->funcdef._storageinfo->codegen.oneline_temps_used_now = 0;
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

    if (IS_STMT(expr->type)) {
        func->funcdef._storageinfo->codegen.oneline_temps_used_now = 0;
    }

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
                    func, expr
                );

                h64instruction_setconst inst_sconst = {0};
                inst_sconst.type = H64INST_SETCONST;
                inst_sconst.content.type = H64VALTYPE_UNSPECIFIED_KWARG;
                inst_sconst.slot = operand2tmp;

                h64instruction_binop inst_binop = {0};
                inst_binop.type = H64INST_BINOP;
                inst_binop.optype = H64OP_CMP_NOTEQUAL;
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

                func->funcdef._storageinfo->codegen.
                    oneline_temps_used_now = 0;

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

                func->funcdef._storageinfo->codegen.
                    oneline_temps_used_now = 0;
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
        func->funcdef._storageinfo->codegen.
            oneline_temps_used_now = 0;

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
            i++;
        }

        rinfo->dont_descend_visitation = 1;
        return 1;
    } else if (expr->type == H64EXPRTYPE_TRY_STMT) {
        rinfo->dont_descend_visitation = 1;

        int32_t jumpid_catch = -1;
        int32_t jumpid_finally = -1;
        int32_t jumpid_end = (
            func->funcdef._storageinfo->jump_targets_used
        );
        func->funcdef._storageinfo->jump_targets_used++;

        h64instruction_pushcatchframe inst_pushframe = {0};
        inst_pushframe.type = H64INST_PUSHCATCHFRAME;
        inst_pushframe.slotexceptionto = -1;
        inst_pushframe.jumponcatch = -1;
        inst_pushframe.jumponfinally = -1;
        if (expr->trystmt.exceptions_count > 0) {
            assert(expr->storage.set);
            assert(expr->storage.ref.type ==
                   H64STORETYPE_STACKSLOT);
            inst_pushframe.jumponcatch = expr->storage.ref.id;
            inst_pushframe.mode |= CATCHMODE_JUMPONCATCH;
            jumpid_catch = (
                func->funcdef._storageinfo->jump_targets_used
            );
            func->funcdef._storageinfo->jump_targets_used++;
            inst_pushframe.jumponcatch = jumpid_catch;
        }
        if (expr->trystmt.has_finally_block) {
            inst_pushframe.mode |= CATCHMODE_JUMPONFINALLY;
            jumpid_finally = (
                func->funcdef._storageinfo->jump_targets_used
            );
            func->funcdef._storageinfo->jump_targets_used++;
            inst_pushframe.jumponfinally = jumpid_finally;
        }

        int exception_reuse_tmp = -1;
        int i = 0;
        while (i < expr->trystmt.exceptions_count) {
            assert(expr->trystmt.exceptions[i]->storage.set);
            int exception_tmp = -1;
            if (expr->trystmt.exceptions[i]->storage.ref.type ==
                    H64STORETYPE_STACKSLOT) {
                exception_tmp = (int)(
                    expr->trystmt.exceptions[i]->storage.ref.id
                );
            } else if (expr->trystmt.exceptions[i]->storage.ref.type ==
                       H64STORETYPE_GLOBALCLASSSLOT) {
                h64instruction_addcatchtype addctype = {0};
                addctype.type = H64INST_ADDCATCHTYPE;
                addctype.classid = (
                    expr->trystmt.exceptions[i]->
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
                assert(expr->trystmt.exceptions[i]->storage.ref.type ==
                       H64STORETYPE_GLOBALVARSLOT);
                if (exception_reuse_tmp < 0) {
                    exception_reuse_tmp = new1linetemp(
                        func, expr
                    );
                }
                exception_tmp = exception_reuse_tmp;
                h64instruction_getglobal inst_getglobal = {0};
                inst_getglobal.type = H64INST_GETGLOBAL;
                inst_getglobal.slotto = exception_tmp;
                inst_getglobal.globalfrom = expr->trystmt.exceptions[i]->
                    storage.ref.id;
                if (!appendinst(
                        rinfo->pr->program, func, expr,
                        &inst_getglobal, sizeof(inst_getglobal))) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
            }
            assert(exception_tmp >= 0);
            h64instruction_addcatchtypebyref addctyperef = {0};
            addctyperef.type = H64INST_ADDCATCHTYPEBYREF;
            addctyperef.slotfrom = exception_tmp;
            if (!appendinst(
                    rinfo->pr->program, func, expr,
                    &addctyperef, sizeof(addctyperef))) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
            i++;
        }

        i = 0;
        while (i < expr->trystmt.trystmt_count) {
            rinfo->dont_descend_visitation = 0;
            int result = ast_VisitExpression(
                expr->trystmt.trystmt[i], expr,
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
        h64instruction_popcatchframe inst_popcatch = {0};
        inst_popcatch.type = H64INST_POPCATCHFRAME;
        if (!appendinst(
                rinfo->pr->program, func, expr,
                &inst_popcatch, sizeof(inst_popcatch))) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }
        h64instruction_jump inst_jump = {0};
        inst_jump.type = H64INST_JUMP;
        inst_jump.jumpbytesoffset = jumpid_end;
        if (!appendinst(
                rinfo->pr->program, func, expr,
                &inst_jump, sizeof(inst_jump))) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }

        if ((inst_pushframe.mode | CATCHMODE_JUMPONCATCH) != 0) {
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
            while (i < expr->trystmt.catchstmt_count) {
                rinfo->dont_descend_visitation = 0;
                int result = ast_VisitExpression(
                    expr->trystmt.catchstmt[i], expr,
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
        }

        if ((inst_pushframe.mode | CATCHMODE_JUMPONFINALLY) != 0) {
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
            while (i < expr->trystmt.finallystmt_count) {
                rinfo->dont_descend_visitation = 0;
                int result = ast_VisitExpression(
                    expr->trystmt.finallystmt[i], expr,
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

        int itertemp = newmultilinetemp(func, expr);
        if (itertemp < 0) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }

        int containertemp = -1;
        int freecontainertemp = 0;
        assert(expr->forstmt.iterated_container->storage.set);
        if (expr->forstmt.iterated_container->storage.ref.type ==
                H64STORETYPE_STACKSLOT) {
            containertemp = expr->forstmt.
                iterated_container->storage.ref.id;
        } else {
            assert(
                expr->forstmt.iterated_container->storage.ref.type ==
                H64STORETYPE_GLOBALVARSLOT
            );
            freecontainertemp = 1;
            containertemp = newmultilinetemp(func, expr);
            if (containertemp < 0) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
            h64instruction_getglobal inst_getglobal = {0};
            inst_getglobal.type = H64INST_GETGLOBAL;
            inst_getglobal.slotto = containertemp;
            inst_getglobal.globalfrom = expr->forstmt.
                iterated_container->storage.ref.id;
            if (!appendinst(
                    rinfo->pr->program, func, expr,
                    &inst_getglobal, sizeof(inst_getglobal))) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
        }

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
        if (freecontainertemp)
            freemultilinetemp(func, containertemp);
        rinfo->dont_descend_visitation = 1;
        return 1;
    } else if (expr->type == H64EXPRTYPE_IF_STMT) {
        rinfo->dont_descend_visitation = 1;
        int32_t jumpid_end = (
            func->funcdef._storageinfo->jump_targets_used
        );
        func->funcdef._storageinfo->jump_targets_used++;

        struct h64ifstmt *current_clause = &func->ifstmt;
        assert(current_clause->conditional != NULL);
        while (current_clause != NULL) {
            int32_t jumpid_nextclause = -1;
            if (current_clause->followup_clause) {
                jumpid_nextclause = (
                    func->funcdef._storageinfo->jump_targets_used
                );
                func->funcdef._storageinfo->jump_targets_used++;
            }

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

            int i = 0;
            while (i < current_clause->stmt_count) {
                rinfo->dont_descend_visitation = 0;
                result = ast_VisitExpression(
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
            if (current_clause->followup_clause != NULL) {
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

int codegen_GenerateBytecodeForFile(
        h64compileproject *project, h64ast *resolved_ast
        ) {
    if (!project || !resolved_ast)
        return 0;

    // Do actual codegen step:
    int transformresult = asttransform_Apply(
        project, resolved_ast,
        &_codegencallback_DoCodegen_visit_in,
        &_codegencallback_DoCodegen_visit_out,
        NULL
    );
    if (!transformresult)
        return 0;

    // Transform jump instructions to final offsets:
    if (!codegen_FinalBytecodeTransform(
            project, project->program
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

    return 1;
}
