// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "compiler/ast.h"
#include "compiler/asthelpers.h"
#include "compiler/astparser.h"
#include "compiler/asttransform.h"
#include "compiler/compileproject.h"
#include "compiler/varstorage.h"


static int nosideffectsvalue(h64expression *expr) {
    if (expr->type == H64EXPRTYPE_IDENTIFIERREF) {
        return 1;
    } else if (expr->type == H64EXPRTYPE_LITERAL) {
        return 1;
    }
    return 0;
}

static int nosideeffectsdef(h64expression *expr) {
    if (expr->type == H64EXPRTYPE_VARDEF_STMT) {
        if (expr->vardef.value == NULL)
            return 1;
        if (nosideffectsvalue(expr->vardef.value))
            return 1;
    }
    return 0;
}

int _resolver_EnsureLStoreAssign(
        asttransforminfo *rinfo, h64expression *expr
        ) {
    if (expr->type == H64EXPRTYPE_FUNCDEF_STMT ||
            expr->type == H64EXPRTYPE_INLINEFUNCDEF) {
        if (!expr->funcdef._storageinfo) {
            expr->funcdef._storageinfo = malloc(
                sizeof(*expr->funcdef._storageinfo)
            );
            if (!expr->funcdef._storageinfo) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
            memset(expr->funcdef._storageinfo, 0,
                   sizeof(*expr->funcdef._storageinfo));
        }
        if (expr->funcdef._storageinfo->lstoreassign_was_computed)
            return 1;
        expr->funcdef._storageinfo->lstoreassign_was_computed = 1;

        h64expression *owningclassexpr = surroundingclass(
            expr, 0
        );

        h64funcstorageextrainfo *einfo = expr->funcdef._storageinfo;
        if (einfo->closureboundvars_count > 0) {
            // It's a closure! Assign temporaries for actual values
            // (the parameter temporaries are used for the varbox
            // reference, not the actual value)
            int param_used_temps = (
                einfo->closureboundvars_count +
                expr->funcdef.arguments.arg_count +
                ((owningclassexpr != NULL ||
                  einfo->closure_with_self) ? 1 : 0)
            );
            int freetemp = param_used_temps;
            int i = 0;
            while (i < einfo->closureboundvars_count) {
                h64localstorageassign *newlstoreassign = realloc(
                    einfo->lstoreassign,
                    sizeof(*newlstoreassign) *
                    (einfo->lstoreassign_count + 1)
                );
                if (!newlstoreassign) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
                einfo->lstoreassign = newlstoreassign;
                memset(
                    &einfo->lstoreassign[
                        einfo->lstoreassign_count
                    ], 0, sizeof(*newlstoreassign)
                );
                einfo->lstoreassign[einfo->lstoreassign_count].
                    valuetemporaryid = freetemp;
                einfo->lstoreassign[einfo->lstoreassign_count].
                    valueboxtemporaryid = i + (
                    (owningclassexpr != NULL ||
                     einfo->closure_with_self) ? 1 : 0
                    );
                einfo->lstoreassign[einfo->lstoreassign_count].
                    vardef = einfo->closureboundvars[i];
                einfo->lstoreassign[einfo->lstoreassign_count].
                    ever_used_closure = 1;
                h64expression *vardefexpr = (
                    einfo->closureboundvars[i]->declarationexpr
                );
                assert(vardefexpr->type == H64EXPRTYPE_VARDEF_STMT);
                einfo->lstoreassign_count++;
                freetemp++;
                i++;
            }
            einfo->lowest_guaranteed_free_temp = freetemp;
        } else {
            einfo->lowest_guaranteed_free_temp = (
                einfo->closureboundvars_count +
                expr->funcdef.arguments.arg_count +
                ((owningclassexpr != NULL ||
                  einfo->closure_with_self) ? 1 : 0)
            );
        }
    }
    return 1;
}

int _resolvercallback_AssignNonglobalStorage_visit_in(
        h64expression *expr, ATTR_UNUSED h64expression *parent,
        void *ud
        ) {
    asttransforminfo *rinfo = (asttransforminfo *)ud;
    if (!_resolver_EnsureLStoreAssign(rinfo, expr)) {
        rinfo->hadoutofmemory = 1;
        return 0;
    }
    return 1;
}

