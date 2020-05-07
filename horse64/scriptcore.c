
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <math.h>
#include <mathc/mathc.h>
#include <SDL2/SDL.h>

#include "compiler/main.h"
#include "filesys.h"
#include "luamem.h"
#include "meshes.h"
#include "render3d.h"
#include "scriptcore.h"
#include "scriptcoreaudio.h"
#include "scriptcoreerror.h"
#include "scriptcoreevents.h"
#include "scriptcorefilesys.h"
#include "scriptcoregraphics.h"
#include "scriptcorejson.h"
#include "scriptcorelight.h"
#include "scriptcoremath.h"
#include "scriptcorescene.h"
#include "scriptcoretime.h"
#include "scriptcoreuiplane.h"
#include "scriptcoreworld.h"
#include "texture.h"
#include "vfs.h"

static lua_State *_mainstate = NULL;


static int _render3d_getiswindowed(lua_State *l) {
    lua_pushboolean(l, render3d_IsWindowed());
    return 1;
}

char *stringescape(const char *value) {
    if (!value)
        return NULL;
    char *escaped = malloc(3 + strlen(value) * 2);
    if (!escaped)
        return NULL;
    int len = 0;
    escaped[0] = '\0';
    int i = 0;
    while (i < strlen(value)) {
        uint8_t c = ((const uint8_t*)value)[i];
        if (c == '\r') {
            escaped[len] = '\\';
            escaped[len + 1] = '\r';
            i++;
            len += 2;
            continue;
        } else if (c == '\n') {
            escaped[len] = '\\';
            escaped[len + 1] = '\n';
            i++;
            len += 2;
            continue;
        } else if (c == '\\') {
            escaped[len] = '\\';
            escaped[len + 1] = '\\';
            i++;
            len += 2;
            continue;
        } else if (c == '"') {
            escaped[len] = '\\';
            escaped[len + 1] = '"';
            i++;
            len += 2;
            continue;
        }
        escaped[len] = c;
        i++; len++;
    }
    escaped[len] = '\0';
    return escaped;
}

char *_prefix__file__(
        const char *contents, const char *filepath
        ) {
    if (!contents)
        return NULL;
    char *escapedpath = stringescape(filepath);
    if (!escapedpath)
        return NULL;
    char prestr[] = "local __file__ = \"";
    char afterstr[] = "\"; ";
    char *buf = malloc(
        strlen(prestr) +
        strlen(escapedpath) + strlen(afterstr) +
        strlen(contents) + 1
    );
    if (!buf) {
        free(escapedpath);
        return NULL;
    }
    memcpy(buf, prestr, strlen(prestr));
    memcpy(buf + strlen(prestr), escapedpath, strlen(escapedpath));
    memcpy(buf + strlen(prestr) + strlen(escapedpath),
           afterstr, strlen(afterstr));
    memcpy(buf + strlen(prestr) + strlen(escapedpath) + strlen(afterstr),
           contents, strlen(contents) + 1);
    free(escapedpath);
    return buf;
}

static int _render3d_setmode(lua_State *l) {
    int n = lua_gettop(l);
    if (n != 1 || lua_type(l, 1) != LUA_TBOOLEAN) {
        lua_pushstring(l, "expected 1 arg of type bool");
        return lua_error(l);
    }
    int boolval = lua_toboolean(l, 1);
    render3d_SetMode(boolval);
    return 0;
}

static int _render3d_getevents(lua_State *l) {
    if (lua_gettop(l) != 0) {
        lua_pushstring(l, "expected 0 args");
        return lua_error(l);
    }
    int count = scriptcoreevents_Process(l);
    return count;
}

static int _render3d_endframe(lua_State *l) {
    render3d_EndFrame();
    return 0;
}

h3dtexture *_check_stackarg_as_tex(lua_State *l, int arg) {
    if ((arg > 0 && lua_gettop(l) < arg) ||
            lua_type(l, arg) != LUA_TUSERDATA)
        return 0;
    if (((scriptobjref*)lua_touserdata(l, arg))->magic !=
            OBJREFMAGIC)
        return 0;
    if (((scriptobjref*)lua_touserdata(l, arg))->type !=
            OBJREF_TEXTURE)
        return 0;
    return texture_GetById(
        ((scriptobjref*)lua_touserdata(l, arg))->value
    );
}

