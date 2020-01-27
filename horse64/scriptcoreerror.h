#ifndef HORSE3D_SCRIPTCOREERROR_H_
#define HORSE3D_SCRIPTCOREERROR_H_

#include <lua.h>
#include <stdlib.h>
#include <string.h>


static int lua_pushtracebackfunc(lua_State *l) {
    lua_pushstring(l, "debugtableref");
    lua_gettable(l, LUA_REGISTRYINDEX);
    if (lua_type(l, -1) != LUA_TTABLE) {
        lua_pop(l, -1);
        lua_pushnil(l);
        return 0;
    }
    lua_pushstring(l, "traceback");
    lua_gettable(l, -2);
    lua_remove(l, -2);
    return 0;
}

#endif  // HORSE3D_SCRIPTCOREERROR_H_