int _resolver_EnsureLocalDefStorage(
        asttransforminfo *rinfo, h64expression *expr
        ) {
    if (expr->storage.set)
        return 1;
    // Look at all places where something is defined with local storage:
    if (expr->type == H64EXPRTYPE_VARDEF_STMT ||
            expr->type == H64EXPRTYPE_FUNCDEF_STMT ||
            expr->type == H64EXPRTYPE_FOR_STMT ||
            expr->type == H64EXPRTYPE_WITH_CLAUSE ||
            (expr->type == H64EXPRTYPE_DO_STMT &&
             expr->dostmt.error_name != NULL)) {
        h64expression *func = surroundingfunc(expr);
        if (!func)
            return 1;  // global, we don't care about that (non-local)
        if (!_resolver_EnsureLStoreAssign(rinfo, func)) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }
        if (!func->funcdef._storageinfo) {
            rinfo->hadunexpectederror = 1;
            return 0;
        }

        // Get scope, and figure out relevant usage range:
        h64scope *scope = (
            (expr->type == H64EXPRTYPE_VARDEF_STMT ?
             expr->vardef.foundinscope :
             (expr->type == H64EXPRTYPE_DO_STMT ?
              &expr->dostmt.rescuescope :
              (expr->type == H64EXPRTYPE_WITH_CLAUSE ?
               expr->withclause.foundinscope :
               (expr->type == H64EXPRTYPE_FUNCDEF_STMT ?
                expr->funcdef.foundinscope : &expr->forstmt.scope))))
        );
        #ifndef NDEBUG
        assert(expr->type != H64EXPRTYPE_VARDEF_STMT ||
               expr->vardef.identifier != NULL);
        assert(expr->type != H64EXPRTYPE_FUNCDEF_STMT ||
               expr->funcdef.name != NULL);
        assert(expr->type != H64EXPRTYPE_DO_STMT ||
               expr->dostmt.error_name != NULL);
        assert(expr->type != H64EXPRTYPE_FOR_STMT ||
               expr->forstmt.iterator_identifier != NULL);
        assert(expr->type != H64EXPRTYPE_WITH_CLAUSE ||
               expr->withclause.withitem_identifier != NULL);
        #endif
        h64scopedef *scopedef = scope_QueryItem(
            scope,
            (expr->type == H64EXPRTYPE_VARDEF_STMT ?
             expr->vardef.identifier : (
             expr->type == H64EXPRTYPE_FUNCDEF_STMT ?
             expr->funcdef.name : (
             expr->type == H64EXPRTYPE_WITH_CLAUSE ?
             expr->withclause.withitem_identifier : (
             (expr->type == H64EXPRTYPE_DO_STMT ?
              expr->dostmt.error_name :
              expr->forstmt.iterator_identifier))))),
            0
        );
        assert(scopedef != NULL);
        assert(scopedef->declarationexpr == expr);

        // Determine temporary slot to be used:
        h64funcstorageextrainfo *einfo = func->funcdef._storageinfo;
        int besttemp = -1;
        int valueboxid = -1;
        besttemp = einfo->lowest_guaranteed_free_temp;
        einfo->lowest_guaranteed_free_temp++;
        if (scopedef->closurebound) {
            valueboxid = einfo->lowest_guaranteed_free_temp;
            einfo->lowest_guaranteed_free_temp++;
        }

        // Insert actual temporary assignment:
        h64localstorageassign *newlstoreassign = realloc(
            einfo->lstoreassign,
            sizeof(*newlstoreassign) *
            (einfo->lstoreassign_count + 1)
        );
        if (!newlstoreassign) {
            rinfo->hadoutofmemory = 1;
            return 0;
        }
        einfo->lstoreassign = newlstoreassign;
        memset(
            &einfo->lstoreassign[einfo->lstoreassign_count], 0,
            sizeof(*newlstoreassign)
        );
        einfo->lstoreassign[einfo->lstoreassign_count].
            valuetemporaryid = besttemp;
        einfo->lstoreassign[einfo->lstoreassign_count].
            valueboxtemporaryid = valueboxid;
        einfo->lstoreassign[einfo->lstoreassign_count].
            vardef = scopedef;
        if (valueboxid >= 0) {
            einfo->lstoreassign[einfo->lstoreassign_count].
                ever_used_closure = 1;
        } else {
            einfo->lstoreassign[einfo->lstoreassign_count].
                ever_used_nonclosure = 1;
        }
        h64expression *vardefexpr = (
            scopedef->declarationexpr
        );
        assert(!vardefexpr->storage.set);
        vardefexpr->storage.set = 1;
        vardefexpr->storage.ref.type = H64STORETYPE_STACKSLOT;
        vardefexpr->storage.ref.id = besttemp;
        einfo->lstoreassign_count++;
    }
    return 1;
}

