// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_COMPILER_ASTHELPERS_H_
#define HORSE64_COMPILER_ASTHELPERS_H_

#include "compileconfig.h"

#include "compiler/ast.h"
#include "compiler/astparser.h"


ATTR_UNUSED static h64expression *surroundingfunc(h64expression *expr) {
    assert(expr != NULL);
    while (expr->parent) {
        expr = expr->parent;
        if (expr->type == H64EXPRTYPE_FUNCDEF_STMT ||
                expr->type == H64EXPRTYPE_INLINEFUNCDEF)
            return expr;
    }
    return NULL;
}

ATTR_UNUSED static h64expression *surroundingclass(
        h64expression *expr, int allowfuncnesting
        ) {
    while (expr->parent) {
        expr = expr->parent;
        if (!allowfuncnesting && (
                expr->type == H64EXPRTYPE_FUNCDEF_STMT ||
                expr->type == H64EXPRTYPE_INLINEFUNCDEF))
            return NULL;
        if (expr->type == H64EXPRTYPE_CLASSDEF_STMT)
            return expr;
    }
    return NULL;
}

ATTR_UNUSED static int isinsideanyfunction(h64expression *expr) {
    return (surroundingfunc(expr) != NULL);
}

ATTR_UNUSED static int isinsideclosure(h64expression *expr) {
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

ATTR_UNUSED static int isexprchildof(
        h64expression *expr, h64expression *checkparent
        ) {
    while (expr->parent) {
        expr = expr->parent;
        if (expr == checkparent)
            return 1;
    }
    return 0;
}

ATTR_UNUSED static int funcdef_has_parameter_with_name(
        h64expression *expr, const char *name) {
    if (expr->type == H64EXPRTYPE_FUNCDEF_STMT
            || expr->type == H64EXPRTYPE_INLINEFUNCDEF) {
        int i = 0;
        while (i < expr->funcdef.arguments.arg_count) {
            if (strcmp(expr->funcdef.arguments.arg_name[i], name) == 0)
                return 1;
            i++;
        }
        return 0;
    } else {
        return 0;
    }
}

h64expression *find_expr_by_tokenindex(
    h64expression *search_in,
    int64_t searchindex
);

void get_tokenindex_range(
    h64expression *expr,
    int64_t *lowest_seen_idx, int64_t *highest_seen_idx
);

h64expression *get_containing_statement(
    h64ast *ast, h64expression *expr
);


int is_simple_constant_expr(h64expression *expr);

int func_has_param_with_name(h64expression *expr, const char *param);

int guarded_by_is_a(h64expression *expr);

int guarded_by_has_attr(h64expression *expr);

#endif  // HORSE64_COMPILER_ASTHELPERS_H_
