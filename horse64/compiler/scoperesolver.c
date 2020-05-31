
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "bytecode.h"
#include "compiler/ast.h"
#include "compiler/astparser.h"
#include "compiler/compileproject.h"
#include "compiler/globallimits.h"
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
    int hadoutofmemory, failedstorageassign;
} resolveinfo;

static int identifierisbuiltin(
        h64program *program,
        const char *identifier,
        storageref *storageref
        ) {
    assert(program->symbols != NULL);
    h64modulesymbols *msymbols = h64debugsymbols_GetBuiltinModule(
        program->symbols
    );
    assert(msymbols != NULL);

    uint64_t number = 0;
    if (hash_StringMapGet(
            msymbols->func_name_to_entry,
            identifier, &number
            )) {
        if (storageref) {
            storageref->type = H64STORETYPE_GLOBALFUNCSLOT;
            storageref->id = number;
        }
        return 1;
    }
    assert(number == 0);
    if (hash_StringMapGet(
            msymbols->class_name_to_entry,
            identifier, &number
            )) {
        if (storageref) {
            storageref->type = H64STORETYPE_GLOBALCLASSSLOT;
            storageref->id = number;
        }
        return 1;
    }
    assert(number == 0);
    if (hash_StringMapGet(
            msymbols->globalvar_name_to_entry,
            identifier, &number
            )) {
        if (storageref) {
            storageref->type = H64STORETYPE_GLOBALVARSLOT;
            storageref->id = number;
        }
        return 1;
    }
    return 0;
}

const char *_shortenedname(
    char *buf, const char *name
);

static int scoperesolver_ComputeItemStorage(
        h64expression *expr, h64program *program, h64ast *ast,
        int *outofmemory
        ) {
    h64scope *scope = ast_GetScope(expr, &ast->scope);
    assert(scope != NULL);
    if (scope->is_global) {
        const char *name = NULL;
        if (expr->type == H64EXPRTYPE_VARDEF_STMT) {
            name = expr->vardef.identifier;
            int64_t global_id = -1;
            if ((global_id = h64program_AddGlobalvar(
                    program, name,
                    expr->vardef.is_const,
                    ast->fileuri,
                    ast->module_path, ast->library_name
                    )) < 0) {
                if (outofmemory) *outofmemory = 1;
                return 0;
            }
            assert(global_id >= 0);
            expr->storage.set = 1;
            expr->storage.ref.type = H64STORETYPE_GLOBALVARSLOT;
            expr->storage.ref.id = global_id;
        } else if (expr->type == H64EXPRTYPE_CLASSDEF_STMT) {
            name = expr->classdef.name;
            int64_t global_id = -1;
            if ((global_id = h64program_AddClass(
                    program, name, ast->fileuri,
                    ast->module_path, ast->library_name
                    )) < 0) {
                if (outofmemory) *outofmemory = 1;
                return 0;
            }
            assert(global_id >= 0);
            expr->storage.set = 1;
            expr->storage.ref.type = H64STORETYPE_GLOBALCLASSSLOT;
            expr->storage.ref.id = global_id;
        } else if (expr->type == H64EXPRTYPE_FUNCDEF_STMT) {
            name = expr->funcdef.name;
            char **kwarg_names = malloc(
                sizeof(*kwarg_names) * expr->funcdef.arguments.arg_count
            );
            if (!kwarg_names) {
                if (outofmemory) *outofmemory = 1;
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
                        if (outofmemory) *outofmemory = 1;
                        return 0;
                    }
                }
                i++;
            }
            int64_t global_id = -1;
            if ((global_id = h64program_RegisterHorse64Function(
                    program, name, ast->fileuri,
                    expr->funcdef.arguments.arg_count,
                    kwarg_names,
                    expr->funcdef.arguments.last_posarg_is_multiarg,
                    ast->module_path,
                    ast->library_name,
                    -1
                    )) < 0) {
                int k = 0;
                while (k < i) {
                    free(kwarg_names[k]);
                    k++;
                }
                free(kwarg_names);
                if (outofmemory) *outofmemory = 1;
                return 0;
            }
            int k = 0;
            while (k < i) {
                free(kwarg_names[k]);
                k++;
            }
            free(kwarg_names);
            assert(global_id >= 0);
            assert(global_id >= 0);
            expr->storage.set = 1;
            expr->storage.ref.type = H64STORETYPE_GLOBALFUNCSLOT;
            expr->storage.ref.id = global_id;
        }
    }
    return 1;
}

