
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
                einfo->externalclosurevar_valuetempid[i] = (
                    freetemp
                );
                freetemp++;
                i++;
            }
            einfo->temps_for_locals_startindex = freetemp;
        } else {
            einfo->temps_for_locals_startindex = (
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

    if (expr->type == H64EXPRTYPE_VARDEF_STMT) {
        h64expression *func = surroundingfunc(expr);
        if (!func)
            return 1;  // global, we don't care about that
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
    free(einfo->closureboundvars);
    free(einfo->externalclosurevar_valuetempid);
    free(einfo);
}