static int _render3d_loadnewtexture(lua_State *l) {
    if (lua_gettop(l) != 1 ||
            lua_type(l, 1) != LUA_TSTRING) {
        lua_pushstring(l, "expected arg #1 to be string");
        return lua_error(l);
    }
    h3dtexture *tex = texture_GetTexture(
        lua_tostring(l, 1)
    );
    if (!tex)
        return 0;

    scriptobjref *ref = lua_newuserdata(l, sizeof(*ref));
    memset(ref, 0, sizeof(*ref));
    ref->magic = OBJREFMAGIC;
    ref->type = OBJREF_TEXTURE;
    ref->value = tex->id;
    return 1;
}

static int _render3d_gettexturebyid(lua_State *l) {
    if (lua_gettop(l) != 1 ||
            lua_type(l, 1) != LUA_TNUMBER) {
        lua_pushstring(l, "expected arg #1 to be number");
        return lua_error(l);
    }
    int texrefid = lua_tonumber(l, 1);
    scriptobjref *ref = lua_newuserdata(l, sizeof(*ref));
    memset(ref, 0, sizeof(*ref));
    ref->magic = OBJREFMAGIC;
    ref->type = OBJREF_TEXTURE;
    ref->value = texrefid;
    return 1;
}

static int _render3d_rendertriangle3d(lua_State *l) {
    if (lua_gettop(l) != 19) {
        lua_pushstring(l, "expected 19 args");
        return lua_error(l);
    }
    h3dtexture *tex = _check_stackarg_as_tex(l, 1);
    if (!tex) {
        lua_pushstring(l, "expected arg #1 to be a valid image");
        return lua_error(l);
    }
    double posx1 = lua_tonumber(l, 2);
    double posy1 = lua_tonumber(l, 3);
    double posz1 = lua_tonumber(l, 4);
    double posx2 = lua_tonumber(l, 5);
    double posy2 = lua_tonumber(l, 6);
    double posz2 = lua_tonumber(l, 7);
    double posx3 = lua_tonumber(l, 8);
    double posy3 = lua_tonumber(l, 9);
    double posz3 = lua_tonumber(l, 10);
    double texx1 = lua_tonumber(l, 11);
    double texy1 = lua_tonumber(l, 12);
    double texx2 = lua_tonumber(l, 13);
    double texy2 = lua_tonumber(l, 14);
    double texx3 = lua_tonumber(l, 15);
    double texy3 = lua_tonumber(l, 16);
    int unlit = lua_toboolean(l, 17);
    int transparent = lua_toboolean(l, 18);
    int additive = lua_toboolean(l, 19);
    h3drendertexref ref;
    memset(&ref, 0, sizeof(ref));
    ref.tex = tex;
    render3d_RenderTriangle3D(
        &ref,
        posx1, posy1, posz1,
        posx2, posy2, posz2,
        posx3, posy3, posz3,
        texx1, texy1,
        texx2, texy2,
        texx3, texy3,
        unlit, transparent, additive, 1
    );
    return 0;
}

static int _render3d_rendertriangle2d(lua_State *l) {
    if (lua_gettop(l) != 13) {
        lua_pushstring(l, "expected 13 args");
        return lua_error(l);
    }
    h3dtexture *tex = _check_stackarg_as_tex(l, 1);
    if (!tex) {
        lua_pushstring(l, "expected arg #1 to be a valid image");
        return lua_error(l);
    }
    double posx1 = lua_tonumber(l, 2);
    double posy1 = lua_tonumber(l, 3);
    double posx2 = lua_tonumber(l, 4);
    double posy2 = lua_tonumber(l, 5);
    double posx3 = lua_tonumber(l, 6);
    double posy3 = lua_tonumber(l, 7);
    double texx1 = lua_tonumber(l, 8);
    double texy1 = lua_tonumber(l, 9);
    double texx2 = lua_tonumber(l, 10);
    double texy2 = lua_tonumber(l, 11);
    double texx3 = lua_tonumber(l, 12);
    double texy3 = lua_tonumber(l, 13);
    h3drendertexref ref;
    memset(&ref, 0, sizeof(ref));
    ref.tex = tex;
    render3d_RenderTriangle2D(
        &ref,
        posx1, posy1, posx2, posy2, posx3, posy3,
        texx1, texy1, texx2, texy2, texx3, texy3
    );
    return 0;
}

