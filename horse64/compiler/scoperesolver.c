// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "bytecode.h"
#include "compiler/ast.h"
#include "compiler/asthelpers.h"
#include "compiler/astobviousmistakes.h"
#include "compiler/astparser.h"
#include "compiler/asttransform.h"
#include "compiler/compileproject.h"
#include "compiler/globallimits.h"
#include "compiler/main.h"
#include "compiler/optimizer.h"
#include "compiler/scoperesolver.h"
#include "compiler/scope.h"
#include "compiler/varstorage.h"
#include "corelib/errors.h"
#include "filesys.h"
#include "hash.h"
#include "nonlocale.h"
#include "threadablechecker.h"


typedef struct resolveinfo {
    int extract_main;
    int main_was_found;
} resolveinfo;


static int isnullvardef(h64expression *expr) {
    if (expr->type != H64EXPRTYPE_VARDEF_STMT)
        return 0;
    return (expr->vardef.value == NULL ||
        (expr->vardef.value->type == H64EXPRTYPE_LITERAL &&
        expr->vardef.value->literal.type == H64TK_CONSTANT_NONE));
}

static int identifierisbuiltin(
        h64program *program,
        const char *identifier,
        storageref *storageref
        ) {
    assert(program->symbols != NULL);
    h64modulesymbols *msymbols = h64debugsymbols_GetBuiltinModule(
        program->symbols
    );
    assert(msymbols != NULL);

    uint64_t number = 0;
    if (hash_StringMapGet(
            msymbols->func_name_to_entry,
            identifier, &number
            )) {
        if (storageref) {
            storageref->type = H64STORETYPE_GLOBALFUNCSLOT;
            storageref->id = number;
        }
        return 1;
    }
    assert(number == 0);
    if (hash_StringMapGet(
            msymbols->class_name_to_entry,
            identifier, &number
            )) {
        if (storageref) {
            storageref->type = H64STORETYPE_GLOBALCLASSSLOT;
            storageref->id = number;
        }
        return 1;
    }
    assert(number == 0);
    if (hash_StringMapGet(
            msymbols->globalvar_name_to_entry,
            identifier, &number
            )) {
        if (storageref) {
            storageref->type = H64STORETYPE_GLOBALVARSLOT;
            storageref->id = number;
        }
        return 1;
    }
    return 0;
}

const char *_shortenedname(
    char *buf, const char *name
);

static int scoperesolver_ComputeItemStorage(
        h64compileproject *project,
        h64expression *expr, h64program *program, h64ast *ast,
        int extract_main,
        int *outofmemory
        ) {
    h64scope *scope = ast_GetScope(expr, &ast->scope);
    assert(program->symbols != NULL);
    assert(scope != NULL);

    // Assign global variables + classes storage:
    if (scope->is_global ||
            expr->type == H64EXPRTYPE_CLASSDEF_STMT) {
        if (expr->storage.set)
            return 1;
        const char *name = NULL;
        if (expr->type == H64EXPRTYPE_VARDEF_STMT) {
            name = expr->vardef.identifier;
            assert(name != NULL);
            int64_t global_id = -1;
            if ((global_id = h64program_AddGlobalvar(
                    program, name,
                    expr->vardef.is_const,
                    ast->fileuri,
                    ast->module_path, ast->library_name
                    )) < 0) {
                if (outofmemory) *outofmemory = 1;
                return 0;
            }
            assert(global_id >= 0);
            expr->storage.set = 1;
            expr->storage.ref.type = H64STORETYPE_GLOBALVARSLOT;
            expr->storage.ref.id = global_id;
            h64globalvarsymbol *gvsymbol = (
                h64debugsymbols_GetGlobalvarSymbolById(
                    program->symbols, global_id
                ));
            assert(gvsymbol != NULL);
            if (expr->vardef.is_const) {
                gvsymbol->is_const = 1;
                gvsymbol->is_simple_const = (
                    expr->vardef.value != NULL ?
                    is_simple_constant_expr(expr->vardef.value) : 1
                );
            }
        } else if (expr->type == H64EXPRTYPE_CLASSDEF_STMT) {
            name = expr->classdef.name;
            int64_t global_id = -1;
            if ((global_id = h64program_AddClass(
                    program, name, ast->fileuri,
                    ast->module_path, ast->library_name
                    )) < 0) {
                if (outofmemory) *outofmemory = 1;
                return 0;
            }
            assert(global_id >= 0);
            expr->storage.set = 1;
            expr->storage.ref.type = H64STORETYPE_GLOBALCLASSSLOT;
            expr->storage.ref.id = global_id;
            expr->classdef.bytecode_class_id = global_id;
            return 1;
        }
    }

    // Handle class attributes:
    if (!scope->is_global && expr->type == H64EXPRTYPE_VARDEF_STMT) {
        h64expression *owningclass = expr->parent;
        while (owningclass) {
            if (owningclass->type == H64EXPRTYPE_CLASSDEF_STMT)
                break;
            if (owningclass->type == H64EXPRTYPE_FUNCDEF_STMT ||
                    owningclass->type == H64EXPRTYPE_INLINEFUNCDEF) {
                owningclass = NULL;
                break;
            }
            owningclass = owningclass->parent;
        }
        if (owningclass) {
            if (!owningclass->storage.set) {
                int inneroom = 0;
                int innerresult = scoperesolver_ComputeItemStorage(
                    project, owningclass, program, ast,
                    extract_main, &inneroom
                );
                if (!innerresult) {
                    if (inneroom && outofmemory) *outofmemory = 1;
                    if (!inneroom && outofmemory) *outofmemory = 0;
                    return 0;
                }
            }
            assert(owningclass->storage.set &&
                   owningclass->storage.ref.type ==
                       H64STORETYPE_GLOBALCLASSSLOT &&
                   owningclass->storage.ref.id >= 0 &&
                   owningclass->storage.ref.id < program->classes_count);
            classid_t owningclassindex = owningclass->storage.ref.id;
            if (program->classes[owningclassindex].varattr_count + 1 >
                    H64LIMIT_MAX_CLASS_VARATTRS) {
                h64classsymbol *csymbol = (
                    h64debugsymbols_GetClassSymbolById(
                        program->symbols, owningclassindex
                    ));
                h64expression *expr = (
                    csymbol ? csymbol->_tmp_classdef_expr_ptr : NULL
                );
                char buf[128] = "";
                snprintf(buf, sizeof(buf) - 1,
                    "exceeded maximum of %" PRId64 " variable "
                    "attributes on this class",
                    (int64_t) H64LIMIT_MAX_CLASS_FUNCATTRS
                );
                if (!result_AddMessage(
                        &ast->resultmsg,
                        H64MSG_ERROR,
                        buf, ast->fileuri,
                        (expr ? expr->line : - 1),
                        (expr ? expr->column : -1)
                        )) {
                    result_AddMessage(
                        &ast->resultmsg,
                        H64MSG_ERROR, "out of memory",
                        ast->fileuri,
                        -1, -1
                    );
                    return 0;
                }
                return 1;
            }
            attridx_t attrindex = -1;
            if ((attrindex = h64program_RegisterClassVariable(
                    program, owningclassindex, expr->vardef.identifier,
                    expr
                    )) < 0) {
                if (outofmemory) *outofmemory = 1;
                return 0;
            }
            assert(!expr->storage.set);
            expr->storage.set = 1;
            expr->storage.ref.type = H64STORETYPE_VARATTRSLOT;
            expr->storage.ref.id = attrindex;
            if (!isnullvardef(expr) && !program->classes[
                    owningclassindex].hasvarinitfunc) {
                int idx = h64program_RegisterHorse64Function(
                    program, "$$varinit",
                    ast->fileuri, 0, NULL, 0,
                    ast->module_path, ast->library_name,
                    owningclassindex
                );
                if (idx < 0) {
                    if (outofmemory) *outofmemory = 1;
                    return 0;
                }
                program->classes[
                    owningclassindex
                ].hasvarinitfunc = 1;
                program->classes[
                    owningclassindex
                ].varinitfuncidx = idx;
                if (program->classes[owningclassindex].user_set_canasync)
                    program->func[idx].user_set_canasync = 1;
                else if (program->classes[owningclassindex].is_threadable == 0)
                    program->func[idx].is_threadable = 0;
            }
            return 1;
        }
    }
    // Add functions to bytecode:
    if (expr->type == H64EXPRTYPE_FUNCDEF_STMT ||
            expr->type == H64EXPRTYPE_INLINEFUNCDEF) {
        // Get the class owning this func, if any:
        h64expression *owningclass = expr->parent;
        while (owningclass) {
            if (owningclass->type == H64EXPRTYPE_CLASSDEF_STMT)
                break;
            if (owningclass->type == H64EXPRTYPE_FUNCDEF_STMT ||
                    owningclass->type == H64EXPRTYPE_INLINEFUNCDEF) {
                owningclass = NULL;
                break;
            }
            owningclass = owningclass->parent;
        }
        if (owningclass && !owningclass->storage.set) {
            int inneroom = 0;
            int innerresult = scoperesolver_ComputeItemStorage(
                project, owningclass, program, ast,
                extract_main, &inneroom
            );
            if (!innerresult) {
                if (inneroom && outofmemory) *outofmemory = 1;
                if (!inneroom && outofmemory) *outofmemory = 0;
                return 0;
            }
        }
        classid_t owningclassindex = -1;
        if (owningclass) {
            assert(owningclass->storage.set &&
                   owningclass->storage.ref.type ==
                       H64STORETYPE_GLOBALCLASSSLOT &&
                   owningclass->storage.ref.id >= 0 &&
                   owningclass->storage.ref.id < program->classes_count);
            owningclassindex = owningclass->storage.ref.id;
        }
        if (owningclassindex >= 0) {
            if (program->classes[owningclassindex].funcattr_count + 1 >
                    H64LIMIT_MAX_CLASS_FUNCATTRS) {
                h64classsymbol *csymbol = (
                    h64debugsymbols_GetClassSymbolById(
                        program->symbols, owningclassindex
                    ));
                h64expression *expr = (
                    csymbol ? csymbol->_tmp_classdef_expr_ptr : NULL
                );
                char buf[128] = "";
                snprintf(buf, sizeof(buf) - 1,
                    "exceeded maximum of %" PRId64 " func "
                    "attributes on this class",
                    (int64_t) H64LIMIT_MAX_CLASS_FUNCATTRS
                );
                if (!result_AddMessage(
                        &ast->resultmsg,
                        H64MSG_ERROR,
                        buf, ast->fileuri,
                        (expr ? expr->line : - 1),
                        (expr ? expr->column : -1)
                        )) {
                    result_AddMessage(
                        &ast->resultmsg,
                        H64MSG_ERROR, "out of memory",
                        ast->fileuri,
                        -1, -1
                    );
                    return 0;
                }
                return 1;
            }
         }

        // Assemble names and parameter info for the function:
        const char *name = expr->funcdef.name;
        assert( expr->funcdef.arguments.arg_count >= 0);
        char **kwarg_names = NULL;
        if (expr->funcdef.arguments.arg_count > 0) {
            kwarg_names = malloc(
                sizeof(*kwarg_names) * expr->funcdef.arguments.arg_count
            );
            if (!kwarg_names) {
                if (outofmemory) *outofmemory = 1;
                return 0;
            }
            memset(kwarg_names, 0, sizeof(*kwarg_names) *
                   expr->funcdef.arguments.arg_count);
        }
        int i = 0;
        while (i < expr->funcdef.arguments.arg_count) {
            if (expr->funcdef.arguments.arg_value[i]) {
                kwarg_names[i] = strdup(
                    expr->funcdef.arguments.arg_name[i]
                );
                if (!kwarg_names[i]) {
                    int k = 0;
                    while (k < i) {
                        free(kwarg_names[k]);
                        k++;
                    }
                    free(kwarg_names);
                    if (outofmemory) *outofmemory = 1;
                    return 0;
                }
            }
            i++;
        }
        assert(name != NULL || expr->type == H64EXPRTYPE_INLINEFUNCDEF);

        // Register actual bytecode program entry for function:
        int64_t bytecode_func_id = -1;
        if ((bytecode_func_id = h64program_RegisterHorse64Function(
                program, name, ast->fileuri,
                expr->funcdef.arguments.arg_count,
                (const char**)kwarg_names,
                expr->funcdef.arguments.last_posarg_is_multiarg,
                ast->module_path,
                ast->library_name,
                owningclassindex
                )) < 0) {
            int k = 0;
            while (k < i) {
                free(kwarg_names[k]);
                k++;
            }
            free(kwarg_names);
            if (outofmemory) *outofmemory = 1;
            return 0;
        }
        if (expr->funcdef.is_canasync) {
            program->func[bytecode_func_id].user_set_canasync = 1;
        } else if (expr->funcdef.is_noasync) {
            program->func[bytecode_func_id].is_threadable = 0;
        }
        int k = 0;
        while (k < i) {
            free(kwarg_names[k]);
            k++;
        }
        free(kwarg_names);
        assert(bytecode_func_id >= 0);
        if (scope->is_global || owningclassindex >= 0) {
            // Not a closure. Set global func storage, and
            // special handling for "main" func.
            assert(expr->funcdef.stmt_count == 0 ||
                   expr->funcdef.stmt != NULL);
            expr->storage.set = 1;
            expr->storage.ref.type = H64STORETYPE_GLOBALFUNCSLOT;
            expr->storage.ref.id = bytecode_func_id;
            if (name != NULL && strcmp(name, "main") == 0 &&
                    extract_main && owningclassindex < 0) {
                if (program->main_func_index >= 0) {
                    char buf[256];
                    snprintf(buf, sizeof(buf) - 1,
                        "unexpected duplicate main func found");
                    if (!result_AddMessage(
                            &ast->resultmsg,
                            H64MSG_ERROR, buf,
                            ast->fileuri,
                            expr->line, expr->column
                            )) {
                        if (outofmemory) *outofmemory = 1;
                        return 0;
                    }
                } else {
                    assert(bytecode_func_id >= 0);
                    program->main_func_index = bytecode_func_id;
                    program->symbols->mainfile_module_path = strdup(
                        ast->module_path
                    );
                    if (!program->symbols->mainfile_module_path) {
                        if (outofmemory) *outofmemory = 1;
                        return 0;
                    }
                    program->symbols->mainfileuri_index = (
                        bytecode_fileuriindex(program, ast->fileuri)
                    );
                    if (program->symbols->mainfileuri_index < 0) {
                        if (outofmemory) *outofmemory = 1;
                        return 0;
                    }
                }
            }
        }
        expr->funcdef.bytecode_func_id = bytecode_func_id;
    }
    return 1;
}

