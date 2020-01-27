
#include <inttypes.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <math.h>
#include <mathc/mathc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif

#include "lights.h"
#include "render3d.h"
#include "scriptcore.h"
#include "scriptcoreerror.h"
#include "scriptcoregraphics.h"
#include "world.h"


static int _graphics_getfps(lua_State *l) {
    lua_pushnumber(l, render3d_fps);
    return 1;
}

static int _graphics_getframetimems(lua_State *l) {
    lua_pushnumber(l, render3d_framems);
    return 1;
}

static int _graphics_getmaxshownlights(lua_State *l) {
    lua_pushnumber(l, lights_GetMaxLights());
    return 1;
}

static int _graphics_disableoutput(lua_State *l) {
    int disabled = 1;
    if (lua_gettop(l) >= 1 &&
            lua_type(l, 1) == LUA_TBOOLEAN)
        disabled = lua_toboolean(l, 1);
    world_DisableDraw(disabled);
    return 0;
}

static int _graphics_exit(lua_State *l) {
    _exit(0);
    return 0;
}

static int _graphics_prompterror(lua_State *l) {
    const char *msg = "Unspecified error.";
    const char *title = "Horse3D ERROR";
    if (lua_gettop(l) >= 1 &&
            lua_type(l, 1) == LUA_TSTRING)
        msg = lua_tostring(l, 1);
    if (lua_gettop(l) >= 2 &&
            lua_type(l, 2) == LUA_TSTRING)
        title = lua_tostring(l, 2);
    #if defined(ANDROID) || defined(__ANDROID__)
    if (!iswindowed) {
        render3d_MinimizeWindow();
    }
    #endif
    #if defined(_WIN32) || defined(_WIN64)
    int msgboxID = MessageBox(
        NULL, msg, title, MB_OK|MB_ICONSTOP
    ); 
    #else
    SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_ERROR, title, msg,
        render3d_GetOutputWindow() 
    );
    #endif
    return 0;
}

void scriptcoregraphics_AddFunctions(lua_State *l) {
    lua_pushcfunction(l, _graphics_getfps);
    lua_setglobal(l, "_graphics_getfps");
    lua_pushcfunction(l, _graphics_getframetimems);
    lua_setglobal(l, "_graphics_getframetimems");
    lua_pushcfunction(l, _graphics_getmaxshownlights);
    lua_setglobal(l, "_graphics_getmaxshownlights");
    lua_pushcfunction(l, _graphics_disableoutput);
    lua_setglobal(l, "_graphics_disableoutput");
    lua_pushcfunction(l, _graphics_prompterror);
    lua_setglobal(l, "_graphics_prompterror");
    lua_pushcfunction(l, _graphics_exit);
    lua_setglobal(l, "_graphics_exit");
}
