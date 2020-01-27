
#include <assert.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <math.h>
#include <mathc/mathc.h>
#include <stdlib.h>
#include <string.h>

#include "filesys.h"
#include "luamem.h"
#include "scriptcore.h"
#include "scriptcoreerror.h"
#include "scriptcorefilesys.h"
#include "vfs.h"

static int _lfs_currentdir(lua_State *l) {
    char *cd = filesys_GetCurrentDirectory();
    if (!cd) {
        lua_pushnil(l);
        lua_pushstring(l, "failed to get current directory");
        return 2;
    }
    if (!luamem_EnsureFreePools(l) ||
            !luamem_EnsureCanAllocSize(l, strlen(cd) + 32)) {
        free(cd);
        lua_pushstring(l, "out of memory");
        return lua_error(l);
    }
    lua_pushstring(l, cd);
    free(cd);
    return 1;
}

static int _lfs_basename(lua_State *l) {
    if (lua_gettop(l) < 1 || lua_type(l, 1) != LUA_TSTRING) {
        lua_pushstring(l, "expected arg of type string");
        return lua_error(l);
    }
    char *s = filesys_Basename(lua_tostring(l, 1));
    if (!s || !luamem_EnsureFreePools(l) ||
            !luamem_EnsureCanAllocSize(l, strlen(s) + 32)) {
        if (s) free(s);
        lua_pushstring(l, "out of memory");
        return lua_error(l);
    }
    lua_pushstring(l, s);
    return 1;
}


static int _lfs_dirname(lua_State *l) {
    if (lua_gettop(l) < 1 || lua_type(l, 1) != LUA_TSTRING) {
        lua_pushstring(l, "expected arg of type string");
        return lua_error(l);
    }
    const char *p = lua_tostring(l, 1);
    char *s = filesys_Dirname(lua_tostring(l, 1));
    if (!s || !luamem_EnsureFreePools(l) ||
            !luamem_EnsureCanAllocSize(l, strlen(s) + 32)) {
        if (s) free(s);
        lua_pushstring(l, "out of memory");
        return lua_error(l);
    }
    lua_pushstring(l, s);
    return 1;
}

static int _lfs_dir(lua_State *l) {
    int n = lua_gettop(l);
    if (n != 1 || lua_type(l, 1) != LUA_TSTRING) {
        lua_pushstring(l, "expected 1 arg of type string");
        return lua_error(l);
    }
    char **contents = NULL;
    if (!filesys_ListFolder(
            lua_tostring(l, 1), &contents, 0)) {
        lua_pushstring(l, "failed to list directory");
        return lua_error(l);
    }
    lua_newtable(l);
    int i = 0;
    while (contents[i]) {
        lua_pushnumber(l, i + 1);
        lua_pushstring(l, contents[i]);
        lua_rawset(l, -3);
        i++;
    }
    filesys_FreeFolderList(contents);
    return 1;
}


static int _lfs_getsize(lua_State *l) {
    int n = lua_gettop(l);
    if (n != 1 || lua_type(l, 1) != LUA_TSTRING) {
        lua_pushstring(l, "expected 1 arg of type string");
        return lua_error(l);
    }
    uint64_t s;
    if (!vfs_Size(lua_tostring(l, 1), &s)) {
        lua_pushnil(l);
        return 1;
    }
    lua_pushnumber(l, s);
    return 1;
}


static int _lfs_exists(lua_State *l) {
    int n = lua_gettop(l);
    if (n != 1 || lua_type(l, 1) != LUA_TSTRING) {
        lua_pushstring(l, "expected 1 arg of type string");
        return lua_error(l);
    }
    int result = 0;
    if (!vfs_Exists(lua_tostring(l, 1), &result)) {
        lua_pushstring(l, "exists failed - out of memory?");
        return lua_error(l);
    }
    lua_pushboolean(l, result);
    return 1;
}


static int _lfs_mkdir(lua_State *l) {
    int n = lua_gettop(l);
    if (n != 1 || lua_type(l, 1) != LUA_TSTRING) {
        lua_pushstring(l, "expected 1 arg of type string");
        return lua_error(l);
    }
    int result = filesys_CreateDirectory(lua_tostring(l, 1));
    if (!result) {
        lua_pushnil(l);
        lua_pushstring(l, "failed to create directory");
        return 2;
    }
    lua_pushboolean(l, 1);
    return 1;
}