int _resolver_IsPossiblyGuardedInvalidMember(
        h64expression *expr
        ) {
    h64expression *child = NULL;
    while (expr) {
        if (child != NULL &&
                expr->type == H64EXPRTYPE_IF_STMT) {
            int got_is_a = guarded_by_is_a(expr);
            int got_has_attr = guarded_by_has_attr(expr);
            if (got_is_a || got_has_attr)
                return 1;
        }
        if (expr->type == H64EXPRTYPE_FUNCDEF_STMT)
            return 0;
        child = expr;
        expr = expr->parent;
    }
    return 0;
}

int _resolvercallback_AssignNonglobalStorage_visit_out(
        h64expression *expr, ATTR_UNUSED h64expression *parent,
        void *ud
        ) {
    asttransforminfo *rinfo = (asttransforminfo *)ud;

    // Compute local storage for the definitions themselves:
    if (!_resolver_EnsureLocalDefStorage(
            rinfo, expr
            )) {
        return 0;
    }

    // Ensure all identifiers referring to defs have storage set:
    if (expr->type == H64EXPRTYPE_IDENTIFIERREF &&
            !expr->storage.set) {
        h64expression *mapsto = expr->identifierref.resolved_to_expr;
        if (mapsto != NULL && mapsto->type != H64EXPRTYPE_IMPORT_STMT) {
            if (!mapsto->storage.set) {
                if (!_resolver_EnsureLocalDefStorage(rinfo, mapsto))
                    return 0;
            }
            if ((mapsto->type != H64EXPRTYPE_INLINEFUNCDEF &&
                    mapsto->type != H64EXPRTYPE_FUNCDEF_STMT) ||
                    !funcdef_has_parameter_with_name(
                    mapsto, expr->identifierref.value
                    )) {  // not a func param that may have no storage
                // -> check if it properly has storage set:
                if (!mapsto->storage.set &&
                        (!rinfo->pr->resultmsg->success ||
                        !rinfo->ast->resultmsg.success)) {
                    // Follow-up error most likely, just ignore this item.
                    rinfo->ast->resultmsg.success = 0;
                    rinfo->pr->resultmsg->success = 0;
                    return 1;
                }
                #ifndef NDEBUG
                if (!mapsto->storage.set) {
                    char *s = ast_ExpressionToJSONStr(
                        mapsto, rinfo->ast->fileuri
                    );
                    fprintf(stderr,
                        "horsec: error: unexpectedly no storage on "
                        "resolved-to expr: %s\n", (s ? s : "<oom>"));
                    free(s);
                    s = ast_ExpressionToJSONStr(
                        expr, rinfo->ast->fileuri
                    );
                    fprintf(stderr,
                        "horsec: error: identifier that was resolved to "
                        "expr: %s\n", (s ? s : "<oom>"));
                    free(s);
                }
                #endif
                assert(mapsto->storage.set);
                memcpy(
                    &expr->storage, &mapsto->storage, sizeof(expr->storage)
                );
                assert(expr->storage.set);
            } else {
                assert(rinfo->pr->program->symbols != NULL);
                assert(mapsto->funcdef.bytecode_func_id >= 0);
                h64funcsymbol *fsymbol = h64debugsymbols_GetFuncSymbolById(
                    rinfo->pr->program->symbols,
                    mapsto->funcdef.bytecode_func_id
                );
                assert(fsymbol != NULL);
                int argtempoffset = (
                    mapsto->funcdef._storageinfo->closureboundvars_count +
                    (fsymbol->has_self_arg ? 1 : 0)
                );
                int _found = 0;
                int i = 0;
                while (i < mapsto->funcdef.arguments.arg_count) {
                    if (strcmp(mapsto->funcdef.arguments.arg_name[i],
                               expr->identifierref.value) == 0) {
                        expr->storage.set = 1;
                        expr->storage.ref.type = H64STORETYPE_STACKSLOT;
                        expr->storage.ref.id = argtempoffset + i;
                        _found = 1;
                        break;
                    }
                    i++;
                }
                assert(_found != 0 && expr->storage.set);
            }
        }
    }
    // If we had no errors so far, resolve self.X fully at compile time:
    if (rinfo->pr->resultmsg->success &&
            rinfo->ast->resultmsg.success &&
            expr->type == H64EXPRTYPE_BINARYOP &&
            expr->op.optype == H64OP_ATTRIBUTEBYIDENTIFIER &&
            expr->op.value1->type == H64EXPRTYPE_IDENTIFIERREF &&
            strcmp(expr->op.value1->identifierref.value, "self") == 0 &&
            expr->op.value2->type == H64EXPRTYPE_IDENTIFIERREF) {
        h64expression *func = surroundingfunc(expr);
        if (!func) {
            char buf[256];
            snprintf(buf, sizeof(buf) - 1,
                "unexpected \"self\" reference outside of function"
            );
            if (!result_AddMessage(
                    &rinfo->ast->resultmsg,
                    H64MSG_ERROR, buf, rinfo->ast->fileuri,
                    expr->line,
                    expr->column
                    )) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
            return 1;
        }
        h64expression *cls = surroundingclass(expr, 1);
        if (!cls) {
            char buf[256];
            snprintf(buf, sizeof(buf) - 1,
                "unexpected \"self\" reference outside of class"
            );
            if (!result_AddMessage(
                    &rinfo->ast->resultmsg,
                    H64MSG_ERROR, buf, rinfo->ast->fileuri,
                    expr->line,
                    expr->column
                    )) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
            return 1;
        }
        classid_t clsid = cls->classdef.bytecode_class_id;
        assert(clsid >= 0 && clsid < rinfo->pr->program->classes_count);
        const char *name = expr->op.value2->identifierref.value;
        int64_t nameid = (
            h64debugsymbols_AttributeNameToAttributeNameId(
                rinfo->pr->program->symbols, name, 0
            )
        );
        attridx_t result = -1;
        int found = 0;
        if (nameid >= 0) {
            while (clsid >= 0) {
                if ((result = h64program_ClassNameToMemberIdx(
                        rinfo->pr->program, clsid, nameid
                        )) >= 0) {
                    found = 1;
                    break;
                }
                clsid = (
                    rinfo->pr->program->classes[clsid].
                    base_class_global_id);
            }
        }
        if (!found) {
            char namebuf[64];
            snprintf(namebuf, sizeof(namebuf) - 1, "%s", name);
            if (strlen(namebuf) >= sizeof(namebuf) - 2) {
                namebuf[sizeof(namebuf) - 4] = '.';
                namebuf[sizeof(namebuf) - 3] = '.';
                namebuf[sizeof(namebuf) - 2] = '.';
                namebuf[sizeof(namebuf) - 1] = '\0';
            }
            int instance_of_guarded = (
                _resolver_IsPossiblyGuardedInvalidMember(expr)
            );
            if (!instance_of_guarded) {
                char buf[256];
                snprintf(buf, sizeof(buf) - 1,
                    "unknown identifier \"%s\" on self "
                    "will cause AttributeError, wrap in conditional with "
                    "has_attr() or .is_a() if intended for API "
                    "compat",
                    namebuf
                );
                if (!result_AddMessage(
                        &rinfo->ast->resultmsg,
                        H64MSG_WARNING, buf, rinfo->ast->fileuri,
                        expr->line,
                        expr->column
                        )) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
            }
        } else {
            if (!expr->storage.set) {
                expr->storage.set = 1;
                expr->storage.ref.type = H64STORETYPE_VARATTRSLOT;
                expr->storage.ref.id = result;
            }
        }
    }
    return 1;
}