static int _camera_setposition(lua_State *l) {
    if (lua_gettop(l) != 3) {
        lua_pushstring(l, "expected 3 args");
        return lua_error(l);
    }
    render3d_projectionmatrix_cached = 0;
    double x = lua_tonumber(l, 1);
    double y = lua_tonumber(l, 2);
    double z = lua_tonumber(l, 3);
    maincam->pos[0] = x;
    maincam->pos[1] = y;
    maincam->pos[2] = z;
    return 0;
}

static int _camera_getposition(lua_State *l) {
    lua_newtable(l);
    lua_pushnumber(l, 1);
    lua_pushnumber(l, maincam->pos[0]);
    lua_rawset(l, -3);
    lua_pushnumber(l, 2);
    lua_pushnumber(l, maincam->pos[1]);
    lua_rawset(l, -3);
    lua_pushnumber(l, 3);
    lua_pushnumber(l, maincam->pos[2]);
    lua_rawset(l, -3);
    return 1;
}

static int _camera_getrotation(lua_State *l) {
    lua_newtable(l);
    lua_pushnumber(l, 1);
    lua_pushnumber(l, maincam->look_quat[0]);
    lua_settable(l, -3);
    lua_pushnumber(l, 2);
    lua_pushnumber(l, maincam->look_quat[1]);
    lua_settable(l, -3);
    lua_pushnumber(l, 3);
    lua_pushnumber(l, maincam->look_quat[2]);
    lua_settable(l, -3);
    lua_pushnumber(l, 4);
    lua_pushnumber(l, maincam->look_quat[3]);
    lua_settable(l, -3);
    return 1;
}

static int _camera_setrotation(lua_State *l) {
    if (lua_gettop(l) != 4 ||
            lua_type(l, 1) != LUA_TNUMBER ||
            lua_type(l, 2) != LUA_TNUMBER ||
            lua_type(l, 3) != LUA_TNUMBER ||
            lua_type(l, 4) != LUA_TNUMBER) {
        lua_pushstring(l, "expected 4 args as numbers");
        return lua_error(l);
    }

    maincam->look_quat[0] = lua_tonumber(l, 1);
    maincam->look_quat[1] = lua_tonumber(l, 2);
    maincam->look_quat[2] = lua_tonumber(l, 3);
    maincam->look_quat[3] = lua_tonumber(l, 4);
    return 0;
}

static int _camera_lookat(lua_State *l) {
    if (lua_gettop(l) != 3) {
        lua_pushstring(l, "expected 3 args");
        return lua_error(l);
    }
    double x = lua_tonumber(l, 1);
    double y = lua_tonumber(l, 2);
    double z = lua_tonumber(l, 3);
    return 0;
}

static int _lua_pcall(lua_State *l) {
    if (lua_gettop(l) < 1) {
        lua_pushstring(l, "bad argument #1 to 'pcall' (value expected)");
        return lua_error(l);
    }
    int args = lua_gettop(l) - 1;
    if (args < 0)
        args = 0;
    if (!lua_checkstack(l, 10)) {
        if (lua_checkstack(l, 1))
            lua_pushstring(l, "out of memory");
        return lua_error(l);
    }

    int errorhandlerindex = lua_gettop(l) - (args);
    lua_pushtracebackfunc(l);
    lua_insert(l, errorhandlerindex);

    int oldtop = lua_gettop(l);
    int result = lua_pcall(l, args, LUA_MULTRET, errorhandlerindex);
    oldtop -= args + 1;
    if (result) {
        lua_pushboolean(l, false);
        if (result != LUA_ERRMEM) {
            const char *s = lua_tostring(l, -2);
            if (s)
                lua_pushstring(l, s);
            else
                lua_pushstring(l, "<internal error: no error message>");
            return 2;
        }
        return 1;
    } else {
        int returned_values = lua_gettop(l) - oldtop;
        lua_pushboolean(l, true);
        if (returned_values > 0)
            lua_insert(l, lua_gettop(l) - returned_values);
        return returned_values + 1;
    }
}

