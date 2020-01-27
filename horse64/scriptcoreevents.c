
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <SDL2/SDL.h>
#include <string.h>

#include "render3d.h"

static int _script_last_seen_w = -1;
static int _script_last_seen_h = -1;
static int windowhasfocus = 1;
static int relativemousemove = 1;
static int relativemousemovetemporarilydisabled = 0;


static void _keyboardev_to_char(SDL_Keycode sym, char *result) {
    char buf[15];
    memset(buf, 0, sizeof(buf));

    if (sym >= SDLK_0 && sym <= SDLK_9) {
        buf[0] = '0' + (sym - SDLK_0);
    } else if (sym >= SDLK_a && sym <= SDLK_z) {
        buf[0] = 'a' + (sym - SDLK_a);
    } else if (sym == SDLK_F1) {
        memcpy(buf, "f1", strlen("f1") + 1);
    } else if (sym == SDLK_F2) {
        memcpy(buf, "f2", strlen("f2") + 1);
    } else if (sym == SDLK_F3) {
        memcpy(buf, "f3", strlen("f3") + 1);
    } else if (sym == SDLK_F4) {
        memcpy(buf, "f4", strlen("f4") + 1);
    } else if (sym == SDLK_F5) {
        memcpy(buf, "f5", strlen("f5") + 1);
    } else if (sym == SDLK_F6) {
        memcpy(buf, "f6", strlen("f6") + 1);
    } else if (sym == SDLK_F7) {
        memcpy(buf, "f7", strlen("f7") + 1);
    } else if (sym == SDLK_F8) {
        memcpy(buf, "f8", strlen("f8") + 1);
    } else if (sym == SDLK_F9) {
        memcpy(buf, "f9", strlen("f9") + 1);
    } else if (sym == SDLK_F10) {
        memcpy(buf, "f10", strlen("f10") + 1);
    } else if (sym == SDLK_F11) {
        memcpy(buf, "f11", strlen("f11") + 1);
    } else if (sym == SDLK_F12) {
        memcpy(buf, "f12", strlen("f12") + 1);
    } else if (sym == SDLK_UP) {
        memcpy(buf, "up", strlen("up") + 1);
    } else if (sym == SDLK_DOWN) {
        memcpy(buf, "down", strlen("down") + 1);
    } else if (sym == SDLK_LEFT) {
        memcpy(buf, "left", strlen("left") + 1);
    } else if (sym == SDLK_RIGHT) {
        memcpy(buf, "right", strlen("right") + 1);
    } else if (sym == SDLK_SPACE) {
        memcpy(buf, "space", strlen("space") + 1);
    } else if (sym == SDLK_RSHIFT) {
        memcpy(buf, "rightshift", strlen("rightshift") + 1);
    } else if (sym == SDLK_LSHIFT) {
        memcpy(buf, "leftshift", strlen("leftshift") + 1);
    } else if (sym == SDLK_RCTRL) {
        memcpy(buf, "rightcontrol", strlen("rightcontrol") + 1);
    } else if (sym == SDLK_LCTRL) {
        memcpy(buf, "leftcontrol", strlen("leftcontrol") + 1);
    } else if (sym == SDLK_ESCAPE || sym == SDLK_AC_BACK) {
        memcpy(buf, "escape", strlen("escape") + 1);
    } else if (sym == SDLK_BACKSPACE) {
        memcpy(buf, "backspace", strlen("backspace") + 1);
    } else if (sym == SDLK_QUOTE) {
        memcpy(buf, "'", strlen("'") + 1);
    } else if (sym == SDLK_COMMA) {
        memcpy(buf, ",", strlen(",") + 1);
    } else if (sym == SDLK_PERIOD) {
        memcpy(buf, ".", strlen(".") + 1);
    } else if (sym == SDLK_PAGEDOWN) {
        memcpy(buf, "pagedown", strlen("pagedown") + 1);
    } else if (sym == SDLK_PAGEUP) {
        memcpy(buf, "pageup", strlen("pageup") + 1);
    } else if (sym == SDLK_RALT) {
        memcpy(buf, "rightalt", strlen("rightalt") + 1);
    } else if (sym == SDLK_LALT) {
        memcpy(buf, "leftalt", strlen("leftalt") + 1);
    } else if (sym == SDLK_TAB) {
        memcpy(buf, "tab", strlen("tab") + 1);
    } else if (sym == SDLK_PAUSE) {
        memcpy(buf, "pause", strlen("pause") + 1);
    }
    memcpy(result, buf, strlen(buf) + 1);
}


static void _updatemousemode() {
    if (relativemousemove && windowhasfocus &&
            !relativemousemovetemporarilydisabled &&
            !SDL_GetRelativeMouseMode()) {
        SDL_SetRelativeMouseMode(1);
    } else if ((!relativemousemove || !windowhasfocus ||
            relativemousemovetemporarilydisabled) &&
            SDL_GetRelativeMouseMode()) {
        SDL_SetRelativeMouseMode(0);
    }
}