int _resolvercallback_BuildGlobalStorage_visit_out(
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
        if (scope->is_global &&
                expr->storage.set == 0) {
            int outofmemory = 0;
            if (!scoperesolver_ComputeItemStorage(
                    expr, rinfo->pr->program, rinfo->ast, &outofmemory)) {
                if (outofmemory) {
                    rinfo->hadoutofmemory = 1;
                    return 0;
                }
                char buf[256];
                snprintf(buf, sizeof(buf) - 1,
                    "internal error: failed to compute storage for "
                    "expr: %s",
                    ast_ExpressionTypeToStr(expr->type)
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
            }
        }
    }
    return 1;
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


int _resolvercallback_AssignNonglobalStorage_visit_out(
        h64expression *expr, h64expression *parent, void *ud
        ) {
    resolveinfo *rinfo = (resolveinfo *)ud;
    return 1;
}

int _resolvercallback_ResolveIdentifiers_visit_out(
        h64expression *expr, h64expression *parent, void *ud
        ) {
    resolveinfo *rinfo = (resolveinfo *)ud;
    // Resolve most inner identifiers:
    if (expr->type == H64EXPRTYPE_IDENTIFIERREF &&
            (parent == NULL ||
             parent->type != H64EXPRTYPE_BINARYOP ||
             parent->op.value1 == expr)) {
        assert(expr->identifierref.value != NULL);
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
            expr->identifierref.resolved_to_expr = def->declarationexpr;
            if (def->declarationexpr->type ==
                    H64EXPRTYPE_VARDEF_STMT ||
                    (def->declarationexpr->type ==
                     H64EXPRTYPE_FUNCDEF_STMT &&
                     strcmp(def->declarationexpr->funcdef.name,
                            expr->identifierref.value)) ||
                    def->declarationexpr->type ==
                    H64EXPRTYPE_CLASSDEF_STMT) {
                def->everused = 1;
                if ((def->first_use_token_index < 0 ||
                        def->first_use_token_index < expr->tokenindex) &&
                        expr->tokenindex >= 0)
                    def->first_use_token_index = expr->tokenindex;
                if ((def->last_use_token_index < 0 ||
                        def->last_use_token_index > expr->tokenindex) &&
                        expr->tokenindex >= 0)
                    def->last_use_token_index = expr->tokenindex;
                if (isinsideclosure(expr))
                    def->closureuse = 1;

                if (def->declarationexpr->storage.set) {
                    memcpy(
                        &expr->storage, &def->declarationexpr->storage,
                        sizeof(expr->storage)
                    );
                } else if (def->scope->is_global) {
                    // Global storage should have been determined already,
                    // so this shouldn't happen. Log as error:
                    rinfo->failedstorageassign = 1;
                }
            } else if (def->declarationexpr->type ==
                    H64EXPRTYPE_IMPORT_STMT) {
                // We are accessing an import. We need to actually get the
                // full import path accessed to get the right statement:
                int accessed_elements_count = 1;
                int accessed_elements_alloc = H64LIMIT_IMPORTCHAINLEN + 1;
                char **accessed_elements = alloca(
                    sizeof(*accessed_elements) * (
                    accessed_elements_alloc)
                );
                accessed_elements[0] = expr->identifierref.value;
                h64expression *pexpr = expr;
                while (pexpr->parent &&
                        pexpr->parent->type == H64EXPRTYPE_BINARYOP &&
                        pexpr->parent->op.optype == H64OP_MEMBERBYIDENTIFIER &&
                        pexpr->parent->op.value1 == pexpr &&
                        pexpr->parent->op.value2 != NULL &&
                        pexpr->parent->op.value2->type ==
                            H64EXPRTYPE_IDENTIFIERREF &&
                        pexpr->parent->parent &&
                        pexpr->parent->parent->op.optype ==
                            H64OP_MEMBERBYIDENTIFIER &&
                        pexpr->parent->parent->op.value1 == pexpr->parent &&
                        pexpr->parent->parent->op.value2 != NULL &&
                        pexpr->parent->parent->op.value2->type ==
                            H64EXPRTYPE_IDENTIFIERREF) {
                    pexpr = pexpr->parent;
                    accessed_elements[accessed_elements_count] =
                        pexpr->op.value2->identifierref.value;
                    accessed_elements_count++;
                    if (accessed_elements_count >= accessed_elements_alloc) {
                        char buf[256];
                        snprintf(buf, sizeof(buf) - 1,
                            "unexpected import chain "
                            "exceeding maximum nesting of %d",
                            (int)H64LIMIT_IMPORTCHAINLEN
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

                // See what exact import statement that maps to:
                h64expression *_mapto = NULL;
                int j = -1;
                while (j < def->additionaldecl_count) {
                    h64expression *val = def->declarationexpr;
                    if (j >= 0)
                        val = def->additionaldecl[j];
                    if (val->type == H64EXPRTYPE_IMPORT_STMT &&
                            val->importstmt.import_elements_count ==
                            accessed_elements_count) {
                        int mismatch = 0;
                        int k = 0;
                        while (k < accessed_elements_count) {
                            if (strcmp(
                                    accessed_elements[k],
                                    val->importstmt.import_elements[k]
                                    ) != 0) {
                                mismatch = 1;
                                break;
                            }
                            k++;
                        }
                        if (!mismatch) _mapto = val;
                    }
                    j++;
                }

                // Put together path for error messages:
                size_t full_imp_path_len = 0;
                char *full_imp_path = NULL;
                {
                    full_imp_path_len = 0;
                    int k = 0;
                    while (k < accessed_elements_count) {
                        if (k > 0)
                            full_imp_path_len++;
                        full_imp_path_len += strlen(accessed_elements[k]);
                        k++;
                    }
                    full_imp_path = malloc(full_imp_path_len + 1);
                    if (!full_imp_path) {
                        rinfo->hadoutofmemory = 1;
                        return 0;
                    }
                    full_imp_path[0] = '\0';
                    k = 0;
                    while (k < accessed_elements_count) {
                        if (k > 0) {
                            full_imp_path[strlen(full_imp_path) + 1] = '\0';
                            full_imp_path[strlen(full_imp_path) + 1] = '.';
                        }
                        memcpy(
                            full_imp_path + strlen(full_imp_path),
                            accessed_elements[k],
                            strlen(accessed_elements[k]) + 1
                        );
                        k++;
                    }
                }

                // Abort if we found no exact match:
                if (_mapto == NULL) {
                    char buf[256];
                    snprintf(buf, sizeof(buf) - 1,
                        "unexpected reference to module path %s, "
                        "not found among this file's imports",
                        full_imp_path
                    );
                    free(full_imp_path);
                    full_imp_path = NULL;
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
                expr->identifierref.resolved_to_expr = _mapto;

                // Resolve the member access that is done on top of this
                // module path (or error if  there is none):
                assert(_mapto != NULL && pexpr != NULL);
                if (pexpr->parent == NULL ||
                        pexpr->parent->type != H64EXPRTYPE_BINARYOP ||
                        pexpr->parent->op.optype !=
                            H64OP_MEMBERBYIDENTIFIER ||
                        pexpr->parent->op.value1 != pexpr ||
                        pexpr->parent->op.value2 == NULL ||
                        pexpr->parent->op.value2->type !=
                            H64EXPRTYPE_IDENTIFIERREF) {
                    char buf[256];
                    snprintf(buf, sizeof(buf) - 1,
                        "unexpected %s of module %s, "
                        "instead of accessing any element from "
                        "the module via \".\"",
                        (pexpr->parent == NULL ? "standalone use" : (
                         pexpr->parent->type != H64EXPRTYPE_BINARYOP ?
                         ast_ExpressionTypeToStr(pexpr->parent->type) :
                         operator_OpTypeToStr(pexpr->parent->op.optype))),
                        full_imp_path
                    );
                    free(full_imp_path);
                    full_imp_path = NULL;
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
                const char *refitemname = (
                    pexpr->parent->op.value2->identifierref.value
                );
                if (!refitemname) {
                    // This should have yielded an error already in a
                    // previous stage, but make sure we spit one out if that
                    // wasn't the case:
                    rinfo->failedstorageassign = 1;
                    free(full_imp_path);
                    full_imp_path = NULL;
                    return 1;
                } else {
                    assert(_mapto->type == H64EXPRTYPE_IMPORT_STMT &&
                        _mapto->importstmt.referenced_ast != NULL &&
                        _mapto->importstmt.referenced_ast->scope.
                                name_to_declaration_map != NULL);
                    uint64_t number = 0;
                    if (!hash_StringMapGet(
                            _mapto->importstmt.referenced_ast->scope.
                            name_to_declaration_map,
                            refitemname, &number) || number == 0) {
                        char buf[256];
                        snprintf(buf, sizeof(buf) - 1,
                            "unexpected unknown identifier %s "
                            "not found in module %s",
                            refitemname,
                            full_imp_path
                        );
                        free(full_imp_path);
                        full_imp_path = NULL;
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
                    h64scopedef *targetitem = (
                        (h64scopedef*)(uintptr_t)number
                    );
                    assert(targetitem != NULL &&
                        targetitem->declarationexpr != NULL);
                    if (targetitem->declarationexpr->storage.set) {
                        memcpy(
                            &expr->storage.ref,
                            &targetitem->declarationexpr->storage.ref,
                            sizeof(expr->storage.ref)
                        );
                        expr->storage.set = 1;
                    } else {
                        assert(!expr->storage.set);
                    }
                    free(full_imp_path);
                    full_imp_path = NULL;
                }
                def->everused = 1;
                if ((def->first_use_token_index < 0 ||
                        def->first_use_token_index < expr->tokenindex) &&
                        expr->tokenindex >= 0)
                    def->first_use_token_index = expr->tokenindex;
                if ((def->last_use_token_index < 0 ||
                        def->last_use_token_index > expr->tokenindex) &&
                        expr->tokenindex >= 0)
                    def->last_use_token_index = expr->tokenindex;
                if (isinsideclosure(expr))
                    def->closureuse = 1;
            } else {
                char buf[256];
                snprintf(buf, sizeof(buf) - 1,
                    "internal error: identifier ref '%s' points "
                    "to unhandled expr type %d at line %" PRId64 ", "
                    "column %" PRId64,
                    expr->identifierref.value,
                    (int)def->declarationexpr->type,
                    def->declarationexpr->line,
                    def->declarationexpr->column
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
        } else if (identifierisbuiltin(
                rinfo->pr->program, expr->identifierref.value,
                &expr->storage.ref)) {
            expr->identifierref.resolved_to_builtin = 1;
            assert(expr->storage.ref.type != 0);
            expr->storage.set = 1;
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

int scoperesolver_BuildASTGlobalStorage(
        h64compileproject *pr, h64ast *unresolved_ast, int recursive
        ) {
    if (unresolved_ast->global_storage_built)
        return 1;

    // Set module path if missing:
    assert(unresolved_ast->fileuri != NULL);
    if (!unresolved_ast->module_path) {
        char *library_source = NULL;
        int pathoom = 0;
        char *project_path = compileproject_GetFileSubProjectPath(
            pr, unresolved_ast->fileuri, &library_source, &pathoom
        );
        if (!project_path) {
            assert(library_source == NULL);
            if (!pathoom) {
                char buf[256];
                snprintf(buf, sizeof(buf) - 1,
                    "unexpected failure to locate file's project base: "
                    "%s", unresolved_ast->fileuri);
                if (!result_AddMessage(
                        &unresolved_ast->resultmsg,
                        H64MSG_ERROR, buf,
                        unresolved_ast->fileuri,
                        -1, -1
                    ))
                    return 0;  // Out of memory
                return 1;  // regular abort with failure
            }
            return 0;
        }
        int modpathoom = 0;
        char *module_path = compileproject_URIRelPath(
            project_path, unresolved_ast->fileuri, &modpathoom
        );
        free(project_path);
        project_path = NULL;
        if (!module_path) {
            free(library_source);
            if (!modpathoom) {
                char buf[256];
                snprintf(buf, sizeof(buf) - 1,
                    "failed to locate this file path inside project: "
                    "%s", unresolved_ast->fileuri);
                result_AddMessage(
                    &unresolved_ast->resultmsg,
                    H64MSG_ERROR, buf,
                    unresolved_ast->fileuri,
                    -1, -1
                );
            }
            return 0;
        }

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
            free(library_source);
            return 0;
        }
        module_path = new_module_path;

        // If path has dots, then abort with error:
        unsigned int i = 0;
        while (i < strlen(module_path)) {
            if (module_path[i] == '.') {
                free(library_source);
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
        unresolved_ast->library_name = library_source;
        free(module_path);
        if (!unresolved_ast->module_path) {
            free(unresolved_ast->library_name);
            unresolved_ast->library_name = NULL;
            return 0;
        }
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

    // Build global storage:
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
            &_resolvercallback_BuildGlobalStorage_visit_out,
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
    {   // Copy over new messages resulting from storage assign:
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
            if (unresolved_ast->resultmsg.message[k].type == H64MSG_ERROR) {
                pr->resultmsg->success = 0;
                unresolved_ast->resultmsg.success = 0;
            }
            k++;
        }
    }
    if (rinfo.failedstorageassign) {
        // Make sure an error is returned:
        int haderrormsg = 0;
        k = 0;
        while (k < pr->resultmsg->message_count) {
            if (pr->resultmsg->message[k].type == H64MSG_ERROR)
                haderrormsg = 1;
            k++;
        }
        if (!haderrormsg) {
            result_AddMessage(
                pr->resultmsg,
                H64MSG_ERROR, "internal error: failed to assign "
                "storage to all items (global storage phase)",
                unresolved_ast->fileuri,
                -1, -1
            );
            pr->resultmsg->success = 0;
        }
    }

    // Do recursive handling if asked for:
    if (recursive) {
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
            if (expr->importstmt.referenced_ast != NULL) {
                if (!scoperesolver_BuildASTGlobalStorage(
                        pr, expr->importstmt.referenced_ast, 0
                        )) {
                    return 0;
                }
            }
            i++;
        }
    }

    unresolved_ast->global_storage_built = 1;
    return 1;
}

int scoperesolver_ResolveAST(
        h64compileproject *pr, h64ast *unresolved_ast
        ) {
    assert(unresolved_ast != NULL);
    if (unresolved_ast->identifiers_resolved)
        return 1;

    // Make sure global storage was assigned on this AST and all
    // referenced ones:
    if (!scoperesolver_BuildASTGlobalStorage(
            pr, unresolved_ast, 0
            )) {
        return 0;
    }

    // Resolve identifiers:
    resolveinfo rinfo;
    memset(&rinfo, 0, sizeof(rinfo));
    rinfo.pr = pr;
    rinfo.ast = unresolved_ast;
    int msgcount = unresolved_ast->resultmsg.message_count;
    int k = 0;
    while (k < unresolved_ast->stmt_count) {
        assert(unresolved_ast->stmt != NULL &&
            unresolved_ast->stmt[k] != NULL);
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
            if (unresolved_ast->resultmsg.message[k].type == H64MSG_ERROR) {
                pr->resultmsg->success = 0;
                unresolved_ast->resultmsg.success = 0;
            }
            k++;
        }
    }
    if (rinfo.failedstorageassign) {
        // Make sure an error is returned:
        int haderrormsg = 0;
        k = 0;
        while (k < pr->resultmsg->message_count) {
            if (pr->resultmsg->message[k].type == H64MSG_ERROR)
                haderrormsg = 1;
            k++;
        }
        if (!haderrormsg) {
            result_AddMessage(
                pr->resultmsg,
                H64MSG_ERROR, "internal error: failed to get"
                "storage for all items (identifier resolution stage)",
                unresolved_ast->fileuri,
                -1, -1
            );
            pr->resultmsg->success = 0;
        }
    }
    unresolved_ast->identifiers_resolved = 1;
    return 1;
}