int _resolvercallback_BuildGlobalStorage_visit_out(
        h64expression *expr, ATTR_UNUSED h64expression *parent,
        void *ud
        ) {
    asttransforminfo *atinfo = (asttransforminfo *)ud;
    resolveinfo *rinfo = (resolveinfo*)atinfo->userdata;
    assert(rinfo != NULL);

    // Add keyword argument names as global name indexes:
    if (expr->type == H64EXPRTYPE_CALL) {
        int i = 0;
        while (i < expr->funcdef.arguments.arg_count) {
            if (!expr->funcdef.arguments.arg_name[i]) {
                i++;
                continue;
            }
            int64_t idx = h64debugsymbols_AttributeNameToAttributeNameId(
                atinfo->pr->program->symbols,
                expr->funcdef.arguments.arg_name[i], 1
            );
            if (idx < 0) {
                atinfo->hadoutofmemory = 1;
                return 0;
            }
            i++;
        }
    }

    // Make sure storage is computed for class varattrs:
    if (expr->type == H64EXPRTYPE_VARDEF_STMT) {
        h64expression *owningclass = surroundingclass(expr, 0);
        if (owningclass) {
            int outofmemory = 0;
            if (!scoperesolver_ComputeItemStorage(
                    atinfo->pr, expr, atinfo->pr->program, atinfo->ast,
                    rinfo->extract_main,
                    &outofmemory)) {
                if (outofmemory) {
                    atinfo->hadoutofmemory = 1;
                    return 0;
                }
                char buf[256];
                snprintf(buf, sizeof(buf) - 1,
                    "internal error: failed to compute storage for "
                    "expr: %s", ast_ExpressionTypeToStr(expr->type)
                );
                if (!result_AddMessage(
                        &atinfo->ast->resultmsg,
                        H64MSG_ERROR,
                        buf,
                        atinfo->ast->fileuri,
                        expr->line, expr->column
                        )) {
                    atinfo->hadoutofmemory = 1;
                    return 0;
                }
            }
        }
    }

    // Add file-global items to the project-global item lookups:
    if (expr->type == H64EXPRTYPE_VARDEF_STMT ||
            expr->type == H64EXPRTYPE_CLASSDEF_STMT ||
            expr->type == H64EXPRTYPE_FUNCDEF_STMT ||
            expr->type == H64EXPRTYPE_INLINEFUNCDEF) {
        h64scope *scope = ast_GetScope(expr, &atinfo->ast->scope);
        if (scope == NULL) {
            if (atinfo->ast->resultmsg.success) {
                // No error yet, so this can't be a follow-up issue
                char *s = ast_ExpressionToJSONStr(
                    expr, atinfo->ast->fileuri
                );
                char _bufstack[1024];
                int buflen = 1024;
                char *_bufheap = malloc(strlen(s) + 2048);
                char *buf = _bufstack;
                if (_bufheap) {
                    buflen = (s ? strlen(s) : 512) + 2048;
                    buf = _bufheap;
                }
                snprintf(buf, buflen - 1,
                    "internal error: failed to obtain scope, "
                    "malformed AST? expr: %s/%s, parent: %s",
                    ast_ExpressionTypeToStr(expr->type), (s ? s : ""),
                    (expr->parent ? ast_ExpressionTypeToStr(expr->parent->type) :
                     "none")
                );
                buf[buflen - 1] = '\0';
                free(_bufheap);
                free(s);
                if (!result_AddMessage(
                        &atinfo->ast->resultmsg,
                        H64MSG_ERROR,
                        buf,
                        atinfo->ast->fileuri,
                        expr->line, expr->column
                        )) {
                    atinfo->hadoutofmemory = 1;
                    return 0;
                }
            }
            return 1;
        }
        if ((scope->is_global &&
                expr->storage.set == 0) || (
                (expr->type == H64EXPRTYPE_FUNCDEF_STMT ||
                 expr->type == H64EXPRTYPE_INLINEFUNCDEF) &&
                expr->funcdef.bytecode_func_id < 0)) {
            int outofmemory = 0;
            if (!scoperesolver_ComputeItemStorage(
                    atinfo->pr, expr, atinfo->pr->program, atinfo->ast,
                    rinfo->extract_main,
                    &outofmemory)) {
                if (outofmemory) {
                    atinfo->hadoutofmemory = 1;
                    return 0;
                }
                char buf[256];
                snprintf(buf, sizeof(buf) - 1,
                    "internal error: failed to compute storage for "
                    "expr: %s",
                    ast_ExpressionTypeToStr(expr->type)
                );
                if (!result_AddMessage(
                        &atinfo->ast->resultmsg,
                        H64MSG_ERROR,
                        buf,
                        atinfo->ast->fileuri,
                        expr->line, expr->column
                        )) {
                    atinfo->hadoutofmemory = 1;
                    return 0;
                }
            }
        }
    }
    return 1;
}

