
#include "compiler/ast.h"
#include "compiler/astparser.h"
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
        i++;
    }
    return 1;
}
