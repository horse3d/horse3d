
#include <assert.h>
#include <stdio.h>

#include "compiler/ast.h"
#include "compiler/astparser.h"
#include "compiler/asttransform.h"
#include "compiler/compileproject.h"
#include "compiler/varstorage.h"


static h64expression *surroundingfunc(h64expression *expr) {
    while (expr->parent) {
        expr = expr->parent;
        if (expr->type == H64EXPRTYPE_FUNCDEF_STMT ||
                expr->type == H64EXPRTYPE_INLINEFUNCDEF)
            return expr;
    }
    return NULL;
}

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

int _resolvercallback_AssignNonglobalStorage_visit_in(
        h64expression *expr, h64expression *parent, void *ud
        ) {
    asttransforminfo *rinfo = (asttransforminfo *)ud;

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
        h64storageextrainfo *einfo = expr->funcdef._storageinfo;
        if (einfo->closureboundvars_count > 0) {
            // It's a closure! Assign temporaries for actual values
            // (the parameter temporaries are used for the varbox
            // reference, not the actual value)
            int param_used_temps = (
                einfo->closureboundvars_count +
                expr->funcdef.arguments.arg_count
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
                    &einfo->lstoreassign[einfo->lstoreassign_count], 0,
                    sizeof(*newlstoreassign)
                );
                einfo->lstoreassign[einfo->lstoreassign_count].
                    valuetemporaryid = freetemp;
                einfo->lstoreassign[einfo->lstoreassign_count].
                    valueboxtemporaryid = i;
                einfo->lstoreassign[einfo->lstoreassign_count].
                    vardef = einfo->closureboundvars[i];
                einfo->lstoreassign[einfo->lstoreassign_count].
                    use_start_token_index = (
                    einfo->closureboundvars[i]->first_use_token_index
                    );
                if (!nosideeffectsdef(einfo->closureboundvars[i]->
                                declarationexpr)) {
                    if (einfo->closureboundvars[i]->declarationexpr->
                            tokenindex <
                            einfo->closureboundvars[i]->
                            first_use_token_index) {
                        einfo->lstoreassign[einfo->lstoreassign_count].
                            use_start_token_index
                            = (
                            einfo->closureboundvars[i]->declarationexpr->
                            tokenindex);
                    }
                }
                einfo->lstoreassign[einfo->lstoreassign_count].
                    use_end_token_index = (
                    einfo->closureboundvars[i]->last_use_token_index
                    );
                einfo->lstoreassign_count++;
                freetemp++;
                i++;
            }
            einfo->lowest_guaranteed_free_temp = freetemp;
        } else {
            einfo->lowest_guaranteed_free_temp = (
                einfo->closureboundvars_count +
                expr->funcdef.arguments.arg_count
            );
        }
    }
    return 1;
}

int _resolvercallback_AssignNonglobalStorage_visit_out(
        h64expression *expr, h64expression *parent, void *ud
        ) {
    asttransforminfo *rinfo = (asttransforminfo *)ud;

    if (expr->type == H64EXPRTYPE_VARDEF_STMT ||
            expr->type == H64EXPRTYPE_FUNCDEF_STMT) {
        h64expression *func = surroundingfunc(expr);
        if (!func)
            return 1;  // global, we don't care about that
        if (!func->funcdef._storageinfo) {
            rinfo->hadunexpectederror = 1;
            return 0;
        }

        // Get scope, and figure out relevant usage range:
        h64scope *scope = (
            expr->type == H64EXPRTYPE_VARDEF_STMT ?
            expr->vardef.foundinscope : expr->funcdef.foundinscope
        );
        h64scopedef *scopedef = scope_QueryItem(
            scope,
            (expr->type == H64EXPRTYPE_VARDEF_STMT ?
             expr->vardef.identifier : expr->funcdef.name),
            0
        );
        assert(scopedef != NULL);
        int own_token_start = scopedef->first_use_token_index;
        int own_token_end = scopedef->first_use_token_index;
        if (!nosideeffectsdef(expr) && expr->tokenindex < own_token_start) {
            own_token_start = expr->tokenindex;
        }

        // Determine temporary slot to be used:
        h64storageextrainfo *einfo = func->funcdef._storageinfo;
        int besttemp = -1;
        int besttemp_score = -1;
        int valueboxid = -1;
        if (scopedef->everused || nosideeffectsdef(
                scopedef->declarationexpr)) {
            int k = 0;
            while (k < einfo->lstoreassign_count) {
                int score = -1;
                if (einfo->lstoreassign[k].use_end_token_index <
                        own_token_start) {
                    score = INT_MAX - (
                        own_token_start -
                        einfo->lstoreassign[k].use_end_token_index
                    );
                } else if (einfo->lstoreassign[k].use_start_token_index >
                        own_token_end) {
                    score = INT_MAX - (
                        own_token_end -
                        einfo->lstoreassign[k].use_start_token_index
                    );
                }
                if (score > 0 && (
                        score < besttemp_score ||
                        besttemp < 0)) {
                    besttemp = einfo->lstoreassign[k].valuetemporaryid;
                    besttemp_score = score;
                }
                k++;
            }
            if (besttemp < 0) {
                besttemp = einfo->lowest_guaranteed_free_temp;
                einfo->lowest_guaranteed_free_temp++;
            }
            assert(besttemp >= 0);
            if (scopedef->closurebound) {
                valueboxid = einfo->lowest_guaranteed_free_temp;
                einfo->lowest_guaranteed_free_temp++;
            }
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
        einfo->lstoreassign[einfo->lstoreassign_count].
            use_start_token_index = own_token_start;
        einfo->lstoreassign[einfo->lstoreassign_count].
            use_end_token_index = own_token_end;
        einfo->lstoreassign_count++;
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
        h64storageextrainfo *einfo
        ) {
    if (!einfo)
        return;
    free(einfo->lstoreassign);
    free(einfo->closureboundvars);
    free(einfo);
}

jsonvalue *varstorage_ExtraInfoToJSON(
        h64storageextrainfo *einfo
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