int scoperesolver_EvaluateDerivedClassParent(
        asttransforminfo *atinfo, h64expression *expr
        ) {
    if (expr->parent->type != H64EXPRTYPE_CLASSDEF_STMT ||
            (expr->type != H64EXPRTYPE_IDENTIFIERREF &&
             (expr->type != H64EXPRTYPE_BINARYOP ||
              expr->op.optype != H64OP_ATTRIBUTEBYIDENTIFIER)) ||
            expr->parent->classdef.baseclass_ref != expr)
        return 1;

    if (!expr->storage.set ||
            expr->storage.ref.type != H64STORETYPE_GLOBALCLASSSLOT) {
        // We can't set this as a base class.
        if ((expr->storage.set &&
                expr->storage.ref.type != H64STORETYPE_GLOBALCLASSSLOT) ||
                (atinfo->ast->resultmsg.success &&
                 atinfo->pr->resultmsg->success)) {
            // Likely a user error and not just a follow-up problem
            char buf[128] = "";
            snprintf(buf, sizeof(buf) - 1,
                "unexpected derived from expression, "
                "must refer to another class"
            );
            if (!result_AddMessage(
                    &atinfo->ast->resultmsg,
                    H64MSG_ERROR,
                    buf,
                    atinfo->ast->fileuri,
                    expr->line, expr->column
                    )) {
                atinfo->hadoutofmemory = 1;
                return 0;
            }
        }
        return 1;  // bail out
    }
    if (expr->parent->classdef.bytecode_class_id < 0) {
        // There was an error that prevents us from completing this,
        // bail out:
        assert(!atinfo->ast->resultmsg.success ||
               !atinfo->pr->resultmsg->success);
        return 1;
    }
    assert(expr->storage.ref.id >= 0 &&
           expr->storage.ref.id < atinfo->pr->program->classes_count);
    { // Cycles should have been detected earlier, ensure this:
        int64_t cid = expr->storage.ref.id;
        while (cid >= 0) {
            assert(cid != expr->parent->classdef.bytecode_class_id);
            cid = atinfo->pr->program->classes[
                cid
            ].base_class_global_id;
        }
    }

    // Set base class id:
    assert(atinfo->pr->program->classes[
        expr->parent->classdef.bytecode_class_id
    ].base_class_global_id < 0);
    atinfo->pr->program->classes[
        expr->parent->classdef.bytecode_class_id
    ].base_class_global_id = expr->storage.ref.id;

    // Mark chain as errors if deriving from 'Exception':
    int error_chain = 0;
    {
        int64_t cid = expr->parent->classdef.bytecode_class_id;
        while (cid >= 0) {
            if (cid == H64STDERROR_ERROR) {
                error_chain = 1;
                break;
            }
            cid = atinfo->pr->program->classes[
                cid
            ].base_class_global_id;
        }
    }
    if (error_chain) {
        int64_t cid = expr->parent->classdef.bytecode_class_id;
        while (cid >= 0) {
            cid = atinfo->pr->program->classes[
                cid
            ].is_error = 1;
            cid = atinfo->pr->program->classes[
                cid
            ].base_class_global_id;
        }
    }
    return 1;
}

