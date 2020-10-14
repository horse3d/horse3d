// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <string.h>

#include "compiler/ast.h"
#include "compiler/asthelpers.h"
#include "compiler/astparser.h"
#include "compiler/asttransform.h"

struct _find_expr_by_tokenindex {
    int64_t searchindex, lowest_seen_index, highest_seen_index;
    h64expression *result;
};

int _find_expr_by_tokenindex_visit_in(
        h64expression *expr,
        ATTR_UNUSED h64expression *parent,
        void *ud) {
    struct _find_expr_by_tokenindex *searchinfo = ud;
    int64_t i = expr->tokenindex;
    if (i >= 0) {
        if (i < searchinfo->lowest_seen_index ||
                searchinfo->lowest_seen_index < 0)
            searchinfo->lowest_seen_index = i;
        if (i > searchinfo->highest_seen_index)
            searchinfo->highest_seen_index = i;
    }
    if (searchinfo->searchindex >= 0 &&
            expr->tokenindex == searchinfo->searchindex)
        searchinfo->result = expr;
    return 1;
}

h64expression *find_expr_by_tokenindex_ex(
        h64expression *search_in,
        int64_t searchindex,
        int64_t *out_lowestseenindex,
        int64_t *out_highestseenindex
        ) {
    struct _find_expr_by_tokenindex sinfo = {0};
    sinfo.searchindex = searchindex;
    sinfo.lowest_seen_index = -1;
    sinfo.highest_seen_index = -1;
    int result = ast_VisitExpression(
        search_in, search_in->parent,
        &_find_expr_by_tokenindex_visit_in,
        NULL, NULL, &sinfo
    );
    assert(result != 0);
    if (out_lowestseenindex)
        *out_lowestseenindex = sinfo.lowest_seen_index;
    if (out_highestseenindex)
        *out_highestseenindex = sinfo.highest_seen_index;
    return sinfo.result;
}

h64expression *find_expr_by_tokenindex(
        h64expression *search_in,
        int64_t searchindex
        ) {
    return find_expr_by_tokenindex_ex(
        search_in, searchindex, NULL, NULL
    );
}

void get_tokenindex_range(
        h64expression *expr,
        int64_t *lowest_seen_idx, int64_t *highest_seen_idx
        ) {
    *lowest_seen_idx = -1;
    *highest_seen_idx = -1;
    find_expr_by_tokenindex_ex(
        expr, -1, lowest_seen_idx, highest_seen_idx
    );
    if (expr->tokenindex >= 0 &&
            (*lowest_seen_idx > expr->tokenindex ||
             *lowest_seen_idx < 0))
        *lowest_seen_idx = expr->tokenindex;
    if (expr->tokenindex >= 0 &&
            (*highest_seen_idx < expr->tokenindex ||
             *highest_seen_idx < 0))
        *highest_seen_idx = expr->tokenindex;
}

h64expression *get_containing_statement(
        ATTR_UNUSED h64ast *ast, h64expression *expr
        ) {
    while (expr) {
        if (IS_STMT(expr->type))
            return expr;
        expr = expr->parent;
    }
    return NULL;
}


int _is_simple_constant_expr_inner(h64expression *expr) {
    if (expr->type == H64EXPRTYPE_LITERAL) {
        return 1;
    } else if ((expr->type == H64EXPRTYPE_BINARYOP ||
            expr->type == H64EXPRTYPE_UNARYOP) &&
            expr->op.optype != H64OP_NEW &&
            (expr->op.optype != H64OP_ATTRIBUTEBYIDENTIFIER ||
             (expr->type == H64EXPRTYPE_BINARYOP &&
              expr->op.value2->type == H64EXPRTYPE_IDENTIFIERREF && (
              strcmp(expr->op.value2->identifierref.value,
                     "as_str") == 0 ||
              strcmp(expr->op.value2->identifierref.value,
                     "len") == 0)))) {
        if (expr->type == H64EXPRTYPE_BINARYOP &&
                expr->op.optype != H64OP_ATTRIBUTEBYIDENTIFIER) {
            return (
                _is_simple_constant_expr_inner(expr->op.value1) &&
                _is_simple_constant_expr_inner(expr->op.value2)
            );
        }
        return _is_simple_constant_expr_inner(expr->op.value1);
    }
    return 0;
}

int is_simple_constant_expr(h64expression *expr) {
    if (expr->type == H64EXPRTYPE_INLINEFUNCDEF)
        return 1;
    return _is_simple_constant_expr_inner(expr);
}

int func_has_param_with_name(h64expression *expr, const char *param) {
    assert(expr->type == H64EXPRTYPE_FUNCDEF_STMT ||
           expr->type == H64EXPRTYPE_INLINEFUNCDEF);
    int i = 0;
    while (i < expr->funcdef.arguments.arg_count) {
        if (expr->funcdef.arguments.arg_name[i] &&
                strcmp(expr->funcdef.arguments.arg_name[i], param) == 0)
            return 1;
        i++;
    }
    return 0;
}