static int _lfs_datadir(lua_State *l) {
    int n = lua_gettop(l);
    if (n != 0) {
        lua_pushstring(l, "expected 0 args");
        return lua_error(l);
    }
    char *exepath = filesys_GetOwnExecutable();
    if (!exepath) {
        lua_pushstring(l, "failed to get executable path");
        return lua_error(l);
    }
    char *exedir_possiblysymlink = filesys_ParentdirOfItem(exepath);
    free(exepath);
    if (!exedir_possiblysymlink) {
        lua_pushstring(l, "failed to get executable path");
        return lua_error(l);
    }
    char *exedir = filesys_GetRealPath(exedir_possiblysymlink);
    free(exedir_possiblysymlink);
    if (!exedir) {
        lua_pushstring(l, "failed to get executable path");
        return lua_error(l);
    }
    lua_pushstring(l, exedir);
    free(exedir);
    return 1;
}


static int _lfs_isdir(lua_State *l) {
    int n = lua_gettop(l);
    if (n != 1 || lua_type(l, 1) != LUA_TSTRING) {
        lua_pushstring(l, "expected 1 arg of type string");
        return lua_error(l);
    }
    int result = 0;
    if (!vfs_IsDirectory(lua_tostring(l, 1), &result)) {
        lua_pushstring(l, "failure in vfs_IsDirectory - out of memory?");
        return lua_error(l);
    }
    lua_pushboolean(l, result);
    return 1;
}

static int _vfs_adddatapak(lua_State *l) {
    if (lua_gettop(l) < 1 || lua_type(l, 1) != LUA_TSTRING) {
        lua_pushstring(l, "expected 1 arg of type string");
        return lua_error(l);
    }
    char *pack = strdup(lua_tostring(l, 1));
    if (!pack) {
        lua_pushstring(l, "alloc failure");
        return lua_error(l);
    }
    if (strlen(pack) < strlen(".h3dpak") ||
            memcmp(pack + strlen(pack) - strlen(".h3dpak"),
                   ".h3dpak", strlen(".h3dpak")) != 0) {
        free(pack);
        lua_pushstring(l, "only h3dpak archives supported");
        return lua_error(l);
    }
    if (!filesys_FileExists(pack) || filesys_IsDirectory(pack)) {
        free(pack);
        lua_pushstring(l, "given file path not found, or a directory");
        return lua_error(l);
    }
    if (!vfs_AddPak(pack)) {
        free(pack);
        lua_pushstring(l, "failed to load .h3dpak - wrong format?");
        return lua_error(l);
    }
    free(pack);
    return 0;
}

static int _vfs_package_require_module_loader(lua_State *l) {
    lua_settop(l, 0);
    lua_pushvalue(l, lua_upvalueindex(2));  // module name
    lua_pushvalue(l, lua_upvalueindex(1));  // lua source code
    char *contents = strdup(lua_tostring(l, -1));
    if (!contents) {
        lua_pushstring(l, "failed to get contents from stack");
        return lua_error(l);
    }
    char *abspath = vfs_AbsolutePath(lua_tostring(l, -1));
    if (!abspath || !luamem_EnsureFreePools(l)) {
        if (abspath)
            free(abspath);
        free(contents);
        lua_pushstring(l, "alloc fail");
        return lua_error(l);
    }
    char *newcontents = _prefix__file__(contents, abspath);
    free(abspath); free(contents);
    contents = newcontents;
    if (!contents) {
        lua_pushstring(l, "alloc fail");
        return lua_error(l);
    }
    int result = luaL_loadbuffer(
        l, contents, strlen(contents), lua_tostring(l, -2)
    );
    free(contents);
    if (result != 0) {
        lua_pushstring(l, "failed to parse module code");
        return lua_error(l);
    }
    if (lua_gettop(l) > 1) {
        lua_replace(l, 1);
    }
    lua_settop(l, 1);
    lua_call(l, 0, 1);
    return 1;
}