int _resolvercallback_ResolveIdentifiers_visit_out(
        h64expression *expr, h64expression *parent, void *ud
        ) {
    asttransforminfo *atinfo = (asttransforminfo *)ud;

    // Resolve most inner identifiers:
    if (expr->type == H64EXPRTYPE_IDENTIFIERREF &&
            (parent == NULL ||
             parent->type != H64EXPRTYPE_BINARYOP ||
             parent->op.value1 == expr ||
             parent->op.optype != H64OP_ATTRIBUTEBYIDENTIFIER)) {
        // This actually refers to an item itself, rather than just
        // being the name of an attribute obtained at runtime -> resolve
        assert(expr->identifierref.value != NULL);
        h64scope *scope = ast_GetScope(expr, &atinfo->ast->scope);
        if (scope == NULL) {
            if (atinfo->ast->resultmsg.success) {
                // No error yet, so this can't be a follow-up issue
                char *s = ast_ExpressionToJSONStr(
                    expr, atinfo->ast->fileuri
                );
                char _bufstack[1024];
                int buflen = 1024;
                char *_bufheap = malloc(strlen(s) + 2048);
                char *buf = _bufstack;
                if (_bufheap) {
                    buflen = strlen(s) + 2048;
                    buf = _bufheap;
                }
                snprintf(buf, buflen - 1,
                    "internal error: failed to obtain scope, "
                    "malformed AST? expr: %s/%s, parent: %s",
                    ast_ExpressionTypeToStr(expr->type), s,
                    (expr->parent ? ast_ExpressionTypeToStr(expr->parent->type) :
                     "none")
                );
                buf[buflen - 1] = '\0';
                free(_bufheap);
                free(s);
                if (!result_AddMessage(
                        &atinfo->ast->resultmsg,
                        H64MSG_ERROR,
                        buf,
                        atinfo->ast->fileuri,
                        expr->line, expr->column
                        )) {
                    atinfo->hadoutofmemory = 1;
                    return 0;
                }
            }
            return 1;
        }
        if (strcmp(expr->identifierref.value, "self") == 0 ||
                strcmp(expr->identifierref.value, "base") == 0) {
            h64expression *owningclass = surroundingclass(
                expr, 1
            );
            if (!owningclass) {
                char buf[256];
                snprintf(buf, sizeof(buf) - 1,
                    "unexpected identifier \"%s\", "
                    "not inside a class func",
                    expr->identifierref.value
                );
                if (!result_AddMessage(
                        &atinfo->ast->resultmsg,
                        H64MSG_ERROR,
                        buf,
                        atinfo->ast->fileuri,
                        expr->line, expr->column
                        )) {
                    atinfo->hadoutofmemory = 1;
                    return 0;
                }
                return 1;
            }
            h64expression *func = surroundingfunc(expr);
            int directly_in_class_func = (
                surroundingclass(func, 0) != NULL
            );
            if (directly_in_class_func && !expr->storage.set) {
                expr->storage.set = 1;
                expr->storage.ref.type = H64STORETYPE_STACKSLOT;
                expr->storage.ref.id = 0;
            } else if (!directly_in_class_func) {
                // This has to be a closure (nested inside other func).
                // Any 'self' usage must have led to closure var storage:
                assert(func != NULL);
                #ifndef NDEBUG
                if (func->funcdef._storageinfo == NULL) {
                    char *s = ast_ExpressionToJSONStr(func, NULL);
                    fprintf(stderr, "horsec: error: internal error: "
                            "invalid missing storage info on "
                            "closure: %s\n", s);
                    free(s);
                }
                #endif
                assert(func->funcdef._storageinfo != NULL);
                func->funcdef._storageinfo->closure_with_self = 1;
            }
            return 1;
        }
        h64scopedef *def = scope_QueryItem(
            scope, expr->identifierref.value,
            SCOPEQUERY_FLAG_BUBBLEUP
        );
        if (!def) {
            if (identifierisbuiltin(
                    atinfo->pr->program, expr->identifierref.value,
                    &expr->storage.ref)) {
                expr->identifierref.resolved_to_builtin = 1;
                assert(expr->storage.ref.type != 0);
                expr->storage.set = 1;
            } else {
                char buf[256]; char describebuf[64];
                snprintf(buf, sizeof(buf) - 1,
                    "unknown identifier \"%s\", variable "
                    "or module not found",
                    _shortenedname(describebuf, expr->identifierref.value)
                );
                if (!result_AddMessage(
                        &atinfo->ast->resultmsg,
                        H64MSG_ERROR,
                        buf,
                        atinfo->ast->fileuri,
                        expr->line, expr->column
                        )) {
                    atinfo->hadoutofmemory = 1;
                    return 0;
                }
                return 1;
            }
            return 1;
        }

        expr->identifierref.resolved_to_def = def;
        expr->identifierref.resolved_to_expr = def->declarationexpr;
        // Check if it's a file-local thing referenced in a way we know:
        // (we know of file-local funcs by name, surrounding funcs'
        // parameters, file-local variables and classes, and
        // surrounding for loops' iterators)
        if (def->declarationexpr->type ==
                H64EXPRTYPE_VARDEF_STMT ||
                def->declarationexpr->type ==
                H64EXPRTYPE_FOR_STMT ||
                (def->declarationexpr->type ==
                 H64EXPRTYPE_FUNCDEF_STMT &&
                 strcmp(def->declarationexpr->funcdef.name,
                        expr->identifierref.value) == 0) ||
                def->declarationexpr->type ==
                H64EXPRTYPE_CLASSDEF_STMT ||
                ((def->declarationexpr->type ==
                  H64EXPRTYPE_FUNCDEF_STMT ||
                  def->declarationexpr->type ==
                  H64EXPRTYPE_INLINEFUNCDEF) &&
                func_has_param_with_name(def->declarationexpr,
                    expr->identifierref.value
                ))) {  // A known file-local thing
            if (!isexprchildof(expr, def->declarationexpr) ||
                    def->declarationexpr->type ==
                    H64EXPRTYPE_FOR_STMT) {
                // Not our direct parent, so we're an external use.
                // -> let's mark it as used from somewhere external:
                def->everused = 1;
                // If the referenced thing is a variable inside a func
                // and we're closure, then mark as used in closure:
                h64expression *localvarfunc = surroundingfunc(
                    def->declarationexpr
                );
                if (isinsideclosure(expr) &&
                        def->declarationexpr->type ==
                        H64EXPRTYPE_VARDEF_STMT &&
                        localvarfunc != NULL) {
                    def->closurebound = 1;  // mark as closure-used
                    h64expression *closure = surroundingfunc(expr);
                    assert(closure != NULL && closure != localvarfunc);
                    // All nested closures up to the scope of to the
                    // variable definition need to bind it:
                    while (closure && closure != localvarfunc) {
                        assert(
                            closure->type == H64EXPRTYPE_FUNCDEF_STMT ||
                            closure->type == H64EXPRTYPE_INLINEFUNCDEF
                        );
                        h64funcstorageextrainfo *einfo = (
                            closure->funcdef._storageinfo
                        );
                        if (!einfo) {
                            einfo = malloc(sizeof(*einfo));
                            if (!einfo) {
                                atinfo->hadoutofmemory = 1;
                                return 0;
                            }
                            memset(einfo, 0, sizeof(*einfo));
                            closure->funcdef._storageinfo = einfo;
                        }

                        h64scopedef **newboundvars = realloc(
                            einfo->closureboundvars,
                            sizeof(*newboundvars) *
                            (einfo->closureboundvars_count + 1)
                        );
                        if (!newboundvars) {
                            atinfo->hadoutofmemory = 1;
                            return 0;
                        }
                        einfo->closureboundvars = newboundvars;
                        einfo->closureboundvars[
                            einfo->closureboundvars_count
                        ] = def;
                        einfo->closureboundvars_count++;
                        closure = surroundingfunc(closure);
                    }
                    assert(closure == localvarfunc);
                }
            }

            if (def->declarationexpr->storage.set) {
                memcpy(
                    &expr->storage, &def->declarationexpr->storage,
                    sizeof(expr->storage)
                );
            } else if (def->scope->is_global) {
                // Global storage should have been determined already,
                // so this shouldn't happen. Log as error:
                atinfo->hadunexpectederror = 1;
            }
            if (!scoperesolver_EvaluateDerivedClassParent(
                    atinfo, expr
                    )) {
                atinfo->hadoutofmemory = 1;
                return 0;
            }
        } else if (def->declarationexpr->type ==
                H64EXPRTYPE_IMPORT_STMT) {
            // Not a file-local, but instead an imported thing.
            // Figure out what we're referencing and from what module:

            if (expr->parent == NULL ||
                    expr->parent->type != H64EXPRTYPE_BINARYOP ||
                    expr->parent->op.optype !=
                        H64OP_ATTRIBUTEBYIDENTIFIER) {
                // A module can only be used as <module>.<item>,
                // and never in any other way.
                char buf[256];
                h64snprintf(buf, sizeof(buf) - 1,
                    "unexpected import reference not used "
                    "as attribute by identifier base, this is invalid"
                );
                if (!result_AddMessage(
                        &atinfo->ast->resultmsg,
                        H64MSG_ERROR,
                        buf,
                        atinfo->ast->fileuri,
                        expr->line, expr->column
                        )) {
                    atinfo->hadoutofmemory = 1;
                    return 0;
                }
                return 1;
            }

            // First, follow the tree to the full import path with dots:
            int accessed_elements_count = 1;
            int accessed_elements_alloc = H64LIMIT_IMPORTCHAINLEN + 1;
            char **accessed_elements_str = alloca(
                sizeof(*accessed_elements_str) * (
                accessed_elements_alloc)
            );
            h64expression **accessed_elements_expr = alloca(
                sizeof(*accessed_elements_expr) * (
                accessed_elements_alloc)
            );
            accessed_elements_expr[0] = expr;
            accessed_elements_str[0] = expr->identifierref.value;
            h64expression *pexpr = expr;
            // Find outer-most attribute by identifier:
            while (pexpr->parent &&
                    pexpr->parent->type == H64EXPRTYPE_BINARYOP &&
                    pexpr->parent->op.optype ==
                        H64OP_ATTRIBUTEBYIDENTIFIER &&
                    pexpr->parent->op.value1 == pexpr &&
                    pexpr->parent->op.value2 != NULL &&
                    pexpr->parent->op.value2->type ==
                        H64EXPRTYPE_IDENTIFIERREF &&
                    pexpr->parent->parent &&
                    pexpr->parent->parent->op.optype ==
                        H64OP_ATTRIBUTEBYIDENTIFIER &&
                    pexpr->parent->parent->op.value1 == pexpr->parent &&
                    pexpr->parent->parent->op.value2 != NULL &&
                    pexpr->parent->parent->op.value2->type ==
                        H64EXPRTYPE_IDENTIFIERREF) {
                pexpr = pexpr->parent;
                accessed_elements_str[accessed_elements_count] =
                    pexpr->op.value2->identifierref.value;
                accessed_elements_expr[accessed_elements_count] =
                    pexpr->op.value2;
                accessed_elements_count++;
                if (accessed_elements_count >= accessed_elements_alloc) {
                    char buf[256];
                    h64snprintf(buf, sizeof(buf) - 1,
                        "unexpected import chain "
                        "exceeding maximum nesting of %d",
                        (int)H64LIMIT_IMPORTCHAINLEN
                    );
                    if (!result_AddMessage(
                            &atinfo->ast->resultmsg,
                            H64MSG_ERROR,
                            buf,
                            atinfo->ast->fileuri,
                            expr->line, expr->column
                            )) {
                        atinfo->hadoutofmemory = 1;
                        return 0;
                    }
                    return 1;
                }
            }

            // See what exact import statement that maps to:
            h64expression *_mapto = NULL;
            int j = -1;
            while (j < def->additionaldecl_count) {
                h64expression *val = def->declarationexpr;
                if (j >= 0)
                    val = def->additionaldecl[j];
                if (val->type == H64EXPRTYPE_IMPORT_STMT &&
                        val->importstmt.import_elements_count ==
                        accessed_elements_count) {
                    int mismatch = 0;
                    int k = 0;
                    while (k < accessed_elements_count) {
                        if (strcmp(
                                accessed_elements_str[k],
                                val->importstmt.import_elements[k]
                                ) != 0) {
                            mismatch = 1;
                            break;
                        }
                        k++;
                    }
                    if (!mismatch) _mapto = val;
                }
                j++;
            }

            // Put together path for error messages later:
            size_t full_imp_path_len = 0;
            char *full_imp_path = NULL;
            {
                full_imp_path_len = 0;
                int k = 0;
                while (k < accessed_elements_count) {
                    if (k > 0)
                        full_imp_path_len++;
                    full_imp_path_len += strlen(accessed_elements_str[k]);
                    k++;
                }
                full_imp_path = malloc(full_imp_path_len + 1);
                if (!full_imp_path) {
                    atinfo->hadoutofmemory = 1;
                    return 0;
                }
                full_imp_path[0] = '\0';
                k = 0;
                while (k < accessed_elements_count) {
                    if (k > 0) {
                        full_imp_path[strlen(full_imp_path) + 1] = '\0';
                        full_imp_path[strlen(full_imp_path) + 1] = '.';
                    }
                    memcpy(
                        full_imp_path + strlen(full_imp_path),
                        accessed_elements_str[k],
                        strlen(accessed_elements_str[k]) + 1
                    );
                    k++;
                }
            }

            // Abort if we found no exact match for import:
            if (_mapto == NULL) {
                char buf[256];
                h64snprintf(buf, sizeof(buf) - 1,
                    "unknown reference to module path \"%s\", "
                    "not found among this file's imports",
                    full_imp_path
                );
                free(full_imp_path);
                full_imp_path = NULL;
                if (!result_AddMessage(
                        &atinfo->ast->resultmsg,
                        H64MSG_ERROR,
                        buf,
                        atinfo->ast->fileuri,
                        expr->line, expr->column
                        )) {
                    atinfo->hadoutofmemory = 1;
                    return 0;
                }
                return 1;
            }
            expr->identifierref.resolved_to_expr = _mapto;

            // Resolve the attribute access that is done on top of this
            // module path (or error if there is none which is invalid,
            // modules cannot be referenced in any other way):
            assert(_mapto != NULL && pexpr != NULL);
            if (pexpr->parent == NULL ||
                    pexpr->parent->type != H64EXPRTYPE_BINARYOP ||
                    pexpr->parent->op.optype !=
                        H64OP_ATTRIBUTEBYIDENTIFIER ||
                    pexpr->parent->op.value1 != pexpr ||
                    pexpr->parent->op.value2 == NULL ||
                    pexpr->parent->op.value2->type !=
                        H64EXPRTYPE_IDENTIFIERREF) {
                char buf[256];
                h64snprintf(buf, sizeof(buf) - 1,
                    "unexpected %s of module %s, "
                    "instead of accessing any element from "
                    "the module via \".\"",
                    (pexpr->parent == NULL ? "standalone use" : (
                     pexpr->parent->type != H64EXPRTYPE_BINARYOP ?
                     ast_ExpressionTypeToStr(pexpr->parent->type) :
                     operator_OpTypeToStr(pexpr->parent->op.optype))),
                    full_imp_path
                );
                free(full_imp_path);
                full_imp_path = NULL;
                if (!result_AddMessage(
                        &atinfo->ast->resultmsg,
                        H64MSG_ERROR,
                        buf, atinfo->ast->fileuri,
                        expr->line, expr->column
                        )) {
                    atinfo->hadoutofmemory = 1;
                    return 0;
                }
                return 1;
            }
            const char *refitemname = (
                pexpr->parent->op.value2->identifierref.value
            );
            if (!refitemname) {
                // Oops, the accessed attribute on the module is NULL.
                // Likely an error in a previous stage (e.g. parser).
                // Mark as error in any case, to make sure we return
                // an error message always:
                atinfo->hadunexpectederror = 1;
                free(full_imp_path);
                full_imp_path = NULL;
                return 1;
            }
            // Get the actual module item by name:
            assert(_mapto->type == H64EXPRTYPE_IMPORT_STMT &&
                ((_mapto->importstmt.referenced_ast != NULL &&
                  _mapto->importstmt.referenced_ast->scope.
                        name_to_declaration_map != NULL) ||
                 _mapto->importstmt.references_c_module));
            if (_mapto->importstmt.references_c_module) {
                // There is no AST to check, since this is a
                // C module.
                // Instead, we need to find this from the registered
                // C functions/classes/vars via the debug symbols:
                h64modulesymbols *msymbols = (
                    h64debugsymbols_GetModule(
                        atinfo->pr->program->symbols, full_imp_path,
                        _mapto->importstmt.source_library, 0
                    )
                );
                assert(msymbols != NULL);
                uint64_t number = 0;
                if (hash_StringMapGet(
                        msymbols->func_name_to_entry,
                        refitemname, &number)) {
                    expr->storage.set = 1;
                    expr->storage.ref.type =
                        H64STORETYPE_GLOBALFUNCSLOT;
                    expr->storage.ref.id = (
                        msymbols->func_symbols[number].global_id
                    );
                } else if (hash_StringMapGet(
                        msymbols->class_name_to_entry,
                        refitemname, &number)) {
                    expr->storage.set = 1;
                    expr->storage.ref.type =
                        H64STORETYPE_GLOBALCLASSSLOT;
                    expr->storage.ref.id = (
                        msymbols->classes_symbols[number].global_id
                    );
                } else if (hash_StringMapGet(
                        msymbols->globalvar_name_to_entry,
                        refitemname, &number)) {
                    expr->storage.set = 1;
                    expr->storage.ref.type =
                        H64STORETYPE_GLOBALVARSLOT;
                    expr->storage.ref.id = (
                        msymbols->globalvar_symbols[number].global_id
                    );
                } else {
                    char buf[256];
                    snprintf(buf, sizeof(buf) - 1,
                        "unknown identifier \"%s\" "
                        "not found in module \"%s\"",
                        refitemname,
                        full_imp_path
                    );
                    free(full_imp_path);
                    full_imp_path = NULL;
                    if (!result_AddMessage(
                            &atinfo->ast->resultmsg,
                            H64MSG_ERROR,
                            buf,
                            atinfo->ast->fileuri,
                            expr->line, expr->column
                            )) {
                        atinfo->hadoutofmemory = 1;
                        return 0;
                    }
                    return 1;
                }
                free(full_imp_path);
                full_imp_path = NULL;
            } else {
                // Access global scope of imported module:
                uint64_t number = 0;
                if (!hash_StringMapGet(
                        _mapto->importstmt.referenced_ast->scope.
                        name_to_declaration_map,
                        refitemname, &number) || number == 0) {
                    char buf[256];
                    snprintf(buf, sizeof(buf) - 1,
                        "unknown identifier \"%s\" "
                        "not found in module \"%s\"",
                        refitemname,
                        full_imp_path
                    );
                    free(full_imp_path);
                    full_imp_path = NULL;
                    if (!result_AddMessage(
                            &atinfo->ast->resultmsg,
                            H64MSG_ERROR,
                            buf,
                            atinfo->ast->fileuri,
                            expr->line, expr->column
                            )) {
                        atinfo->hadoutofmemory = 1;
                        return 0;
                    }
                    return 1;
                }

                // We found the item. Mark as used & copy storage:
                h64scopedef *targetitem = (
                    (h64scopedef*)(uintptr_t)number
                );
                assert(targetitem != NULL &&
                    targetitem->declarationexpr != NULL);
                if (targetitem->declarationexpr->storage.set) {
                    memcpy(
                        &expr->storage.ref,
                        &targetitem->declarationexpr->storage.ref,
                        sizeof(expr->storage.ref)
                    );
                    expr->storage.set = 1;
                } else {
                    assert(!expr->storage.set);
                    assert(!atinfo->ast->resultmsg.success ||
                           !atinfo->pr->resultmsg->success);
                }
                free(full_imp_path);
                full_imp_path = NULL;
                targetitem->everused = 1;  // item from target AST was used
                def->everused = 1;  // import statement was used
            }

            // Set storage on all import path items:
            if (pexpr != expr && expr->storage.set) {
                memcpy(
                    &pexpr->storage.ref,
                    &expr->storage.ref,
                    sizeof(expr->storage.ref)
                );
                pexpr->storage.set = 1;
            }
            int k = 0;
            while (k < accessed_elements_count) {
                if (accessed_elements_expr[k] == expr) {
                    k++;
                    continue;
                }
                if (expr->storage.set) {
                    assert(!accessed_elements_expr[k]->storage.set);
                    memcpy(
                        &accessed_elements_expr[k]->storage.ref,
                        &expr->storage.ref,
                        sizeof(expr->storage.ref)
                    );
                    accessed_elements_expr[k]->storage.set = 1;
                }
                k++;
            }
            if (expr->storage.set) {  // always true if we had no error
                assert(!pexpr->parent->op.value2->
                        storage.set);
                memcpy(
                    &pexpr->parent->op.value2->storage.ref,
                    &expr->storage.ref,
                    sizeof(expr->storage.ref)
                );
                pexpr->parent->op.value2->storage.set = 1;
                memcpy(
                    &pexpr->parent->storage.ref,
                    &expr->storage.ref,
                    sizeof(expr->storage.ref)
                );
                pexpr->parent->storage.set = 1;
            }

            // Evaluate the identifiers pointing to base classes,
            // if this is any:
            if (!scoperesolver_EvaluateDerivedClassParent(
                    atinfo, pexpr->parent->op.value2
                    )) {
                atinfo->hadoutofmemory = 1;
                return 0;
            }
        } else {
            char buf[256];
            h64snprintf(buf, sizeof(buf) - 1,
                "internal error: identifier ref '%s' points "
                "to unhandled expr type %d at line %" PRId64 ", "
                "column %" PRId64,
                expr->identifierref.value,
                (int)def->declarationexpr->type,
                def->declarationexpr->line,
                def->declarationexpr->column
            );
            if (!result_AddMessage(
                    &atinfo->ast->resultmsg,
                    H64MSG_ERROR,
                    buf,
                    atinfo->ast->fileuri,
                    expr->line, expr->column
                    )) {
                atinfo->hadoutofmemory = 1;
                return 0;
            }
            return 1;
        }
    }

    // Resolve attribute by identifier names to ids:
    if (expr->type == H64EXPRTYPE_IDENTIFIERREF &&
            parent != NULL &&
            parent->type == H64EXPRTYPE_BINARYOP &&
            parent->op.value2 == expr &&
            parent->op.optype == H64OP_ATTRIBUTEBYIDENTIFIER &&
            !expr->storage.set) {
        int64_t idx = h64debugsymbols_AttributeNameToAttributeNameId(
            atinfo->pr->program->symbols,
            expr->identifierref.value, 1
        );
        if (idx < 0) {
            atinfo->hadoutofmemory = 1;
            return 0;
        }
    }

    return 1;
}

