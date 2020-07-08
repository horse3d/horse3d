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

static inline h64expression *statement_before_after_ex(
        h64ast *ast, h64expression *expr, int get_after) {
    h64expression *stmt = get_containing_statement(ast, expr);
    if (stmt->parent != NULL) {
        if (stmt->parent->type == H64EXPRTYPE_IF_STMT) {
            struct h64ifstmt *clause = &stmt->parent->ifstmt;
            while (clause) {
                int i = 0;
                while (i < clause->stmt_count) {
                    if (clause->stmt[i] == stmt) {
                        if (likely(!get_after && i > 0))
                            return clause->stmt[i - 1];
                        if (likely(get_after && i + 1 <
                                   clause->stmt_count))
                            return clause->stmt[i + 1];
                        return NULL;
                    }
                    i++;
                }
                clause = clause->followup_clause;
            }
            assert(0 && "invalid statement not found in AST");
            return NULL;
        } else if (stmt->parent->type == H64EXPRTYPE_FOR_STMT) {
            int i = 0;
            while (i < stmt->parent->forstmt.stmt_count) {
                if (stmt->parent->forstmt.stmt[i] == stmt) {
                    if (likely(!get_after && i > 0))
                        return stmt->parent->forstmt.stmt[i - 1];
                    if (likely(get_after && i + 1 <
                               stmt->parent->forstmt.stmt_count))
                        return stmt->parent->forstmt.stmt[i + 1];
                    return NULL;
                }
                i++;
            }
            assert(0 && "invalid statement not found in AST");
            return NULL;
        } else if (stmt->parent->type == H64EXPRTYPE_WHILE_STMT) {
            int i = 0;
            while (i < stmt->parent->whilestmt.stmt_count) {
                if (stmt->parent->whilestmt.stmt[i] == stmt) {
                    if (likely(!get_after && i > 0))
                        return stmt->parent->whilestmt.stmt[i - 1];
                    if (likely(get_after && i + 1 <
                               stmt->parent->whilestmt.stmt_count))
                        return stmt->parent->whilestmt.stmt[i + 1];
                    return NULL;
                }
                i++;
            }
            assert(0 && "invalid statement not found in AST");
            return NULL;
        } else if (stmt->parent->type == H64EXPRTYPE_FUNCDEF_STMT ||
                   stmt->parent->type == H64EXPRTYPE_INLINEFUNCDEF) {
            int i = 0;
            while (i < stmt->parent->funcdef.stmt_count) {
                if (stmt->parent->funcdef.stmt[i] == stmt) {
                    if (likely(!get_after && i > 0))
                        return stmt->parent->funcdef.stmt[i - 1];
                    if (likely(get_after && i + 1 <
                               stmt->parent->funcdef.stmt_count))
                        return stmt->parent->funcdef.stmt[i + 1];
                    return NULL;
                }
                i++;
            }
            assert(0 && "invalid statement not found in AST");
            return NULL;
        } else if (stmt->parent->type == H64EXPRTYPE_CLASSDEF_STMT) {
            int i = 0;
            while (i < stmt->parent->classdef.vardef_count) {
                if (stmt->parent->classdef.vardef[i] == stmt) {
                    if (likely(!get_after && i > 0))
                        return stmt->parent->classdef.vardef[i - 1];
                    if (likely(get_after && i + 1 <
                               stmt->parent->classdef.vardef_count))
                        return stmt->parent->classdef.vardef[i + 1];
                    return NULL;
                }
                i++;
            }
            i = 0;
            while (i < stmt->parent->classdef.funcdef_count) {
                if (stmt->parent->classdef.funcdef[i] == stmt) {
                    if (likely(!get_after && i > 0))
                        return stmt->parent->classdef.funcdef[i - 1];
                    if (likely(get_after && i + 1 <
                               stmt->parent->classdef.funcdef_count))
                        return stmt->parent->classdef.funcdef[i + 1];
                    return NULL;
                }
                i++;
            }
            assert(0 && "invalid statement not found in AST");
            return NULL;
        } else if (stmt->parent->type == H64EXPRTYPE_TRY_STMT) {
            int i = 0;
            while (i < stmt->parent->trystmt.trystmt_count) {
                if (stmt->parent->trystmt.trystmt[i] == stmt) {
                    if (likely(!get_after && i > 0))
                        return stmt->parent->trystmt.trystmt[i - 1];
                    if (likely(get_after && i + 1 <
                               stmt->parent->trystmt.trystmt_count))
                        return stmt->parent->trystmt.trystmt[i + 1];
                    return NULL;
                }
                i++;
            }
            i = 0;
            while (i < stmt->parent->trystmt.catchstmt_count) {
                if (stmt->parent->trystmt.catchstmt[i] == stmt) {
                    if (likely(!get_after && i > 0))
                        return stmt->parent->trystmt.catchstmt[i - 1];
                    if (likely(get_after && i + 1 <
                               stmt->parent->trystmt.catchstmt_count))
                        return stmt->parent->trystmt.catchstmt[i + 1];
                    return NULL;
                }
                i++;
            }
            i = 0;
            while (i < stmt->parent->trystmt.finallystmt_count) {
                if (stmt->parent->trystmt.finallystmt[i] == stmt) {
                    if (likely(!get_after && i > 0))
                        return stmt->parent->trystmt.finallystmt[i - 1];
                    if (likely(get_after && i + 1 <
                               stmt->parent->trystmt.finallystmt_count))
                        return stmt->parent->trystmt.finallystmt[i + 1];
                    return NULL;
                }
                i++;
            }
            assert(0 && "invalid statement not found in AST");
            return NULL;
        }
        assert(0 && "unrecognized statement parent type");
        return NULL;
    } else {
        int i = 0;
        while (i < ast->stmt_count) {
            if (ast->stmt[i] == stmt) {
                if (likely(i > 0 && !get_after))
                    return ast->stmt[i - 1];
                if (likely(i + 1 < ast->stmt_count && get_after))
                    return ast->stmt[i + 1];
                return NULL;
            }
            i++;
        }
        assert(0 && "invalid statement not found in AST");
        return NULL;
    }
}

int execution_flow_backwards(
        h64ast *ast, h64expression *expr,
        int *out_prevstatement_count,
        h64expression ***out_prevstatement
        ) {
    h64expression *stmt = get_containing_statement(ast, expr);
    h64expression *stmt_before = statement_before(ast, expr);
    if (stmt_before) {

    }
    return 0;
}

h64expression *statement_before(
        h64ast *ast, h64expression *expr
        ) {
    return statement_before_after_ex(ast, expr, 0);
}

h64expression *statement_after(
        h64ast *ast, h64expression *expr
        ) {
    return statement_before_after_ex(ast, expr, 1);
}
