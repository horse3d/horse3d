
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
    if (scope->definitionref)
        free(scope->definitionref);
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
                scope->definitionref[i].declarationexpr->type)
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
