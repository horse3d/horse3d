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
#include "compiler/codeflow.h"


int _setbeforeafter_visit_out(
        h64expression *expr, ATTR_UNUSED h64expression *parent,
        ATTR_UNUSED void *ud
        ) {
    if (expr->type == H64EXPRTYPE_IF_STMT) {
        struct h64ifstmt *clause = &expr->ifstmt;
        while (clause) {
            int i = 0;
            while (i < clause->stmt_count) {
                if (!clause->stmt[i]->flow.set) {
                    clause->stmt[i]->flow.set = 1;
                    clause->stmt[i]->flow.prev = (
                        i > 0 ? clause->stmt[i - 1] : NULL
                    );
                    clause->stmt[i]->flow.next = (
                        i + 1 < clause->stmt_count ?
                        clause->stmt[i + 1] : NULL
                    );
                }
                i++;
            }
            clause = clause->followup_clause;
        }
    } else if (expr->type == H64EXPRTYPE_FOR_STMT) {
        int i = 0;
        while (i < expr->forstmt.stmt_count) {
            if (!expr->forstmt.stmt[i]->flow.set) {
                expr->forstmt.stmt[i]->flow.set = 1;
                expr->forstmt.stmt[i]->flow.prev = (
                    i > 0 ? expr->forstmt.stmt[i - 1] : NULL
                );
                expr->forstmt.stmt[i]->flow.next = (
                    i + 1 < expr->forstmt.stmt_count ?
                    expr->forstmt.stmt[i + 1] : NULL
                );
            }
            i++;
        }
    } else if (expr->type == H64EXPRTYPE_WHILE_STMT) {
        int i = 0;
        while (i < expr->whilestmt.stmt_count) {
            if (!expr->whilestmt.stmt[i]->flow.set) {
                expr->whilestmt.stmt[i]->flow.set = 1;
                expr->whilestmt.stmt[i]->flow.prev = (
                    i > 0 ? expr->whilestmt.stmt[i - 1] : NULL
                );
                expr->whilestmt.stmt[i]->flow.next = (
                    i + 1 < expr->whilestmt.stmt_count ?
                    expr->whilestmt.stmt[i + 1] : NULL
                );
            }
            i++;
        }
    } else if (expr->type == H64EXPRTYPE_FUNCDEF_STMT ||
               expr->type == H64EXPRTYPE_INLINEFUNCDEF) {
        int i = 0;
        while (i < expr->funcdef.stmt_count) {
            if (!expr->funcdef.stmt[i]->flow.set) {
                expr->funcdef.stmt[i]->flow.set = 1;
                expr->funcdef.stmt[i]->flow.prev = (
                    i > 0 ? expr->funcdef.stmt[i - 1] : NULL
                );
                expr->funcdef.stmt[i]->flow.next = (
                    i + 1 < expr->funcdef.stmt_count ?
                    expr->funcdef.stmt[i + 1] : NULL
                );
            }
            i++;
        }
    } else if (expr->type == H64EXPRTYPE_CLASSDEF_STMT) {
        int i = 0;
        while (i < expr->classdef.vardef_count) {
            if (!expr->classdef.vardef[i]->flow.set) {
                expr->classdef.vardef[i]->flow.set = 1;
                expr->classdef.vardef[i]->flow.prev = (
                    i > 0 ? expr->classdef.vardef[i - 1] : NULL
                );
                expr->classdef.vardef[i]->flow.next = (
                    i + 1 < expr->classdef.vardef_count ?
                    expr->classdef.vardef[i + 1] : (
                        expr->classdef.funcdef_count > 0 ?
                        expr->classdef.funcdef[0] : NULL
                    )
                );
            }
            i++;
        }
        i = 0;
        while (i < expr->classdef.funcdef_count) {
            if (!expr->classdef.funcdef[i]->flow.set) {
                expr->classdef.funcdef[i]->flow.set = 1;
                expr->classdef.funcdef[i]->flow.prev = (
                    i > 0 ? expr->classdef.funcdef[i - 1] : (
                        expr->classdef.vardef_count > 0 ?
                        expr->classdef.vardef[
                            expr->classdef.vardef_count - 1
                        ] : NULL
                    )
                );
                expr->classdef.funcdef[i]->flow.next = (
                    i + 1 < expr->classdef.funcdef_count ?
                    expr->classdef.funcdef[i + 1] : NULL
                );
            }
            i++;
        }
    } else if (expr->type == H64EXPRTYPE_TRY_STMT) {
        int i = 0;
        while (i < expr->trystmt.trystmt_count) {
            if (!expr->trystmt.trystmt[i]->flow.set) {
                expr->trystmt.trystmt[i]->flow.set = 1;
                expr->trystmt.trystmt[i]->flow.prev = (
                    i > 0 ? expr->trystmt.trystmt[i - 1] : NULL
                );
                expr->trystmt.trystmt[i]->flow.next = (
                    i + 1 < expr->trystmt.trystmt_count ?
                    expr->trystmt.trystmt[i + 1] : NULL
                );
            }
            i++;
        }
        i = 0;
        while (i < expr->trystmt.catchstmt_count) {
            if (!expr->trystmt.catchstmt[i]->flow.set) {
                expr->trystmt.catchstmt[i]->flow.set = 1;
                expr->trystmt.catchstmt[i]->flow.prev = (
                    i > 0 ? expr->trystmt.catchstmt[i - 1] : NULL
                );
                expr->trystmt.catchstmt[i]->flow.next = (
                    i + 1 < expr->trystmt.catchstmt_count ?
                    expr->trystmt.catchstmt[i + 1] : NULL
                );
            }
            i++;
        }
        i = 0;
        while (i < expr->trystmt.finallystmt_count) {
            if (!expr->trystmt.finallystmt[i]->flow.set) {
                expr->trystmt.finallystmt[i]->flow.set = 1;
                expr->trystmt.finallystmt[i]->flow.prev = (
                    i > 0 ? expr->trystmt.finallystmt[i - 1] : NULL
                );
                expr->trystmt.finallystmt[i]->flow.next = (
                    i + 1 < expr->trystmt.finallystmt_count ?
                    expr->trystmt.finallystmt[i + 1] : NULL
                );
            }
            i++;
        }
    }
    return 1;
}

void codeflow_SetBeforeAfter(h64compileproject *pr, h64ast *ast) {
    int i = 0;
    while (i < ast->stmt_count) {
        if (!ast->stmt[i]->flow.set) {
            ast->stmt[i]->flow.set = 1;
            ast->stmt[i]->flow.prev = (
                i > 0 ? ast->stmt[i - 1] : NULL
            );
            ast->stmt[i]->flow.next = (
                i + 1 < ast->stmt_count ?
                ast->stmt[i + 1] : NULL
            );
        }
        i++;
    }
    int transformresult = asttransform_Apply(
        pr, ast,
        NULL, &_setbeforeafter_visit_out,
        NULL
    );
    assert(transformresult != 0);
}

int codeflow_FollowFlowBackwards(
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
    h64expression *stmt = get_containing_statement(ast, expr);
    assert(stmt->flow.set);
    return stmt->flow.prev;
}

h64expression *statement_after(
        h64ast *ast, h64expression *expr
        ) {
    h64expression *stmt = get_containing_statement(ast, expr);
    assert(stmt->flow.set);
    return stmt->flow.next;
}
