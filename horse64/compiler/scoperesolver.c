
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "compiler/ast.h"
#include "compiler/astparser.h"
#include "compiler/compileproject.h"
#include "compiler/scoperesolver.h"

int scoperesolver_ResolveAST(
        h64compileproject *pr, h64ast *unresolved_ast
        ) {
    // First, make sure all imports are loaded up:
    int i = 0;
    while (i < unresolved_ast->scope.definitionref_count) {
        if (unresolved_ast->scope.definitionref[i].
                declarationexpr_count < 0) {
            i++;
            continue;
        }
        h64expression *expr = (
            unresolved_ast->scope.definitionref[i].declarationexpr[0]
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
            free(file_path);
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
        i++;
    }
    return 1;
}