int scoperesolver_BuildASTGlobalStorage(
        h64compileproject *pr, h64misccompileroptions *miscoptions,
        h64ast *unresolved_ast, int recursive,
        resolveinfo *rinfo
        ) {
    if (unresolved_ast->global_storage_built)
        return 1;

    if (miscoptions->compiler_stage_debug) {
        fprintf(
            stderr, "horsec: debug: scoperesolver_BuildASTGlobalStorage "
                "start on %s (pr->resultmsg.success: %d)\n",
            unresolved_ast->fileuri, pr->resultmsg->success
        );
    }

    // Mark done even if we fail:
    unresolved_ast->global_storage_built = 1;

    // Set module path if missing:
    assert(unresolved_ast->fileuri != NULL);
    if (!unresolved_ast->module_path) {
        char *library_source = NULL;
        int pathoom = 0;
        char *project_path = compileproject_GetFileSubProjectPath(
            pr, unresolved_ast->fileuri, &library_source, &pathoom
        );
        if (!project_path) {
            assert(library_source == NULL);
            if (!pathoom) {
                char buf[2048];
                snprintf(buf, sizeof(buf) - 1,
                    "unexpected failure to locate file's project base: "
                    "%s - with overall project folder: %s",
                    unresolved_ast->fileuri,
                    pr->basefolder);
                if (!result_AddMessage(
                        &unresolved_ast->resultmsg,
                        H64MSG_ERROR, buf,
                        unresolved_ast->fileuri,
                        -1, -1
                    ))
                    return 0;  // Out of memory
                return 1;  // regular abort with failure
            }
            return 0;
        }
        int modpathoom = 0;
        char *module_path = compileproject_URIRelPath(
            project_path, unresolved_ast->fileuri, &modpathoom
        );
        if (!module_path) {
            free(library_source);
            if (!modpathoom) {
                char buf[2048];
                snprintf(buf, sizeof(buf) - 1,
                    "failed to locate this file path inside project: "
                    "%s (file project base: %s, overall project base: %s)",
                    unresolved_ast->fileuri, project_path,
                    pr->basefolder);
                free(project_path);
                project_path = NULL;
                if (!result_AddMessage(
                        &unresolved_ast->resultmsg,
                        H64MSG_ERROR, buf,
                        unresolved_ast->fileuri,
                        -1, -1
                        ))
                    return 0;
                return 1;
            }
            free(project_path);
            project_path = NULL;
            return 0;
        }
        free(project_path);
        project_path = NULL;

        // Strip away file extension and normalize:
        if (strlen(module_path) > strlen(".h64") && (
                memcmp(module_path + strlen(module_path) -
                       strlen(".h64"), ".h64", strlen(".h64")) == 0 ||
                memcmp(module_path + strlen(module_path) -
                       strlen(".H64"), ".H64", strlen(".H64")) == 0)) {
            module_path[strlen(module_path) - strlen(".h64")] = '\0';
        } else {
            free(library_source);
            char buf[256];
            h64snprintf(buf, sizeof(buf) - 1,
                "cannot import code from file not ending in "
                ".h64: %s", unresolved_ast->fileuri
            );
            free(module_path);
            if (!result_AddMessage(
                    &unresolved_ast->resultmsg,
                    H64MSG_ERROR, buf,
                    unresolved_ast->fileuri,
                    -1, -1
                ))
                return 0;  // Out of memory
            return 1;  // regular abort with failure
        }
        char *new_module_path = filesys_Normalize(module_path);
        free(module_path);
        if (!new_module_path) {
            free(library_source);
            return 0;
        }
        module_path = new_module_path;

        // If path has dots in later elements, then abort with error:
        unsigned int i = 0;
        while (i < strlen(module_path)) {
            if (module_path[i] == '.') {
                free(library_source);
                char buf[256];
                h64snprintf(buf, sizeof(buf) - 1,
                    "cannot import code from file with dots in name: "
                    "\"%s\"", module_path);
                free(module_path);
                if (!result_AddMessage(
                        &unresolved_ast->resultmsg,
                        H64MSG_ERROR, buf,
                        unresolved_ast->fileuri,
                        -1, -1
                    ))
                    return 0;  // Out of memory
                return 1;  // regular abort with failure
            }
            i++;
        }

        // Replace file separators with dots:
        i = 0;
        while (i < strlen(module_path)) {
            if (module_path[i] == '/'
                    #if defined(_WIN32) || defined(_WIN64)
                    || module_path[i] == '\\'
                    #endif
                    ) {
                module_path[i] = '.';
            }
            i++;
        }

        unresolved_ast->module_path = strdup(module_path);
        unresolved_ast->library_name = library_source;
        free(module_path);
        if (!unresolved_ast->module_path) {
            free(unresolved_ast->library_name);
            unresolved_ast->library_name = NULL;
            return 0;
        }
    }

    // First, make sure all imports are loaded up:
    int i = 0;
    while (i < unresolved_ast->scope.definitionref_count) {
        assert(unresolved_ast->scope.definitionref[i] != NULL);
        h64expression *expr = (
            unresolved_ast->scope.definitionref[i]->declarationexpr
        );
        if (expr->type != H64EXPRTYPE_IMPORT_STMT ||
                (expr->importstmt.referenced_ast != NULL &&
                 strlen(expr->importstmt.referenced_ast->fileuri) > 0) ||
                expr->importstmt.references_c_module) {
            i++;
            continue;
        }
        int oom = 0;
        int iscmodule = compileproject_DoesImportMapToCFuncs(
            pr, (const char **)expr->importstmt.import_elements,
            expr->importstmt.import_elements_count,
            expr->importstmt.source_library,
            miscoptions->import_debug, &oom
        );
        if (iscmodule) {
            expr->importstmt.references_c_module = 1;
            i++;
            continue;
        }
        if (!iscmodule && oom) {
            result_AddMessage(
                &unresolved_ast->resultmsg, H64MSG_ERROR,
                "import failed, out of memory or other fatal "
                "internal error",
                unresolved_ast->fileuri,
                expr->line, expr->column
            );
            return 0;
        }
        oom = 0;
        char *file_path = compileproject_ResolveImportToFile(
            pr, unresolved_ast->fileuri,
            (const char **)expr->importstmt.import_elements,
            expr->importstmt.import_elements_count,
            expr->importstmt.source_library,
            miscoptions->import_debug, &oom
        );
        if (!file_path) {
            char buf[256];
            if (oom) {
                result_AddMessage(
                    &unresolved_ast->resultmsg, H64MSG_ERROR,
                    "import failed, out of memory or other fatal "
                    "internal error",
                    unresolved_ast->fileuri,
                    expr->line, expr->column
                );
                return 0;
            } else {
                char modpath[128] = "";
                int i2 = 0;
                while (i2 < expr->importstmt.import_elements_count) {
                    if (i2 > 0 && strlen(modpath) < sizeof(modpath) - 1)
                        strncat(modpath, ".",
                                sizeof(modpath) - strlen(modpath) - 1);
                    if (strlen(modpath) < sizeof(modpath) - 1)
                        strncat(modpath, expr->importstmt.import_elements[i2],
                                sizeof(modpath) - strlen(modpath) - 1);
                    i2++;
                }
                if (strlen(modpath) >= sizeof(modpath) - 1) {
                    modpath[sizeof(modpath) - 4] = '.';
                    modpath[sizeof(modpath) - 3] = '.';
                    modpath[sizeof(modpath) - 2] = '.';
                    modpath[sizeof(modpath) - 1] = '\0';
                }
                h64snprintf(buf, sizeof(buf) - 1,
                    "couldn't resolve import, module \"%s\" not found",
                    modpath
                );
                if (!result_AddMessage(
                        &unresolved_ast->resultmsg,
                        H64MSG_ERROR, buf,
                        unresolved_ast->fileuri,
                        expr->line, expr->column
                        )) {
                    result_AddMessage(
                        &unresolved_ast->resultmsg,
                        H64MSG_ERROR, "out of memory",
                        unresolved_ast->fileuri,
                        expr->line, expr->column
                    );
                    return 0;
                }
                i++;
                continue;
            }
        }
        assert(expr->importstmt.referenced_ast == NULL);
        char *error = NULL;
        if (!compileproject_GetAST(
                pr, file_path,
                &expr->importstmt.referenced_ast, &error
                )) {
            expr->importstmt.referenced_ast = NULL;
            char buf[256];
            snprintf(buf, sizeof(buf) - 1,
                "unexpected failure to process import: %s",
                error);
            free(error);
            if (!result_AddMessage(
                    &unresolved_ast->resultmsg,
                    H64MSG_ERROR, buf,
                    unresolved_ast->fileuri,
                    expr->line, expr->column
                    )) {
                free(file_path);
                result_AddMessage(
                    &unresolved_ast->resultmsg,
                    H64MSG_ERROR, "out of memory",
                    unresolved_ast->fileuri,
                    expr->line, expr->column
                );
                return 0;
            }
        }
        free(file_path);
        assert(expr->importstmt.referenced_ast != NULL ||
            !unresolved_ast->resultmsg.success);
        i++;
    }

    // Build global storage:
    int buildstorageresult = asttransform_Apply(
        pr, unresolved_ast, NULL,
        &_resolvercallback_BuildGlobalStorage_visit_out,
        rinfo
    );
    if (!buildstorageresult)
        return 0;

    // Do recursive handling if asked for:
    if (recursive) {
        // First, make sure all imports are loaded up:
        int i = 0;
        while (i < unresolved_ast->scope.definitionref_count) {
            assert(unresolved_ast->scope.definitionref[i] != NULL);
            h64expression *expr = (
                unresolved_ast->scope.definitionref[i]->declarationexpr
            );
            if (expr->type != H64EXPRTYPE_IMPORT_STMT) {
                i++;
                continue;
            }
            if (expr->importstmt.referenced_ast != NULL) {
                resolveinfo rinfo2;
                memcpy(&rinfo2, rinfo, sizeof(*rinfo));
                rinfo2.extract_main = 0;
                if (!scoperesolver_BuildASTGlobalStorage(
                        pr, miscoptions,
                        expr->importstmt.referenced_ast, 0, &rinfo2
                        )) {
                    // Try to transfer messages anyway, but
                    // ignore failure:
                    result_TransferMessages(
                        &expr->importstmt.referenced_ast->resultmsg,
                        pr->resultmsg
                    );
                    return 0;
                }
                if (!result_TransferMessages(
                        &expr->importstmt.referenced_ast->resultmsg,
                        pr->resultmsg)) {
                    return 0;
                }
            }
            i++;
        }
    }

    if (miscoptions->compiler_stage_debug) {
        fprintf(
            stderr, "horsec: debug: scoperesolver_BuildASTGlobalStorage "
                "completed on %s (pr->resultmsg.success: %d)\n",
            unresolved_ast->fileuri, pr->resultmsg->success
        );
    }

    return 1;
}