int varstorage_AssignLocalStorage(
        h64compileproject *pr, h64ast *ast
        ) {
    // Assign storage for all local variables and parameters:
    int transformresult = asttransform_Apply(
        pr, ast,
        &_resolvercallback_AssignNonglobalStorage_visit_in,
        &_resolvercallback_AssignNonglobalStorage_visit_out,
        NULL
    );
    if (!transformresult)
        return 0;

    return 1;
}

void varstorage_FreeExtraInfo(
        h64funcstorageextrainfo *einfo
        ) {
    if (!einfo)
        return;
    free(einfo->codegen.extra_temps_used);
    free(einfo->codegen.extra_temps_deletepastline);
    free(einfo->lstoreassign);
    free(einfo->closureboundvars);
    free(einfo);
}

jsonvalue *varstorage_ExtraInfoToJSON(
        h64funcstorageextrainfo *einfo
        ) {
    if (!einfo)
        return NULL;

    int fail = 0;
    jsonvalue *v = json_Dict();
    if (!v)
        return NULL;

    {
        jsonvalue *assignlist = json_List();

        int k = 0;
        while (k < einfo->lstoreassign_count) {
            jsonvalue *item = json_Dict();
            const char *name = NULL;
            if (einfo->lstoreassign[k].vardef->
                    declarationexpr->type == H64EXPRTYPE_VARDEF_STMT) {
                name = (einfo->lstoreassign[k].vardef->
                    declarationexpr->vardef.identifier);
            } else if (einfo->lstoreassign[k].vardef->
                    declarationexpr->type == H64EXPRTYPE_FUNCDEF_STMT) {
                name = (einfo->lstoreassign[k].vardef->
                    declarationexpr->funcdef.name);
            } else if (einfo->lstoreassign[k].vardef->
                    declarationexpr->type == H64EXPRTYPE_FOR_STMT) {
                name = (einfo->lstoreassign[k].vardef->
                    declarationexpr->forstmt.iterator_identifier);
            } else if (einfo->lstoreassign[k].vardef->
                    declarationexpr->type == H64EXPRTYPE_DO_STMT) {
                name = (einfo->lstoreassign[k].vardef->
                    declarationexpr->dostmt.error_name);
            }
            if ((name && !json_SetDictStr(item, "name", name)) ||
                    (!name && !json_SetDictNull(item, "name"))) {
                fail = 1;
                json_Free(item);
                break;
            }
            if (!json_SetDictInt(item, "value-temporary-id",
                    einfo->lstoreassign[k].valuetemporaryid)) {
                fail = 1;
                json_Free(item);
                break;
            }
            if (!json_SetDictInt(item, "valuebox-temporary-id",
                    einfo->lstoreassign[k].valueboxtemporaryid)) {
                fail = 1;
                json_Free(item);
                break;
            }
            if (!json_AddToList(assignlist, item)) {
                fail = 1;
                json_Free(item);
                break;
            }
            k++;
        }
        if (fail) {
            json_Free(assignlist);
        } else if (!json_SetDict(v, "local-storage-assignments",
                assignlist)) {
            fail = 1;
            json_Free(assignlist);
        }
    }

    if (fail) {
        json_Free(v);
        return NULL;
    }
    return v;
}

