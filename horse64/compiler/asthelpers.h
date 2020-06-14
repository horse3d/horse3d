#ifndef HORSE64_COMPILER_ASTHELPERS_H_
#define HORSE64_COMPILER_ASTHELPERS_H_

#include "compiler/ast.h"

static h64expression *surroundingfunc(h64expression *expr) {
    while (expr->parent) {
        expr = expr->parent;
        if (expr->type == H64EXPRTYPE_FUNCDEF_STMT ||
                expr->type == H64EXPRTYPE_INLINEFUNCDEF)
            return expr;
    }
    return NULL;
}

static int isinsideanyfunction(h64expression *expr) {
    return (surroundingfunc(expr) != NULL);
}

static int isinsideclosure(h64expression *expr) {
    int surroundingfuncscount = 0;
    while (expr->parent) {
        expr = expr->parent;
        if (expr->type == H64EXPRTYPE_FUNCDEF_STMT ||
                expr->type == H64EXPRTYPE_INLINEFUNCDEF) {
            surroundingfuncscount++;
        }
    }
    return (surroundingfuncscount > 1);
}

static int isexprchildof(
        h64expression *expr, h64expression *checkparent
        ) {
    while (expr->parent) {
        expr = expr->parent;
        if (expr == checkparent)
            return 1;
    }
    return 0;
}

#endif  // HORSE64_COMPILER_ASTHELPERS_H_
