// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "compiler/ast.h"
#include "compiler/scope.h"
#include "hash.h"
#include "json.h"
#include "nonlocale.h"


int scope_Init(h64scope *scope, h64expression *expr) {
    scope->magicinitnum = SCOPEMAGICINITNUM;

    if (!scope->name_to_declaration_map) {
        scope->name_to_declaration_map = hash_NewStringMap(32);
        if (!scope->name_to_declaration_map)
            return 0;
    }
    scope->expr = expr;
    return 1;
}

void scope_FreeData(h64scope *scope) {
    if (!scope)
        return;

    if (scope->name_to_declaration_map)
        hash_FreeMap(scope->name_to_declaration_map);
    int i = 0;
    while (i < scope->definitionref_count) {
        if (scope->definitionref[i]) {
            free(scope->definitionref[i]->additionaldecl);
        }
        free(scope->definitionref[i]);
        i++;
    }
    free(scope->definitionref);
    memset(scope, 0, sizeof(*scope));
}

void scope_RemoveItem(
        h64scope *scope, const char *identifier_ref
        ) {
    if (!scope || !identifier_ref)
        return;
    assert(scope->name_to_declaration_map != NULL);
    uint64_t value;
    if (!hash_StringMapGet(
            scope->name_to_declaration_map, identifier_ref,
            &value))
        return;
    if (value != 0) {
        assert(hash_StringMapUnset(
            scope->name_to_declaration_map, identifier_ref
        ) != 0);
        int i = 0;
        while (i < scope->definitionref_count) {
            if (strcmp(scope->definitionref[i]->identifier,
                    identifier_ref) == 0) {
                if (scope->definitionref[i]->declarationexpr) {
                    h64expression *e = (
                        scope->definitionref[i]->declarationexpr
                    );
                    if (!e->destroyed) {
                        if (e->type == H64EXPRTYPE_FUNCDEF_STMT ||
                                e->type == H64EXPRTYPE_INLINEFUNCDEF) {
                            if (e->funcdef.foundinscope == scope)
                                e->funcdef.foundinscope = NULL;
                        } else if (e->type == H64EXPRTYPE_VARDEF_STMT) {
                            if (e->vardef.foundinscope == scope)
                                e->vardef.foundinscope = NULL;
                        } else if (e->type == H64EXPRTYPE_CLASSDEF_STMT) {
                            if (e->classdef.foundinscope == scope)
                                e->classdef.foundinscope = NULL;
                        } else if (e->type == H64EXPRTYPE_IMPORT_STMT) {
                            if (e->importstmt.foundinscope == scope)
                                e->importstmt.foundinscope = NULL;
                        } else if (e->type == H64EXPRTYPE_FOR_STMT) {
                            // nothing to do
                        } else {
                            assert(0 && "this should be unreachable");
                        }
                    }
                }
                free(scope->definitionref[i]);
                if (i + 1 < scope->definitionref_count) {
                    memmove(
                        &scope->definitionref[i],
                        &scope->definitionref[i + 1],
                        sizeof(*scope->definitionref) * (
                        scope->definitionref_count - i - 1
                        )
                    );
                }
                scope->definitionref_count--;
                continue;
            }
            i++;
        }
    }
}

int scope_AddItem(
        h64scope *scope, const char *identifier_ref,
        h64expression *expr, int *outofmemory
        ) {
    // Try to add to existing entry:
    h64scopedef *def = scope_QueryItem(
        scope, identifier_ref, SCOPEQUERY_FLAG_QUERYCLASSITEMS
    );
    if (def) {
        if (outofmemory) *outofmemory = 0;
        return 0;
    }

    // Add as new entry:
    if (scope->definitionref_count + 1 > scope->definitionref_alloc) {
        int new_alloc = scope->definitionref_alloc * 2;
        if (new_alloc < scope->definitionref_count + 8)
            new_alloc = scope->definitionref_count + 8;
        h64scopedef **new_refs = realloc(
            scope->definitionref, new_alloc * sizeof(*new_refs)
        );
        if (!new_refs) {
            if (outofmemory) *outofmemory = 1;
            return 0;
        }
        scope->definitionref_alloc = new_alloc;
        scope->definitionref = new_refs;
    }
    int i = scope->definitionref_count;
    assert(i < scope->definitionref_alloc);
    scope->definitionref_count++;
    scope->definitionref[i] = malloc(sizeof(**scope->definitionref));
    if (!scope->definitionref[i]) {
        scope->definitionref_count--;
        if (outofmemory) *outofmemory = 1;
        return 0;
    }
    memset(scope->definitionref[i], 0, sizeof(**scope->definitionref));
    scope->definitionref[i]->scope = scope;
    scope->definitionref[i]->identifier = identifier_ref;
    scope->definitionref[i]->declarationexpr = expr;
    if (!hash_StringMapSet(
            scope->name_to_declaration_map, identifier_ref,
            (uintptr_t)scope->definitionref[i])) {
        free(scope->definitionref[i]);
        scope->definitionref_count--;
        if (outofmemory) *outofmemory = 1;
        return 0;
    }

    int addedtoself = (
        (expr->type == H64EXPRTYPE_FUNCDEF_STMT ||
         expr->type == H64EXPRTYPE_INLINEFUNCDEF) ?
        &expr->funcdef.scope == scope :
        ((expr->type == H64EXPRTYPE_FOR_STMT ?
          &expr->forstmt.scope == scope : (
          expr->type == H64EXPRTYPE_DO_STMT ?
          &expr->dostmt.rescuescope == scope :
          0)))
    );
    if (!addedtoself) {
        if (expr->type == H64EXPRTYPE_VARDEF_STMT) {
            expr->vardef.foundinscope = scope;
        } else if (expr->type == H64EXPRTYPE_CLASSDEF_STMT) {
            expr->classdef.foundinscope = scope;
        } else if (expr->type == H64EXPRTYPE_FUNCDEF_STMT) {
            expr->funcdef.foundinscope = scope;
        } else if (expr->type == H64EXPRTYPE_WITH_CLAUSE) {
            expr->withclause.foundinscope = scope;
        } else if (expr->type == H64EXPRTYPE_IMPORT_STMT) {
            expr->importstmt.foundinscope = scope;
        } else {
            h64fprintf(stderr, "horsec: warning: "
                "unexpected add to scope of expr type %d\n",
                expr->type);
            assert(0 && "abort for invalid item to add");
            // In release mode, avoid crashing:
            hash_StringMapUnset(
                scope->name_to_declaration_map, identifier_ref
            );
            free(scope->definitionref[i]);
            scope->definitionref_count--;
            if (outofmemory) *outofmemory = 0;
            return 0;
        }
    }
    return 1;
}

