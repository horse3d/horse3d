
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <math.h>
#include <mathc/mathc.h>
#include <SDL2/SDL.h>

#include "datetime.h"
#include "scriptcore.h"
#include "scriptcoretime.h"

static double _timefloat() {
    uint64_t now = datetime_Ticks();
    uint64_t seconds = (now / 1000ULL);
    uint64_t msfraction = (now % 1000ULL);
    return (double)seconds + ((double)msfraction / 1000.0);
}

static int _time_getticks(lua_State *l) {
    lua_pushnumber(l, _timefloat());
    return 1;
}

static int _time_sleepms(lua_State *l) {
    if (lua_gettop(l) < 1 ||
            lua_type(l, 1) != LUA_TNUMBER) {
        lua_pushstring(l, "expected arg of type number");
        return lua_error(l);
    }
    datetime_Sleep(lua_tonumber(l, 1));
    return 0;
}

static int _time_schedulefunc(lua_State *l) {
    if (lua_gettop(l) < 2 ||
            lua_type(l, 1) != LUA_TNUMBER ||
            lua_type(l, 2) != LUA_TFUNCTION) {
        lua_pushstring(l, "expected args of types number, function");
        return lua_error(l);
    }
    if (lua_tonumber(l, 1) < 0) {
        lua_pushstring(l, "scheduled waiting time must be zero or greater");
        return lua_error(l);
    }
    lua_pushstring(l, "scheduledfuncslist");
    lua_gettable(l, LUA_REGISTRYINDEX);
    if (lua_type(l, -1) != LUA_TTABLE) {
        lua_pushstring(l, "scheduledfuncslist");
        lua_newtable(l);
        lua_settable(l, LUA_REGISTRYINDEX);
        lua_pushstring(l, "scheduledfuncslist");
        lua_gettable(l, LUA_REGISTRYINDEX);
        if (lua_type(l, -1) != LUA_TTABLE) {
            lua_pushstring(l, "failed to create scheduling table");
            return lua_error(l);
        }
    }
    int entries = lua_rawlen(l, -1);
    lua_pushnumber(l, entries + 1);
    lua_newtable(l);
    lua_pushnumber(l, 1);
    lua_pushnumber(l, _timefloat() + lua_tonumber(l, 1));
    lua_settable(l, -3);
    lua_pushnumber(l, 2);
    lua_pushvalue(l, 2);
    lua_settable(l, -3);
    lua_settable(l, -3);
    return 0;
}

int _time_getscheduledfuncstable(lua_State *l) {
    lua_pushstring(l, "scheduledfuncslist");
    lua_gettable(l, LUA_REGISTRYINDEX);
    if (lua_type(l, -1) != LUA_TTABLE) {
        lua_pop(l, 1);
        lua_newtable(l);
        return 1;
    }
    return 1;
}

void scriptcoretime_AddFunctions(lua_State *l) {
    lua_pushcfunction(l, _time_getticks);
    lua_setglobal(l, "_time_getticks");
    lua_pushcfunction(l, _time_sleepms);
    lua_setglobal(l, "_time_sleepms");
    lua_pushcfunction(l, _time_getscheduledfuncstable);
    lua_setglobal(l, "_time_getscheduledfuncstable");
    lua_pushcfunction(l, _time_schedulefunc);
    lua_setglobal(l, "_time_schedulefunc");
}
