
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bytecode.h"
#include "compiler/ast.h"
#include "compiler/astparser.h"
#include "compiler/compileproject.h"
#include "compiler/scoperesolver.h"
#include "compiler/scope.h"
#include "filesys.h"
#include "hash.h"

typedef struct resolveinfo {
    h64compileproject *pr;
    char *libraryname;
    char *modulepath;
    h64ast *ast;
    int isbuiltinmodule;
    int hadoutofmemory;
} resolveinfo;

static int identifierisbuiltin(
        h64compileproject *pr,
        const char *identifier,
        int isbuiltinmodule  // whether to allow access to secret built-ins
        ) {
    assert(pr->program->symbols != NULL);
    h64modulesymbols *msymbols = h64debugsymbols_GetBuiltinModule(
        pr->program->symbols
    );
    assert(msymbols != NULL);

    uint64_t number = 0;
    if (hash_StringMapGet(
            msymbols->func_name_to_func_id,
            identifier, &number
            )) {
        int func_id = (int)number;
        assert(func_id >= 0 && func_id < msymbols->func_count);
        if (msymbols->func_symbols[func_id].
                modulepath == NULL &&
                msymbols->func_symbols[func_id].
                libraryname == NULL) {
            return 1;
        }
        return 0;
    }
    assert(number == 0);
    if (hash_StringMapGet(
            msymbols->class_name_to_class_id,
            identifier, &number
            )) {
        int cls_id = (int)number;
        assert(cls_id >= 0 && cls_id < msymbols->classes_count);
        if (msymbols->classes_symbols[cls_id].
                modulepath == NULL &&
                msymbols->classes_symbols[cls_id].
                libraryname == NULL) {
            return 1;
        }
        return 0;
    }
    return 0;
}

const char *_shortenedname(
    char *buf, const char *name
);

int _resolvercallback_ResolveIdentifiersBuildSymbolLookup_visit_out(
        h64expression *expr, h64expression *parent, void *ud
        ) {
    resolveinfo *rinfo = (resolveinfo *)ud;
    // Add file-global items to the project-global item lookups:
    if (expr->type == H64EXPRTYPE_VARDEF_STMT ||
            expr->type == H64EXPRTYPE_CLASSDEF_STMT ||
            expr->type == H64EXPRTYPE_FUNCDEF_STMT) {
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
        if (scope->is_global) {
            const char *name = NULL;
            if (expr->type == H64EXPRTYPE_VARDEF_STMT) {
                name = expr->vardef.identifier;
                // FIXME: set globals to make them importable
            } else if (expr->type == H64EXPRTYPE_CLASSDEF_STMT) {
                name = expr->classdef.name;
                if (!h64program_AddClass(
                        rinfo->pr->program, name, rinfo->ast->fileuri,
                        rinfo->modulepath, rinfo->libraryname
                        )) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
            } else if (expr->type == H64EXPRTYPE_FUNCDEF_STMT) {
                name = expr->funcdef.name;
                char **kwarg_names = malloc(
                    sizeof(*kwarg_names) * expr->funcdef.arguments.arg_count
                );
                if (!kwarg_names) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
                memset(kwarg_names, 0, sizeof(*kwarg_names) *
                       expr->funcdef.arguments.arg_count);
                int i = 0;
                while (i < expr->funcdef.arguments.arg_count) {
                    if (expr->funcdef.arguments.arg_value[i]) {
                        kwarg_names[i] = strdup(
                            expr->funcdef.arguments.arg_name[i]
                        );
                        if (!kwarg_names[i]) {
                            int k = 0;
                            while (k < i) {
                                free(kwarg_names[k]);
                                k++;
                            }
                            free(kwarg_names);
                            rinfo->hadoutofmemory = 1;
                            return 0;
                        }
                    }
                    i++;
                }
                if (!h64program_RegisterHorse64Function(
                        rinfo->pr->program, name, rinfo->ast->fileuri,
                        expr->funcdef.arguments.arg_count,
                        kwarg_names,
                        expr->funcdef.arguments.last_posarg_is_multiarg,
                        rinfo->modulepath,
                        rinfo->libraryname,
                        -1
                        )) {
                    int k = 0;
                    while (k < i) {
                        free(kwarg_names[k]);
                        k++;
                    }
                    free(kwarg_names);
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
                int k = 0;
                while (k < i) {
                    free(kwarg_names[k]);
                    k++;
                }
                free(kwarg_names);
            }
        }
    }

    // Resolve identifiers if we can:
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
                rinfo->pr,
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
    // Set module path if missing:
    assert(unresolved_ast->fileuri != NULL);
    if (!unresolved_ast->module_path) {
        char *module_path = compileproject_ToProjectRelPath(
            pr, unresolved_ast->fileuri
        );
        if (!module_path)
            return 0;

        // Strip away file extension and normalize:
        if (strlen(module_path) > strlen(".h64") && (
                memcmp(module_path + strlen(module_path) -
                       strlen(".h64"), ".h64", strlen(".h64")) == 0 ||
                memcmp(module_path + strlen(module_path) -
                       strlen(".h64"), ".h64", strlen(".h64")) == 0)) {
            module_path[strlen(module_path) - strlen(".h64")] = '\0';
        }
        char *new_module_path = filesys_Normalize(module_path);
        free(module_path);
        if (!new_module_path) {
            return 0;
        }
        module_path = new_module_path;

        // If path has dots, then abort with error:
        unsigned int i = 0;
        while (i < strlen(module_path)) {
            if (module_path[i] == '.') {
                char buf[256];
                snprintf(buf, sizeof(buf) - 1,
                    "cannot integrate module with dots in file path: "
                    "%s", module_path);
                free(module_path);
                if (!result_AddMessage(
                        &unresolved_ast->resultmsg,
                        H64MSG_ERROR, buf,
                        unresolved_ast->fileuri,
                        -1, -1
                    ))
                    return 0;  // Out of memory
                return 1;  // regular abort with failure
            }
            i++;
        }

        // Replace file separators with dots:
        i = 0;
        while (i < strlen(module_path)) {
            if (module_path[i] == '/'
                    #if defined(_WIN32) || defined(_WIN64)
                    || module_path[i] == '\\'
                    #endif
                    ) {
                module_path[i] = '.';
            }
            i++;
        }

        unresolved_ast->module_path = strdup(module_path);
        free(module_path);
        if (!unresolved_ast->module_path)
            return 0;
    }

    // First, make sure all imports are loaded up:
    int i = 0;
    while (i < unresolved_ast->scope.definitionref_count) {
        assert(unresolved_ast->scope.definitionref[i] != NULL);
        h64expression *expr = (
            unresolved_ast->scope.definitionref[i]->declarationexpr
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

    // Now, actually resolve identifiers and build project-wide lookups:
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
            &_resolvercallback_ResolveIdentifiersBuildSymbolLookup_visit_out,
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
