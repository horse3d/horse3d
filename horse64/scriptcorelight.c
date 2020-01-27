
#include <inttypes.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <math.h>
#include <mathc/mathc.h>
#include <stdint.h>
#include <string.h>

#include "lights.h"
#include "scriptcore.h"
#include "scriptcoreerror.h"
#include "scriptcorelight.h"


void lua_PushLightRef(lua_State *l, int64_t objid) {
    scriptobjref *ref = lua_newuserdata(l, sizeof(*ref));
    memset(ref, 0, sizeof(*ref));
    ref->magic = OBJREFMAGIC;
    ref->type = OBJREF_LIGHT;
    ref->value = objid;
    luaL_newmetatable(l, "horse3d.world.light");
    lua_setmetatable(l, -2);
    lua_pushstring(l, "horse3d_world_light_set_metatableref");
    lua_gettable(l, LUA_REGISTRYINDEX);
    if (lua_type(l, -1) != LUA_TFUNCTION) {
        lua_pop(l, 1);
    } else {
        lua_pushvalue(l, -2);
        lua_call(l, 1, 0);
    }
}

static int _lights_addnew(lua_State *l) {
    h3dlight *light = lights_New(NULL);
    if (!light) {
        lua_pushstring(l, "failed to add light");
        return lua_error(l);
    }
    lua_PushLightRef(l, lights_GetLightId(light));
    return 1;
}

static int _lights_object_byid(lua_State *l) {
    if (lua_gettop(l) < 1 ||
            lua_type(l, 1) != LUA_TNUMBER) {
        lua_pushstring(l, "expected arg of type number");
        return lua_error(l);
    }
    int64_t id = (int64_t)lua_tonumber(l, 1);
    h3dlight *light = lights_GetLightById(id);
    if (!light) {
        lua_pushnil(l);
        return 1;
    }
    lua_PushLightRef(l, id);
    return 1;
}

static int _lights_setlightposition(lua_State *l) {
    if (lua_gettop(l) != 2 ||
            lua_type(l, 1) != LUA_TUSERDATA ||
            lua_type(l, 2) != LUA_TTABLE) {
        lua_pushstring(l, "expected two args of types "
                       "horsed.world.light, table");
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_OBJ) {
        lua_pushstring(l, "expected arg #1 to be horse3d.world.light");
        return lua_error(l);
    }
    h3dlight *light = lights_GetLightById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!light) {
        lua_pushstring(
            l, "failed to get light - was it deleted?"
        );
        return lua_error(l);
    }

    double pos[3];
    lua_pushnumber(l, 1);
    lua_gettable(l, 2);
    pos[0] = lua_tonumber(l, -1);
    lua_pushnumber(l, 2);
    lua_gettable(l, 2);
    pos[1] = lua_tonumber(l, -1);
    lua_pushnumber(l, 3);
    lua_gettable(l, 2);
    pos[2] = lua_tonumber(l, -1);

    lights_MoveLight(light, pos);
    return 0;
}

void scriptcorelight_AddFunctions(lua_State *l) {
    lua_pushcfunction(l, _lights_object_byid);
    lua_setglobal(l, "_lights_object_byid");
    lua_pushcfunction(l, _lights_setlightposition);
    lua_setglobal(l, "_lights_setlightposition");
    lua_pushcfunction(l, _lights_addnew);
    lua_setglobal(l, "_lights_addnew");
}