h64scopedef *scope_QueryItem(
        h64scope *scope, const char *identifier_ref,
        int flags
        ) {
    assert(identifier_ref != NULL);
    uint64_t result = 0;
    assert(scope->name_to_declaration_map != NULL);
    if (((flags & SCOPEQUERY_FLAG_QUERYCLASSITEMS) == 0 &&
            scope->expr && scope->expr->type == H64EXPRTYPE_CLASSDEF_STMT) ||
            !hash_StringMapGet(
            scope->name_to_declaration_map, identifier_ref, &result
            )) {
        #ifndef NDEBUG
        if (scope->parentscope)
            assert(scope->parentscope->magicinitnum == SCOPEMAGICINITNUM);
        #endif
        if ((flags & SCOPEQUERY_FLAG_BUBBLEUP) != 0 && scope->parentscope)
            return scope_QueryItem(
                scope->parentscope, identifier_ref, flags
            );
        return 0;
    }
    h64expression *expr = NULL;
    if (result)
        expr = ((h64scopedef*)(uintptr_t)result)->declarationexpr;
    if (!result || (result && expr && expr->destroyed)) {
        if (result && expr && expr->destroyed) {
            scope_RemoveItem(scope, identifier_ref);
        }
        if ((flags & SCOPEQUERY_FLAG_BUBBLEUP) != 0 && scope->parentscope)
            return scope_QueryItem(
                scope->parentscope, identifier_ref, flags
            );
        return 0;
    }
    return (h64scopedef*)(uintptr_t)result;
}

char *scope_ScopeToJSONStr(h64scope *scope) {
    jsonvalue *v = scope_ScopeToJSON(scope);
    if (v) {
        char *result = json_Dump(v);
        json_Free(v);
        return result;
    }
    json_Free(v);
    return NULL;
}

jsonvalue *scope_ScopeToJSON(
        h64scope *scope
        ) {
    jsonvalue *v = json_Dict();
    if (!v)
        return NULL;
    jsonvalue *itemslist = json_List();
    int fail = 0;
    int i = 0;
    while (i < scope->definitionref_count) {
        assert(scope->definitionref[i] != NULL);
        jsonvalue *item = json_Dict();
        if (!json_SetDictStr(item, "identifier",
                scope->definitionref[i]->identifier
                )) {
            fail = 1;
            json_Free(item);
            break;
        }
        if (!json_SetDictStr(item, "type",
                ast_ExpressionTypeToStr(
                scope->definitionref[i]->declarationexpr->type)
                )) {
            fail = 1;
            json_Free(item);
            break;
        }
        if (!json_SetDictBool(item, "everused",
                scope->definitionref[i]->everused)) {
            fail = 1;
            json_Free(item);
            break;
        }
        if (!json_SetDictBool(item, "closurebound",
                scope->definitionref[i]->closurebound)) {
            fail = 1;
            json_Free(item);
            break;
        }
        if (!json_AddToList(itemslist, item)) {
            fail = 1;
            json_Free(item);
            break;
        }
        i++;
    }
    if (!json_SetDict(v, "items", itemslist)) {
        fail = 1;
        json_Free(itemslist);
    }
    if (fail) {
        json_Free(v);
        return NULL;
    }
    return v;
}
