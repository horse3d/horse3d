
#include <string.h>

#include "compiler/ast.h"
#include "compiler/scope.h"
#include "hash.h"


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

}
