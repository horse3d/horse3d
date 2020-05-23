
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "compiler/ast.h"
#include "compiler/scope.h"
#include "hash.h"
#include "json.h"


int scope_Init(h64scope *scope, char hashkey[16]) {
    memcpy(scope->hashkey, hashkey,
           sizeof(*hashkey) * 16);
    scope->magicinitnum = SCOPEMAGICINITNUM;

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
    int i = 0;
    while (i < scope->definitionref_count) {
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
    h64scopedef *def = scope_QueryItem(scope, identifier_ref, 0);
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
        #ifndef NDEBUG
        if (scope->parentscope)
            assert(scope->parentscope->magicinitnum == SCOPEMAGICINITNUM);
        #endif
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
