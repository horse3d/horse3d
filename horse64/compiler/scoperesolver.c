
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "compiler/ast.h"
#include "compiler/astparser.h"
#include "compiler/compileproject.h"
#include "compiler/scoperesolver.h"
#include "compiler/scope.h"

typedef struct resolveinfo {
    h64compileproject *pr;
    h64ast *ast;
    int isbuiltinmodule;
    int hadoutofmemory;
} resolveinfo;

static int identifierisbuiltin(
        const char *identifier, int isbuiltinmodule
        ) {
    if (strcmp(identifier, "print") == 0 ||
            strcmp(identifier, "instanceof") == 0 ||
            strcmp(identifier, "Exception") == 0 ||
            strcmp(identifier, "RuntimeException") == 0 ||
            strcmp(identifier, "IOException") == 0 ||
            strcmp(identifier, "OSException") == 0) {
        return 1;
    }
    return 0;
}

const char *_shortenedname(
    char *buf, const char *name
);

int _resolvercallback_ResolveIdentifiers_visit_out(
        h64expression *expr, h64expression *parent, void *ud
        ) {
    resolveinfo *rinfo = (resolveinfo *)ud;
    if (expr->type == H64EXPRTYPE_IDENTIFIERREF &&
            (parent == NULL ||
             parent->type != H64EXPRTYPE_BINARYOP ||
             parent->op.value1 == expr)) {
        h64scope *scope = ast_GetScope(expr, &rinfo->ast->scope);
        if (scope == NULL) {
            char buf[256];
            snprintf(buf, sizeof(buf) - 1,
                "internal error: failed to obtain scope, "
                "malformed AST? expr: %s, parent: %s",
                ast_ExpressionTypeToStr(expr->type),
                (expr->parent ? ast_ExpressionTypeToStr(expr->parent->type) :
                 "none")
            );
            if (!result_AddMessage(
                    &rinfo->ast->resultmsg,
                    H64MSG_ERROR,
                    buf,
                    rinfo->ast->fileuri,
                    expr->line, expr->column
                    )) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
            return 1;
        }
        h64scopedef *def = scope_QueryItem(
            scope, expr->identifierref.value, 1
        );
        if (def) {
            expr->identifierref.resolved_to_def = def;
        } else if (identifierisbuiltin(
                expr->identifierref.value, rinfo->isbuiltinmodule)) {
            expr->identifierref.resolved_to_builtin = 1;
        } else {
            char buf[256]; char describebuf[64];
            snprintf(buf, sizeof(buf) - 1,
                "unexpected unknown identifier \"%s\", variable "
                "or module not found",
                _shortenedname(describebuf, expr->identifierref.value)
            );
            if (!result_AddMessage(
                    &rinfo->ast->resultmsg,
                    H64MSG_ERROR,
                    buf,
                    rinfo->ast->fileuri,
                    expr->line, expr->column
                    )) {
                rinfo->hadoutofmemory = 1;
                return 0;
            }
            return 1;
        }
    }
    return 1;
}

int scoperesolver_ResolveAST(
        h64compileproject *pr, h64ast *unresolved_ast
        ) {
    // First, make sure all imports are loaded up:
    int i = 0;
    while (i < unresolved_ast->scope.definitionref_count) {
        h64expression *expr = (
            unresolved_ast->scope.definitionref[i].declarationexpr
        );
        if (expr->type != H64EXPRTYPE_IMPORT_STMT) {
            i++;
            continue;
        }
        int oom = 0;
        char *file_path = compileproject_ResolveImport(
            pr, unresolved_ast->fileuri,
            (const char **)expr->importstmt.import_elements,
            expr->importstmt.import_elements_count,
            expr->importstmt.source_library,
            &oom
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
                int i = 0;
                while (i < expr->importstmt.import_elements_count) {
                    if (i > 0 && strlen(modpath) < sizeof(modpath) - 1)
                        strncat(modpath, ".",
                                sizeof(modpath) - strlen(modpath) - 1);
                    if (strlen(modpath) < sizeof(modpath) - 1)
                        strncat(modpath, expr->importstmt.import_elements[i],
                                sizeof(modpath) - strlen(modpath) - 1);
                    i++;
                }
                if (strlen(modpath) >= sizeof(modpath) - 1) {
                    modpath[sizeof(modpath) - 4] = '.';
                    modpath[sizeof(modpath) - 3] = '.';
                    modpath[sizeof(modpath) - 2] = '.';
                    modpath[sizeof(modpath) - 1] = '\0';
                }
                snprintf(buf, sizeof(buf) - 1,
                    "couldn't resolve import, module \"%s\" not found",
                    modpath
                );
                unresolved_ast->resultmsg.success = 0;
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
            unresolved_ast->resultmsg.success = 0;
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

    // Now, actually resolve identifiers:
    resolveinfo rinfo;
    memset(&rinfo, 0, sizeof(rinfo));
    rinfo.pr = pr;
    rinfo.ast = unresolved_ast;
    int msgcount = unresolved_ast->resultmsg.message_count;
    int k = 0;
    while (k < unresolved_ast->stmt_count) {
        int result = ast_VisitExpression(
            unresolved_ast->stmt[k], NULL,
            NULL,
            &_resolvercallback_ResolveIdentifiers_visit_out,
            &rinfo
        );
        if (!result || rinfo.hadoutofmemory) {
            result_AddMessage(
                &unresolved_ast->resultmsg,
                H64MSG_ERROR, "out of memory",
                unresolved_ast->fileuri,
                -1, -1
            );
            return 0;
        }
        k++;
    }
    {   // Copy over new messages resulting from resolution stage:
        k = msgcount;
        while (k < unresolved_ast->resultmsg.message_count) {
            if (!result_AddMessage(
                    pr->resultmsg,
                    unresolved_ast->resultmsg.message[k].type,
                    unresolved_ast->resultmsg.message[k].message,
                    unresolved_ast->resultmsg.message[k].fileuri,
                    unresolved_ast->resultmsg.message[k].line,
                    unresolved_ast->resultmsg.message[k].column
                    )) {
                return 0;
            }
            if (unresolved_ast->resultmsg.message[k].type == H64MSG_ERROR)
                pr->resultmsg->success = 0;
            k++;
        }
    }
    return 1;
}