int scriptcore_Run(int argc, const char **argv) {
    int doubledash_seen = 0;
    const char *action = NULL;
    int action_offset = -1;
    const char *action_file = NULL;
    int i = 0;
    while (i < argc) {
        if (strcmp(argv[i], "--") == 0) {
            doubledash_seen = 1;
            i++;
            continue;
        }
        if (!doubledash_seen) {
            if (strcasecmp(argv[i], "-h") == 0 ||
                    strcasecmp(argv[i], "--help") == 0 ||
                    strcasecmp(argv[i], "-?") == 0 ||
                    strcasecmp(argv[i], "/?") == 0) {
                printf("Usage: horsecc [action] "
                       "[...options + arguments...]\n");
                printf("\n");
                printf("Available actions:\n");
                printf("  - \"compile\"           Compile .horse code "
                       "and output binary.\n");
                printf("  - \"get_ast\"           Get AST of code\n");
                printf("  - \"get_resolved_ast\"  "
                       "Get AST of code with resolved identifiers\n");
                printf("  - \"get_tokens\"        Get Tokenization of code\n");
                printf("  - \"run\"               Compile .horse code, and "
                       "run it at once in-place.\n"); 
                return 0;
            }
            if (!action && (strcmp(argv[i], "compile") == 0 ||
                    strcmp(argv[i], "get_ast") == 0 ||
                    strcmp(argv[i], "get_resolved_ast") == 0 ||
                    strcmp(argv[i], "get_tokens") == 0 ||
                    strcmp(argv[i], "run") == 0)) {
                action = argv[i];
                action_offset = i + 1;
                break;
            }
        }
        i++;
    }
    if (!action) {
        fprintf(stderr, "horsecc: error: need action, "
            "like horsecc run. See horsecc --help\n");
        return 1;
    }

    if (strcmp(action, "compile") == 0) {
        return compiler_command_Compile(argv, argc, action_offset);
    } else if (strcmp(action, "get_ast") == 0) {
        return compiler_command_GetAST(argv, argc, action_offset);
    } else if (strcmp(action, "get_resolved_ast") == 0) {
        return compiler_command_GetResolvedAST(argv, argc, action_offset);
    } else if (strcmp(action, "get_tokens") == 0) {
        return compiler_command_GetTokens(argv, argc, action_offset);
    } else if (strcmp(action, "run") == 0) {
        return compiler_command_Run(argv, argc, action_offset);
    } else {
        return 1;
    }

    /*
    lua_State *l = luamem_NewMemManagedState();
    _mainstate = l;

    luaL_openlibs(l);

    char *executable_path = filesys_GetOwnExecutable();
    if (!executable_path) {
        fprintf(stderr, "Path alloc failed!\n");
        lua_close(l);
        return 1;
    }
    char *executable_dir = filesys_ParentdirOfItem(executable_path);
    free(executable_path);
    if (!executable_dir) {
        fprintf(stderr, "Path alloc failed!\n");
        lua_close(l);
        return 1;
    }
    char *data_path = filesys_GetRealPath(executable_dir);
    free(executable_dir);
    if (!data_path) {
        fprintf(stderr, "Path alloc failed!\n");
        lua_close(l);
        return 1;
    }

    char *script_path = strdup(action_file);
    free(data_path);
    if (script_path) {
        char *_s = filesys_Normalize(script_path);
        free(script_path);
        script_path = _s;
    }
    if (!script_path) {
        fprintf(stderr, "Path alloc failed!\n");
        lua_close(l);
        return 1;
    }

    const char *_loaderror_nofile = "no main.horse found";
    const char *loaderror = "generic I/O failure";
    vfs_EnableCurrentDirectoryDiskAccess(1);
    if (strcmp(action, "run") == 0) {
        int result = compiler_command_CompileAndRun(script_path);
        if (script_path)
            free(script_path);
        lua_close(l);
        #if !defined(ANDROID) && !defined(__ANDROID__)
        render3d_CloseWindow();
        #endif
        #if defined(ANDROID) || defined(__ANDROID__)
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR,
            "Horse3D missing main.horse file",
            buf, NULL
        );
        #endif
        return (result ? 0 : 1);
    } else if (strcmp(action, "tokenize") == 0) {
        int result = compiler_command_Tokenize(script_path);
        if (script_path)
            free(script_path);
        lua_close(l);
        #if !defined(ANDROID) && !defined(__ANDROID__)
        render3d_CloseWindow();
        #endif
        return (result ? 0 : 1);
    } else {
        fprintf(stderr, "horsecc: error: internal error, "
            "missing code path for action %s\n", action);
        lua_close(l);
        #if !defined(ANDROID) && !defined(__ANDROID__)
        render3d_CloseWindow();
        #endif
        return 1;
    }
    vfs_EnableCurrentDirectoryDiskAccess(1);
    if (script_path) {
        free(script_path);
        script_path = NULL;
    }
    int stacksize = lua_gettop(l);

    lua_pushcfunction(l, _render3d_setmode);
    lua_setglobal(l, "_render3d_setmode");
    lua_pushcfunction(l, _render3d_getevents);
    lua_setglobal(l, "_render3d_getevents");
    lua_pushcfunction(l, _render3d_endframe);
    lua_setglobal(l, "_render3d_endframe");
    lua_pushcfunction(l, _render3d_loadnewtexture);
    lua_setglobal(l, "_render3d_loadnewtexture");
    lua_pushcfunction(l, _render3d_gettexturebyid);
    lua_setglobal(l, "_render3d_gettexturebyid");
    lua_pushcfunction(l, _render3d_rendertriangle3d);
    lua_setglobal(l, "_render3d_rendertriangle3d");
    lua_pushcfunction(l, _render3d_rendertriangle2d);
    lua_setglobal(l, "_render3d_rendertriangle2d");
    lua_pushcfunction(l, _camera_setposition);
    lua_setglobal(l, "_camera_setposition");
    lua_pushcfunction(l, _camera_setrotation);
    lua_setglobal(l, "_camera_setrotation");
    lua_pushcfunction(l, _camera_getrotation);
    lua_setglobal(l, "_camera_getrotation");
    lua_pushcfunction(l, _camera_getposition);
    lua_setglobal(l, "_camera_getposition");
    lua_pushcfunction(l, _render3d_getiswindowed);
    lua_setglobal(l, "_render3d_getiswindowed");
    lua_pushcfunction(l, _lua_pcall);
    lua_setglobal(l, "pcall");

    scriptcorejson_AddFunctions(l);
    scriptcoregraphics_AddFunctions(l);
    scriptcorescene_AddFunctions(l);
    scriptcoremath_AddFunctions(l);
    scriptcoreaudio_AddFunctions(l);
    scriptcoreworld_AddFunctions(l);
    scriptcorefilesys_AddFunctions(l);
    scriptcorelight_AddFunctions(l);
    scriptcoretime_AddFunctions(l);
    scriptcoreuiplane_AddFunctions(l);

    scriptcorefilesys_RegisterVFS(l);

    int prev_stack = lua_gettop(l);
    lua_getglobal(l, "debug");
    if (lua_type(l, -1) == LUA_TTABLE) {
        lua_pushstring(l, "debugtableref");
        lua_pushvalue(l, -2);
        lua_settable(l, LUA_REGISTRYINDEX);
    }
    lua_settop(l, prev_stack);

    lua_pushtracebackfunc(l);
    lua_insert(l, -2);
    int errorhandlerindex = lua_gettop(l) - 1;

    lua_newtable(l);
    i = 0;
    while (i < argc) {
        lua_pushnumber(l, i);
        lua_pushstring(l, argv[i]);
        lua_settable(l, -3);
        i++;
    }
    lua_setglobal(l, "arg");

    int result = lua_pcall(l, 0, 1, errorhandlerindex);
    if (result) {
        char buf[1024];
        snprintf(
            buf, sizeof(buf) - 1,
            "FATAL STARTUP SCRIPT CRASH: %s",
            lua_tostring(l, -1)
        );
        fprintf(stderr, "horse3d: error: %s\n", buf);
        lua_close(l);
        #if !defined(ANDROID) && !defined(__ANDROID__)
        render3d_CloseWindow();
        #endif
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR,
            "Horse3D FATAL STARTUP SCRIPT CRASH",
            buf, NULL
        );
        return 1;
    }

    int luaresult = 0;
    if (lua_type(l, -1) == LUA_TNUMBER) {
        luaresult = lua_tonumber(l, -1);
    }

    return luaresult;
    */
}

lua_State *scriptcore_MainState() {
    return _mainstate;
}
