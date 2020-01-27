
#include <lua.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "json.h"
#include "scriptcore.h"
#include "scriptcorejson.h"

int _json_decode(lua_State *l) {
    if (lua_gettop(l) < 1 || lua_type(l, 1) != LUA_TSTRING) {
        lua_pushstring(l, "expected arg #1 to be string");
        return lua_error(l);
    }
    jsonvalue *v = json_Parse(lua_tostring(l, 1));
    if (!v) {
        lua_pushstring(l, "json parse failed");
        return lua_error(l);
    }
    if (!json_PushDecodedValueToLuaStack(l, v)) {
        json_Free(v);
        lua_pushstring(l, "failed to push value to stack, "
            "out of memory?");
        return lua_error(l);
    }
    json_Free(v);
    return 1;
}

int _json_encode(lua_State *l) {
    if (lua_gettop(l) < 1) {
        lua_pushstring(l, "expected arg #1 to be value to be encoded");
        return lua_error(l);
    }
    char *error = NULL;
    if (!json_PushEncodedStrToLuaStack(l, 1, &error)) {
        if (error) {
            lua_pushstring(l, error);
            free(error);
        } else {
            lua_pushstring(l, "failed to encode, unknown error - "
                "out of memory?");
        }
        return lua_error(l);
    }
    return 1;
}

void scriptcorejson_AddFunctions(lua_State *l) {
    lua_pushcfunction(l, _json_decode);
    lua_setglobal(l, "_json_decode");
    lua_pushcfunction(l, _json_encode);
    lua_setglobal(l, "_json_encode");
}