static int _vfs_package_require_searcher(lua_State *l) {
    if (lua_gettop(l) < 1 ||
            lua_type(l, 1) != LUA_TSTRING) {
        char buf[512];
        snprintf(
            buf, sizeof(buf) -1, 
            "expected #1 arg of type string, got #%d (#1 is type %d)",
            lua_gettop(l),
            (lua_gettop(l) >= 1 ? lua_type(l, 1) : -999)
        );
        lua_pushstring(l, buf);
        return lua_error(l);
    }

    // Get module path supplied to us:
    const char *modpath = lua_tostring(l, 1);
    char *path = malloc(strlen(modpath) + strlen(".lua") + 1);
    if (!path)
        return 0;
    int i = 0;
    while (i < strlen(modpath)) {
        if (modpath[i] == '.') {
            path[i] = '/';
        } else {
            path[i] = modpath[i];
        }
        i++;
    }
    memcpy(path + strlen(modpath), ".lua", strlen(".lua") + 1);

    // See if module exists and how large the file is:
    int result = 0;
    if (!vfs_Exists(path, &result)) {
        free(path);
        lua_pushstring(l, "unexpected failure in vfs_Exists");
        return lua_error(l);
    }
    if (!result) {
        char *s = vfs_NormalizePath(path);
        free(path);
        path = NULL;
        if (!s) {
            lua_pushstring(l, "alloc error");
            return lua_error(l);
        }
        if (s && strlen(s) < 2 || (s[0] != '.' && s[1] != '.' &&
                s[2] != '\0' && s[2] != '/'
                #if defined(_WIN32) || defined(_WIN64)
                && s[2] != '\\'
                #endif
                )) {
            char *s2 = filesys_Join("horse3d/scriptcore", s);
            free(s);
            s = NULL;
            if (!s2) {
                lua_pushstring(l, "alloc error");
                return lua_error(l);
            }
            path = s2;
            s2 = NULL;
        } else if (s) {
            free(s);
            s = NULL;
        }
        if (!path)
            return 0;
        if (!vfs_Exists(path, &result)) {
            free(path);
            lua_pushstring(l, "unexpected failure in vfs_Exists");
            return lua_error(l);
        }
        if (!result) {
            free(path);
            path = NULL;
            return 0;
        }
    }
    if (!vfs_IsDirectory(path, &result)) {
        free(path);
        lua_pushstring(l, "unexpected failure in vfs_IsDirectory");
        return lua_error(l);
    }
    if (result) {
        free(path);
        return 0;
    }
    uint64_t contentsize = 0;
    if (!vfs_Size(path, &contentsize)) {
        free(path);
        lua_pushstring(l, "unexpected failure in vfs_Size");
        return lua_error(l);
    }
    if (contentsize <= 0) {
        free(path);
        return 0;
    }

    // Read contents:
    char *contents = malloc(contentsize + 1);
    if (!contents) {
        free(path);
        return 0;
    }
    if (!vfs_GetBytes(path, 0, contentsize, contents)) {
        free(path);
        lua_pushstring(l, "unexpected failure in vfs_GetBytes");
        return lua_error(l);
    }
    contents[contentsize] = '\0';

    // Keep the cleaned up path:
    char *cleaned_path = vfs_NormalizePath(path);
    free(path);
    path = NULL;
    if (!cleaned_path) {
        free(contents);
        lua_pushstring(l, "path cleanup failure");
        return lua_error(l);
    }

    // Return loader function + name:
    lua_pushstring(l, contents);
    free(contents);
    lua_pushstring(l, cleaned_path);
    lua_pushcclosure(l, _vfs_package_require_module_loader, 2);
    lua_pushstring(l, cleaned_path);
    free(cleaned_path);
    assert(lua_type(l, -1) == LUA_TSTRING);
    assert(lua_type(l, -2) == LUA_TFUNCTION);
    return 2;
}

static int _vfs_lua_loadfile(lua_State *l) {
    if (lua_gettop(l) < 1 ||
            lua_type(l, 1) != LUA_TSTRING) {
        return 0;
    }
    lua_settop(l, 1);
    char *path = filesys_Normalize(lua_tostring(l, 1));
    if (!path) {
        lua_pushstring(l, "alloc failure");
        return lua_error(l);
    }
    int result;
    if (!vfs_Exists(path, &result)) {
        free(path);
        return 0;
    }
    if (!result) {
        free(path);
        return 0;
    }
    uint64_t contentsize = 0;
    if (!vfs_Size(path, &contentsize)) {
        free(path);
        lua_pushstring(l, "unexpected failure in vfs_Size");
        return lua_error(l);
    }
    if (contentsize <= 0) {
        free(path);
        return 0;
    }
    char *contents = malloc(contentsize + 1);
    if (!contents) {
        free(path);
        return 0;
    }
    if (!vfs_GetBytes(path, 0, contentsize, contents)) {
        free(path);
        lua_pushstring(l, "unexpected failure in vfs_GetBytes");
        return lua_error(l);
    }
    contents[contentsize] = '\0';
    char *abspath = vfs_AbsolutePath(path);
    free(path);
    if (!abspath) {
        free(contents);
        lua_pushstring(l, "unexpected vfs absolute path alloc fail");
        return lua_error(l);
    }
    char *newcontents = _prefix__file__(
        contents, abspath
    );
    free(contents); contents = newcontents;
    if (!contents) {
        lua_pushstring(l, "unexpected vfs absolute path alloc fail");
        return lua_error(l);
    }
    result = luaL_loadbuffer(
        l, contents, strlen(contents), lua_tostring(l, -1)
    );
    free(contents);
    if (result != 0) {
        lua_pushnil(l);
        lua_pushstring(l, "failed to parse module code");
        return 2;
    }
    return 1;
}