jsonvalue *varstorage_StorageAsJSON(h64expression *expr) {
    if (!expr)
        return NULL;
    jsonvalue *v = NULL;
    if ((expr->type == H64EXPRTYPE_FUNCDEF_STMT ||
            expr->type == H64EXPRTYPE_INLINEFUNCDEF) &&
            expr->funcdef._storageinfo != NULL) {
        v = varstorage_ExtraInfoToJSON(
            expr->funcdef._storageinfo
        );
        if (!v)
            return NULL;
    } else {
        v = malloc(sizeof(*v));
        if (!v)
            return NULL;
        memset(v, 0, sizeof(*v));
        v->type = JSON_VALUE_OBJECT;
    }
    if (!expr->storage.set)
        return v;
    switch (expr->storage.ref.type) {
        case H64STORETYPE_INVALID: {
            if (!json_SetDictStr(v, "type", "invalid")) {
                json_Free(v);
                return NULL;
            }
            break;
        }
        case H64STORETYPE_STACKSLOT: {
            if (!json_SetDictStr(v, "type", "stackslot")) {
                json_Free(v);
                return NULL;
            }
            if (!json_SetDictInt(v, "id", expr->storage.ref.id)) {
                json_Free(v);
                return NULL;
            }
            break;
        }
        case H64STORETYPE_GLOBALFUNCSLOT: {
            if (!json_SetDictStr(v, "type", "funcslot")) {
                json_Free(v);
                return NULL;
            }
            if (!json_SetDictInt(v, "id", expr->storage.ref.id)) {
                json_Free(v);
                return NULL;
            }
            break;
        }
        case H64STORETYPE_GLOBALCLASSSLOT: {
            if (!json_SetDictStr(v, "type", "classslot")) {
                json_Free(v);
                return NULL;
            }
            if (!json_SetDictInt(v, "id", expr->storage.ref.id)) {
                json_Free(v);
                return NULL;
            }
            break;
        }
        case H64STORETYPE_GLOBALVARSLOT: {
            if (!json_SetDictStr(v, "type", "globalvarslot")) {
                json_Free(v);
                return NULL;
            }
            if (!json_SetDictInt(v, "id", expr->storage.ref.id)) {
                json_Free(v);
                return NULL;
            }
            break;
        }
        case H64STORETYPE_VARATTRSLOT: {
            if (!json_SetDictStr(v, "type", "varattrslot")) {
                json_Free(v);
                return NULL;
            }
            if (!json_SetDictInt(v, "id", expr->storage.ref.id)) {
                json_Free(v);
                return NULL;
            }
            break;
        }
        default: {
            if (!json_SetDictStr(v, "type", "<unknown>")) {
                json_Free(v);
                return NULL;
            }
            if (!json_SetDictInt(v, "id", expr->storage.ref.id)) {
                json_Free(v);
                return NULL;
            }
        }
    }
    return v;
}