int scoperesolver_ResolveAST(
        h64compileproject *pr, h64misccompileroptions *miscoptions,
        h64ast *unresolved_ast, int extract_program_main
        ) {
    // Abort if we're obviously already done:
    assert(unresolved_ast != NULL);
    if (unresolved_ast->identifiers_resolved)
        return 1;
    if (!pr->resultmsg->success || !unresolved_ast->resultmsg.success) {
        pr->resultmsg->success = 0;
        unresolved_ast->resultmsg.success = 0;
        return 1;
    }

    unresolved_ast->identifiers_resolved = 1;  // mark done even if failing
    assert(
        pr->program->main_func_index < 0 || !extract_program_main
    );
    resolveinfo rinfo;
    memset(&rinfo, 0, sizeof(rinfo));
    rinfo.extract_main = (extract_program_main != 0);

    // Make sure global storage was assigned on this AST and all
    // referenced ones:
    if (!scoperesolver_BuildASTGlobalStorage(
            pr, miscoptions, unresolved_ast, 1, &rinfo
            )) {
        pr->resultmsg->success = 0;
        unresolved_ast->resultmsg.success = 0;
        return 0;
    } else {
        // Copy any generated errors or warnings:
        if (!result_TransferMessages(
                &unresolved_ast->resultmsg, pr->resultmsg
                )) {
            result_AddMessage(
                &unresolved_ast->resultmsg,
                H64MSG_ERROR, "out of memory",
                unresolved_ast->fileuri,
                -1, -1
            );
            return 0;
        }
    }

    // Abort if we ran into an error at this point:
    if (!pr->resultmsg->success || !unresolved_ast->resultmsg.success) {
        pr->resultmsg->success = 0;
        unresolved_ast->resultmsg.success = 0;
        return 1;
    }

    // Add error message if we looked for "main" and didn't find it:
    if (extract_program_main && pr->program->main_func_index < 0) {
        pr->resultmsg->success = 0;
        unresolved_ast->resultmsg.success = 0;
        char buf[256];
        snprintf(buf, sizeof(buf) - 1,
            "unexpected lack of \"main\" func, expected "
            "to find it as a program starting point in this file");
        if (!result_AddMessage(
                &unresolved_ast->resultmsg,
                H64MSG_ERROR, buf,
                unresolved_ast->fileuri,
                -1, -1
                )) {
            result_AddMessage(
                &unresolved_ast->resultmsg,
                H64MSG_ERROR, "out of memory",
                unresolved_ast->fileuri,
                -1, -1
            );
            return 0;
        }
    }

    // Propagate class attributes from base to derived classes:
    pr->_class_was_propagated = malloc(
        (int64_t)sizeof(*pr->_class_was_propagated) *
        pr->program->classes_count
    );
    if (!pr->_class_was_propagated) {
        result_AddMessage(
            &unresolved_ast->resultmsg,
            H64MSG_ERROR, "out of memory",
            unresolved_ast->fileuri,
            -1, -1
        );
        return 0;
    }
    memset(
        pr->_class_was_propagated, 0,
        (int64_t)sizeof(*pr->_class_was_propagated) *
        pr->program->classes_count
    );
    classid_t i = 0;
    while (i < pr->program->classes_count) {
        classid_t k = i;
        while (pr->program->classes[k].base_class_global_id >= 0 &&
                !pr->_class_was_propagated[
                    pr->program->classes[k].base_class_global_id
                ]) {
            k = pr->program->classes[k].base_class_global_id;
            if (k >= 0 && k == i) {
                h64classsymbol *csymbol = (
                    h64debugsymbols_GetClassSymbolById(
                        pr->program->symbols, k
                    ));
                h64expression *expr = (
                    csymbol ? csymbol->_tmp_classdef_expr_ptr : NULL
                );
                char buf[128] = "";
                snprintf(buf, sizeof(buf) - 1,
                    "unexpected cycle in base classes, "
                    "a class must not derive from itself"
                );
                if (!result_AddMessage(
                        &unresolved_ast->resultmsg,
                        H64MSG_ERROR,
                        buf,
                        unresolved_ast->fileuri,
                        (expr ? expr->line : -1),
                        (expr ? expr->column : -1)
                        )) {
                    result_AddMessage(
                        &unresolved_ast->resultmsg,
                        H64MSG_ERROR, "out of memory",
                        unresolved_ast->fileuri,
                        -1, -1
                    );
                    return 0;
                }
                return 1;
            }
        }
        if (pr->program->classes[k].base_class_global_id < 0 ||
                pr->_class_was_propagated[k]) {
            pr->_class_was_propagated[k] = 1;
            if (k == i)
                i++;
            continue;
        }
        h64classsymbol *csymbol = (
            h64debugsymbols_GetClassSymbolById(
                pr->program->symbols, k
            ));
        assert(csymbol != NULL);
        classid_t k_parent = (
            pr->program->classes[k].base_class_global_id
        );
        h64classsymbol *csymbol_parent = (
            h64debugsymbols_GetClassSymbolById(
                pr->program->symbols, k_parent
            ));
        assert(csymbol_parent != NULL);
        // Make sure we aren't overriding parent varattrs since that is not
        // allowed:
        attridx_t i2 = 0;
        while (i2 < pr->program->classes[k].varattr_count &&
                k_parent >= 0) {
            int64_t nameidx = pr->program->classes[k].
                varattr_global_name_idx[i2];
            attridx_t foundidx = h64program_LookupClassAttribute(
                pr->program, k_parent, nameidx
            );
            if (foundidx >= 0) {
                h64expression *expr = (
                    csymbol && csymbol->_tmp_varattr_expr_ptr ?
                    csymbol->_tmp_varattr_expr_ptr[i2] : NULL
                );
                char buf[128] = "";
                char namebuf[64] = "";
                snprintf(buf, sizeof(buf) - 1,
                    "blocked name \"%s\", "
                    "variable attributes must not be overriding base class "
                    "attributes",
                    ((expr && expr->type == H64EXPRTYPE_VARDEF_STMT) ?
                     _shortenedname(namebuf, expr->vardef.identifier) :
                     NULL)
                );
                if (!result_AddMessage(
                        &unresolved_ast->resultmsg,
                        H64MSG_ERROR,
                        buf,
                        unresolved_ast->fileuri,
                        (expr ? expr->line : - 1),
                        (expr ? expr->column : -1)
                        )) {
                    result_AddMessage(
                        &unresolved_ast->resultmsg,
                        H64MSG_ERROR, "out of memory",
                        unresolved_ast->fileuri,
                        -1, -1
                    );
                    return 0;
                }
                return 1;
            }
            i2++;
        }
        // Pull in varattrs from parent class:
        attridx_t newvarattr_count = (
            pr->program->classes[k_parent].varattr_count +
            pr->program->classes[k].varattr_count
        );
        if (newvarattr_count >
                pr->program->classes[k].varattr_count) {
            if (newvarattr_count > H64LIMIT_MAX_CLASS_VARATTRS) {
                h64expression *expr = (
                    csymbol ? csymbol->_tmp_classdef_expr_ptr : NULL
                );
                char buf[128] = "";
                snprintf(buf, sizeof(buf) - 1,
                    "exceeded maximum of %" PRId64 " variable "
                    "attributes on this class",
                    (int64_t) H64LIMIT_MAX_CLASS_FUNCATTRS
                );
                if (!result_AddMessage(
                        &unresolved_ast->resultmsg,
                        H64MSG_ERROR,
                        buf,
                        unresolved_ast->fileuri,
                        (expr ? expr->line : - 1),
                        (expr ? expr->column : -1)
                        )) {
                    result_AddMessage(
                        &unresolved_ast->resultmsg,
                        H64MSG_ERROR, "out of memory",
                        unresolved_ast->fileuri,
                        -1, -1
                    );
                    return 0;
                }
                return 1;
            }
            int64_t *new_varattr_global_name_idx = realloc(
                pr->program->classes[k].varattr_global_name_idx,
                sizeof(*new_varattr_global_name_idx) *
                newvarattr_count
            );
            if (!new_varattr_global_name_idx) {
                result_AddMessage(
                    &unresolved_ast->resultmsg,
                    H64MSG_ERROR, "out of memory",
                    unresolved_ast->fileuri,
                    -1, -1
                );
                return 0;
            }
            void **new_tmp_varattr_expr_ptr = realloc(
                csymbol->_tmp_varattr_expr_ptr,
                sizeof(*new_tmp_varattr_expr_ptr) * newvarattr_count
            );
            if (!new_tmp_varattr_expr_ptr) {
                result_AddMessage(
                    &unresolved_ast->resultmsg,
                    H64MSG_ERROR, "out of memory",
                    unresolved_ast->fileuri,
                    -1, -1
                );
                return 0;
            }
            pr->program->classes[k].varattr_global_name_idx = (
                new_varattr_global_name_idx
            );
            attridx_t oldvarattr_count = (
                pr->program->classes[k].varattr_count
            );
            attridx_t z = pr->program->classes[k].varattr_count;
            while (z < newvarattr_count) {
                pr->program->classes[z].
                    varattr_global_name_idx[z] = (
                        pr->program->classes[k_parent].
                        varattr_global_name_idx[z - oldvarattr_count]
                    );
                csymbol->_tmp_varattr_expr_ptr[z] = (
                    csymbol_parent->_tmp_varattr_expr_ptr[
                        z - oldvarattr_count
                    ]);
                z++;
            }
        }
        // Pull in funcattrs from parent class (only non-overridden ones):
        i2 = 0;
        while (i2 < pr->program->classes[k_parent].funcattr_count) {
            attridx_t foundidx = h64program_LookupClassAttribute(
                pr->program, k, pr->program->classes[k_parent].
                    funcattr_global_name_idx[i2]
            );
            if (foundidx >= 0)
                break;
            attridx_t newfuncattr_count = (
                pr->program->classes[k].funcattr_count + 1
            );
            if (newfuncattr_count > H64LIMIT_MAX_CLASS_FUNCATTRS) {
                h64expression *expr = (
                    csymbol ? csymbol->_tmp_classdef_expr_ptr : NULL
                );
                char buf[128] = "";
                snprintf(buf, sizeof(buf) - 1,
                    "exceeded maximum of %" PRId64 " func "
                    "attributes on this class",
                    (int64_t) H64LIMIT_MAX_CLASS_FUNCATTRS
                );
                if (!result_AddMessage(
                        &unresolved_ast->resultmsg,
                        H64MSG_ERROR,
                        buf,
                        unresolved_ast->fileuri,
                        (expr ? expr->line : - 1),
                        (expr ? expr->column : -1)
                        )) {
                    result_AddMessage(
                        &unresolved_ast->resultmsg,
                        H64MSG_ERROR, "out of memory",
                        unresolved_ast->fileuri,
                        -1, -1
                    );
                    return 0;
                }
                return 1;
            }
            int64_t *new_funcattr_global_name_idx = realloc(
                pr->program->classes[k].funcattr_global_name_idx,
                sizeof(*new_funcattr_global_name_idx) *
                newfuncattr_count
            );
            if (!new_funcattr_global_name_idx) {
                result_AddMessage(
                    &unresolved_ast->resultmsg,
                    H64MSG_ERROR, "out of memory",
                    unresolved_ast->fileuri,
                    -1, -1
                );
                return 0;
            }
            pr->program->classes[k].funcattr_global_name_idx =
                new_funcattr_global_name_idx;
            funcid_t *new_funcattr_func_idx = realloc(
                pr->program->classes[k].funcattr_func_idx,
                sizeof(*new_funcattr_func_idx) *
                newfuncattr_count
            );
            if (!new_funcattr_func_idx) {
                result_AddMessage(
                    &unresolved_ast->resultmsg,
                    H64MSG_ERROR, "out of memory",
                    unresolved_ast->fileuri,
                    -1, -1
                );
                return 0;
            }
            pr->program->classes[k].funcattr_func_idx =
                new_funcattr_func_idx;
            pr->program->classes[k].funcattr_global_name_idx[
                newfuncattr_count - 1
                ] = pr->program->classes[k_parent].
                    funcattr_global_name_idx[i2];
            pr->program->classes[k].funcattr_func_idx[
                newfuncattr_count - 1
                ] = pr->program->classes[k_parent].
                    funcattr_func_idx[i2];
            i2++;
        }
        // Regenerate hash map:
        if (!h64program_RebuildClassAttributeHashmap(
                pr->program, k)) {
            result_AddMessage(
                &unresolved_ast->resultmsg,
                H64MSG_ERROR, "out of memory",
                unresolved_ast->fileuri,
                -1, -1
            );
            return 0;
        }
        // Advance:
        if (k == i)
            i++;
    }

    // Resolve identifiers:
    int transformresult = asttransform_Apply(
        pr, unresolved_ast, NULL,
        &_resolvercallback_ResolveIdentifiers_visit_out,
        &rinfo
    );
    if (!transformresult)
        return 0;

    // If so far we didn't have an error, do local storage:
    if (pr->resultmsg->success &&
            unresolved_ast->resultmsg.success) {
        if (!varstorage_AssignLocalStorage(
                pr, unresolved_ast
                ))
            return 0;
        if (!astobviousmistakes_CheckAST(pr, unresolved_ast))
            return 0;
    }

    // If no error even now, register all functions with threadable
    // checker:
    if (pr->resultmsg->success &&
            unresolved_ast->resultmsg.success) {
        if (!threadablechecker_RegisterASTForCheck(
                pr, unresolved_ast
                ))
            return 0;
    }

    // Finally, make sure we copy all errors/warnngs/...:
    if (!result_TransferMessages(
            &unresolved_ast->resultmsg, pr->resultmsg
            )) {
        result_AddMessage(
            &unresolved_ast->resultmsg,
            H64MSG_ERROR, "out of memory",
            unresolved_ast->fileuri,
            -1, -1
        );
        return 0;
    }
    return 1;
}