int scriptcoreevents_Process(lua_State *l) {
    if (!l)
        return 0;

    lua_newtable(l);
    int tbl_index = lua_gettop(l);

    int count = 0;

    int ww = 1;
    int wh = 1;
    SDL_GetWindowSize(
        render3d_GetOutputWindow(), &ww, &wh
    );
    if (ww < 1)
        ww = 1;
    if (wh < 1)
        wh = 1;

    if (_script_last_seen_w < 0 ||
            _script_last_seen_w != ww ||
            _script_last_seen_h != wh) {
        lua_newtable(l);
        lua_pushstring(l, "type");
        lua_pushstring(l, "window_resize");
        lua_settable(l, -3);
        lua_pushstring(l, "width");
        lua_pushnumber(l, ww);
        lua_settable(l, -3);
        lua_pushstring(l, "height");
        lua_pushnumber(l, wh);
        lua_settable(l, -3);
        lua_rawseti(l, tbl_index, count + 1);
        count++;
        _script_last_seen_w = ww;
        _script_last_seen_h = wh;
    }

    _updatemousemode();

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        _updatemousemode();
        if (e.type == SDL_QUIT) {
            lua_newtable(l);
            lua_pushstring(l, "type");
            lua_pushstring(l, "quit");
            lua_settable(l, -3);
            lua_rawseti(l, tbl_index, count + 1);
            count++;
        } else if (e.type == SDL_KEYDOWN) {
            char key[15] = "";
            _keyboardev_to_char(e.key.keysym.sym, key);
            if (strlen(key) > 0) {
                lua_newtable(l);
                lua_pushstring(l, "type");
                lua_pushstring(l, "keydown");
                lua_settable(l, -3);
                lua_pushstring(l, "key");
                lua_pushstring(l, key);
                lua_settable(l, -3);
                lua_rawseti(l, tbl_index, count + 1);
                count++;
            }
        } else if (e.type == SDL_KEYUP) {
            char key[15] = "";
            _keyboardev_to_char(e.key.keysym.sym, key);
            if (strlen(key) > 0) {
                lua_newtable(l);
                lua_pushstring(l, "type");
                lua_pushstring(l, "keyup");
                lua_settable(l, -3);
                lua_pushstring(l, "key");
                lua_pushstring(l, key);
                lua_settable(l, -3);
                lua_rawseti(l, tbl_index, count + 1);
                count++;
            }
        } else if (e.type == SDL_MOUSEMOTION) {
            if (relativemousemove &&
                    (!windowhasfocus ||
                     relativemousemovetemporarilydisabled))
                continue;
            lua_newtable(l);
            lua_pushstring(l, "type");
            lua_pushstring(l, "mousemove");
            lua_settable(l, -3);
            if (!relativemousemove) {
                lua_pushstring(l, "position");
                lua_newtable(l);
                lua_pushnumber(l, 1);
                lua_pushnumber(l, (double)e.motion.x);
                lua_settable(l, -3);
                lua_pushnumber(l, 2);
                lua_pushnumber(l, (double)e.motion.y);
                lua_settable(l, -3);
                lua_settable(l, -3);
            } else {
                lua_pushstring(l, "movement");
                lua_newtable(l);
                lua_pushnumber(l, 1);
                lua_pushnumber(
                    l, (double)e.motion.xrel / (double)ww
                );
                lua_settable(l, -3);
                lua_pushnumber(l, 2);
                lua_pushnumber(
                    l, (double)e.motion.yrel / (double)wh
                );
                lua_settable(l, -3);
                lua_settable(l, -3);
            }
            lua_rawseti(l, tbl_index, count + 1);
            count++;
        } else if (e.type == SDL_MOUSEBUTTONDOWN) {
            if (relativemousemove &&
                    (!windowhasfocus ||
                     relativemousemovetemporarilydisabled)) {
                if (windowhasfocus && relativemousemovetemporarilydisabled)
                    relativemousemovetemporarilydisabled = 0;
                continue;
            }
            lua_newtable(l);
            lua_pushstring(l, "type");
            lua_pushstring(l, "mousedown");
            lua_settable(l, -3);
            lua_pushstring(l, "button");
            lua_pushnumber(l, (double)e.button.button);
            lua_settable(l, -3);
            if (!relativemousemove) {
                lua_pushstring(l, "position");
                lua_newtable(l);
                lua_pushnumber(l, 1);
                lua_pushnumber(l, (double)e.button.x);
                lua_settable(l, -3);
                lua_pushnumber(l, 2);
                lua_pushnumber(l, (double)e.button.y);
                lua_settable(l, -3);
                lua_settable(l, -3);
            }
            lua_rawseti(l, tbl_index, count + 1);
            count++;
        } else if (e.type == SDL_WINDOWEVENT) {
            if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                render3d_UpdateViewport();
                if (_script_last_seen_w != (int)e.window.data1 ||
                        _script_last_seen_h != (int)e.window.data2) {
                    lua_newtable(l);
                    lua_pushstring(l, "type");
                    lua_pushstring(l, "window_resize");
                    lua_settable(l, -3);
                    lua_pushstring(l, "width");
                    lua_pushnumber(l, (int)e.window.data1);
                    lua_settable(l, -3);
                    lua_pushstring(l, "height");
                    lua_pushnumber(l, (int)e.window.data2);
                    lua_settable(l, -3);
                    lua_rawseti(l, tbl_index, count + 1);
                    count++;
                    _script_last_seen_w = (int)e.window.data1;
                    _script_last_seen_h = (int)e.window.data2;
                }
            } else if (e.window.event == SDL_WINDOWEVENT_MINIMIZED) {
                windowhasfocus = 0;
                relativemousemovetemporarilydisabled = 1;
            } else if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                windowhasfocus = 0;
                relativemousemovetemporarilydisabled = 1;
            } else if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                windowhasfocus = 1;
            }
        }
    }
    _updatemousemode();
    return 1;
}