static int _vfs_readvfsfile(lua_State *l) {
    if (lua_gettop(l) < 1 ||
            lua_type(l, 1) != LUA_TSTRING) {
        return 0;
    }
    lua_settop(l, 1);
    char *path = vfs_NormalizePath(lua_tostring(l, 1));
    if (!path) {
        lua_pushstring(l, "alloc failure");
        return lua_error(l);
    }
    int result;
    if (!vfs_ExistsIgnoringCurrentDirectoryDiskAccess(
            path, &result
            )) {
        free(path);
        return 0;
    }
    if (!result || (vfs_IsDirectory(path, &result) && result)) {
        free(path);
        return 0;
    }
    uint64_t contentsize = 0;
    if (!vfs_Size(path, &contentsize)) {
        free(path);
        lua_pushstring(l, "unexpected failure in vfs_Size");
        return lua_error(l);
    }
    if (contentsize <= 0) {
        free(path);
        return 0;
    }
    char *contents = malloc(contentsize + 1);
    if (!contents) {
        free(path);
        return 0;
    }
    if (!vfs_GetBytes(path, 0, contentsize, contents)) {
        free(path);
        lua_pushstring(l, "unexpected failure in vfs_GetBytes");
        return lua_error(l);
    }
    contents[contentsize] = '\0';
    free(path);
    lua_pushstring(l, contents);
    free(contents);
    return 1;
}


void scriptcorefilesys_RegisterVFS(lua_State *l) {
    const int previous_stack = lua_gettop(l);
    lua_getglobal(l, "package");
    lua_pushstring(l, "searchers");
    lua_gettable(l, -2);
    assert(lua_gettop(l) == previous_stack + 2);
    int searchercount = lua_rawlen(l, -1);
    int insertindex = 2;
    if (insertindex > searchercount + 1)
        insertindex = searchercount + 1;
    assert(lua_type(l, -1) == LUA_TTABLE);
    int k = searchercount + 1;
    while (k > insertindex && k > 1) {
        // Stack: package.searchers (-1)
        lua_pushnumber(l, k - 1);
        assert(lua_type(l, -2) == LUA_TTABLE);
        assert(lua_type(l, -1) == LUA_TNUMBER);
        lua_gettable(l, -2);
        assert(lua_type(l, -1) == LUA_TFUNCTION);
        assert(lua_gettop(l) == previous_stack + 3);
        // Stack: package.searchers (-2), package.searchers[k - 1] (-1)
        lua_pushnumber(l, k);
        lua_pushvalue(l, -2);
        assert(lua_gettop(l) == previous_stack + 5);
        assert(lua_type(l, -1) == LUA_TFUNCTION);
        // Stack: package.searchers (-4), package.searchers[k - 1] (-3),
        //        k, package.searchers[k - 1] (-2, -1)
        lua_settable(l, -4);  // sets package.searchers slot k to k - 1 value
        assert(lua_gettop(l) == previous_stack + 3);
        // Stack: package.searchers (-2), package.searchers[k - 1] (-1)
        lua_pop(l, 1);
        assert(lua_gettop(l) == previous_stack + 2);
        assert(lua_type(l, -1) == LUA_TTABLE);
        k--;
    }
    lua_pushnumber(l, insertindex);
    lua_pushcfunction(l, _vfs_package_require_searcher);
    lua_settable(l, -3);
    lua_settop(l, previous_stack);
}

void scriptcorefilesys_AddFunctions(lua_State *l) {
    lua_pushcfunction(l, _vfs_adddatapak);
    lua_setglobal(l, "_vfs_adddatapak");
    lua_pushcfunction(l, _lfs_dir);
    lua_setglobal(l, "_lfs_dir");
    lua_pushcfunction(l, _lfs_isdir);
    lua_setglobal(l, "_lfs_isdir");
    lua_pushcfunction(l, _lfs_exists);
    lua_setglobal(l, "_lfs_exists");
    lua_pushcfunction(l, _lfs_getsize);
    lua_setglobal(l, "_lfs_getsize");
    lua_pushcfunction(l, _lfs_mkdir);
    lua_setglobal(l, "_lfs_mkdir");
    lua_pushcfunction(l, _lfs_datadir);
    lua_setglobal(l, "_lfs_datadir");
    lua_pushcfunction(l, _lfs_currentdir);
    lua_setglobal(l, "_lfs_currentdir");
    lua_pushcfunction(l, _vfs_lua_loadfile);
    lua_setglobal(l, "_vfs_lua_loadfile");
    lua_pushcfunction(l, _vfs_readvfsfile);
    lua_setglobal(l, "_vfs_readvfsfile");
    lua_pushcfunction(l, _lfs_basename);
    lua_setglobal(l, "_lfs_basename");
    lua_pushcfunction(l, _lfs_dirname);
    lua_setglobal(l, "_lfs_dirname");
}
