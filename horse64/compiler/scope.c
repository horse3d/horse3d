
#include <assert.h>
#include <string.h>

#include "compiler/ast.h"
#include "compiler/scope.h"
#include "hash.h"
#include "json.h"


int scope_Init(h64scope *scope, char hashkey[16]) {
    memcpy(scope->hashkey, hashkey,
           sizeof(*hashkey) * 16);

    if (!scope->name_to_declaration_map) {
        scope->name_to_declaration_map = hash_NewStringMap(32);
        if (!scope->name_to_declaration_map)
            return 0;
    }
    hashmap_SetFixedHashSecret(
        scope->name_to_declaration_map, (uint8_t*)hashkey
    );
    return 1;
}

void scope_FreeData(h64scope *scope) {
    if (!scope)
        return;

    if (scope->name_to_declaration_map)
        hash_FreeMap(scope->name_to_declaration_map);
    if (scope->definitionref) {
        int i = 0;
        while (i < scope->definitionref_count) {
            if (scope->definitionref[i].declarationexpr)
                free(scope->definitionref[i].declarationexpr);
            i++;
        }
        free(scope->definitionref);
    }
}

int scope_AddItem(
        h64scope *scope, const char *identifier_ref,
        h64expression *expr
        ) {
    // Try to add to existing entry:
    h64scopedef *def = scope_QueryItem(scope, identifier_ref, 0);
    if (def) {
        assert(strcmp(def->identifier, identifier_ref) == 0);
        h64expression **new_exprs = realloc(
            def->declarationexpr,
            sizeof(*new_exprs) * (def->declarationexpr_count + 1)
        );
        if (!new_exprs)
            return 0;
        def->declarationexpr = new_exprs;
        def->declarationexpr[def->declarationexpr_count] = expr;
        def->declarationexpr_count++;
        return 1;
    }

    // Add as new entry:
    if (scope->definitionref_count + 1 > scope->definitionref_alloc) {
        int new_alloc = scope->definitionref_alloc * 2;
        if (new_alloc < scope->definitionref_count + 8)
            new_alloc = scope->definitionref_count + 8;
        h64scopedef *new_refs = realloc(
            scope->definitionref, new_alloc * sizeof(*new_refs)
        );
        if (!new_refs)
            return 0;
        scope->definitionref_alloc = new_alloc;
        scope->definitionref = new_refs;
    }
    int i = scope->definitionref_count;
    assert(i < scope->definitionref_alloc);
    scope->definitionref_count++;
    memset(&scope->definitionref[i], 0, sizeof(*scope->definitionref));
    scope->definitionref[i].scope = scope;
    scope->definitionref[i].identifier = identifier_ref;
    scope->definitionref[i].declarationexpr = malloc(
        sizeof(*scope->definitionref[i].declarationexpr)
    );
    if (!scope->definitionref[i].declarationexpr) {
        scope->definitionref_count--;
        return 0;
    }
    scope->definitionref[i].declarationexpr_count = 1;
    scope->definitionref[i].declarationexpr[0] = expr;
    if (!hash_StringMapSet(
            scope->name_to_declaration_map, identifier_ref,
            (uintptr_t)&scope->definitionref[i])) {
        free(scope->definitionref[i].declarationexpr);
        scope->definitionref_count--;
        return 0;
    }
    return 1;
}

h64scopedef *scope_QueryItem(
        h64scope *scope, const char *identifier_ref, int bubble_up
        ) {
    uint64_t result = 0;
    assert(scope->name_to_declaration_map != NULL);
    if (!hash_StringMapGet(
            scope->name_to_declaration_map, identifier_ref, &result
            )) {
        if (bubble_up && scope->parentscope)
            return scope_QueryItem(scope->parentscope, identifier_ref, 1);
        return 0;
    }
    if (!result) {
        if (bubble_up && scope->parentscope)
            return scope_QueryItem(scope->parentscope, identifier_ref, 1);
        return 0;
    }
    return (h64scopedef*)(uintptr_t)result;
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
        jsonvalue *item = json_Dict();
        if (!json_SetDictStr(item, "identifier",
                scope->definitionref[i].identifier
                )) {
            fail = 1;
            json_Free(item);
            break;
        }
        if (!json_SetDictStr(item, "type",
                ast_ExpressionTypeToStr(
                scope->definitionref[i].declarationexpr[0]->type)
                )) {
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
