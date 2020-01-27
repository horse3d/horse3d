
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <math.h>
#include <mathc/mathc.h>
#include <string.h>

#include "math3d.h"
#include "scriptcore.h"
#include "scriptcoreerror.h"
#include "scriptcoremath.h"

static int _math_normalize(lua_State *l) {
    if (lua_gettop(l) != 3 ||
            lua_type(l, 1) != LUA_TNUMBER ||
            lua_type(l, 2) != LUA_TNUMBER ||
            lua_type(l, 3) != LUA_TNUMBER) {
        lua_pushstring(l, "expected 3 args of type number");
        return lua_error(l);
    }

    double v[3];
    v[0] = lua_tonumber(l, 1);
    v[1] = lua_tonumber(l, 2);
    v[2] = lua_tonumber(l, 3);

    lua_newtable(l);
    lua_pushnumber(l, 1);
    lua_pushnumber(l, v[0]);
    lua_settable(l, -3);
    lua_pushnumber(l, 2);
    lua_pushnumber(l, v[1]);
    lua_settable(l, -3);
    lua_pushnumber(l, 3);
    lua_pushnumber(l, v[2]);
    lua_settable(l, -3);
    return 1;
}


static int _math_quat_rotatevec(lua_State *l) {
    if (lua_gettop(l) != 2 ||
            lua_type(l, 1) != LUA_TTABLE ||
            lua_type(l, 2) != LUA_TTABLE) {
        lua_pushstring(l, "expected 2 args of type table");
        return lua_error(l);
    }

    double v[3];
    lua_pushnumber(l, 1);
    lua_gettable(l, 2);
    v[0] = lua_tonumber(l, -1);
    lua_pushnumber(l, 2);
    lua_gettable(l, 2);
    v[1] = lua_tonumber(l, -1);
    lua_pushnumber(l, 3);
    lua_gettable(l, 2);
    v[2] = lua_tonumber(l, -1);

    double quat[4];
    lua_pushnumber(l, 1);
    lua_gettable(l, 1);
    quat[0] = lua_tonumber(l, -1);
    lua_pushnumber(l, 2);
    lua_gettable(l, 1);
    quat[1] = lua_tonumber(l, -1);
    lua_pushnumber(l, 3);
    lua_gettable(l, 1);
    quat[2] = lua_tonumber(l, -1);
    lua_pushnumber(l, 4);
    lua_gettable(l, 1);
    quat[3] = lua_tonumber(l, -1);

    vec3_rotate_quat(v, v, quat);

    lua_newtable(l);
    lua_pushnumber(l, 1);
    lua_pushnumber(l, v[0]);
    lua_settable(l, -3);
    lua_pushnumber(l, 2);
    lua_pushnumber(l, v[1]);
    lua_settable(l, -3);
    lua_pushnumber(l, 3);
    lua_pushnumber(l, v[2]);
    lua_settable(l, -3);
    return 1;
}


static int _math_quat_fromeuler(lua_State *l) {
    if (lua_gettop(l) != 3 ||
            lua_type(l, 1) != LUA_TNUMBER ||
            lua_type(l, 2) != LUA_TNUMBER ||
            lua_type(l, 3) != LUA_TNUMBER) {
        lua_pushstring(l, "expected 3 args of type number");
        return lua_error(l);
    }

    double angles[3];
    angles[0] = lua_tonumber(l, 1) * M_PI / 180.0;
    angles[1] = lua_tonumber(l, 2) * M_PI / 180.0;
    angles[2] = lua_tonumber(l, 3) * M_PI / 180.0;

    double quat[4];
    quat_from_euler(quat, angles);

    lua_newtable(l);
    lua_pushnumber(l, 1);
    lua_pushnumber(l, quat[0]);
    lua_settable(l, -3);
    lua_pushnumber(l, 2);
    lua_pushnumber(l, quat[1]);
    lua_settable(l, -3);
    lua_pushnumber(l, 3);
    lua_pushnumber(l, quat[2]);
    lua_settable(l, -3);
    lua_pushnumber(l, 4);
    lua_pushnumber(l, quat[3]);
    lua_settable(l, -3);
    return 1;
}


static int _math_round(lua_State *l) {
    if (lua_gettop(l) < 1 || lua_type(l, 1) != LUA_TNUMBER) {
        lua_pushstring(l, "expected 1 arg of  type number");
        return lua_error(l);
    }
    lua_pushnumber(l, round(lua_tonumber(l, 1)));
    return 1;
}


void scriptcoremath_AddFunctions(lua_State *l) {
    lua_pushcfunction(l, _math_round);
    lua_setglobal(l, "_math_round");
    lua_pushcfunction(l, _math_quat_fromeuler);
    lua_setglobal(l, "_math_quat_fromeuler");
    lua_pushcfunction(l, _math_quat_rotatevec);
    lua_setglobal(l, "_math_quat_rotatevec");
    lua_pushcfunction(l, _math_normalize);
    lua_setglobal(l, "_math_normalize");
}
