
#include "compiler/ast.h"
#include "compiler/astparser.h"
#include "compiler/asttransform.h"
#include "compiler/compileproject.h"
#include "compiler/varstorage.h"


int _resolvercallback_AssignNonglobalStorage_visit_out(
        h64expression *expr, h64expression *parent, void *ud
        ) {
    asttransforminfo *rinfo = (asttransforminfo *)ud;

    if (expr->type == H64EXPRTYPE_FUNCDEF_STMT ||
            expr->type == H64EXPRTYPE_INLINEFUNCDEF) {

    }
}

int varstorage_AssignLocalStorage(
        h64compileproject *pr, h64ast *ast
        ) {
    // Assign storage for all local variables and parameters:
    int transformresult = asttransform_Apply(
        pr, ast, NULL,
        &_resolvercallback_AssignNonglobalStorage_visit_out,
        NULL
    );
    if (!transformresult)
        return 0;

    return 1;
}
