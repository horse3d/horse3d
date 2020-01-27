
#include <inttypes.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <math.h>
#include <mathc/mathc.h>
#include <stdint.h>
#include <string.h>

#include "scriptcore.h"
#include "scriptcoreerror.h"
#include "scriptcoreuiplane.h"
#include "uiobject.h"
#include "uiplane.h"


void lua_PushUiObjectRef(lua_State *l, int64_t objid) {
    scriptobjref *ref = lua_newuserdata(l, sizeof(*ref));
    memset(ref, 0, sizeof(*ref));
    ref->magic = OBJREFMAGIC;
    ref->type = OBJREF_UIOBJECT;
    ref->value = objid;
    luaL_newmetatable(l, "horse3d.uiplane.object");
    lua_setmetatable(l, -2);
    lua_pushstring(l, "horse3d_uiplane_object_set_metatableref");
    lua_gettable(l, LUA_REGISTRYINDEX);
    if (lua_type(l, -1) != LUA_TFUNCTION) {
        lua_pop(l, 1);
    } else {
        lua_pushvalue(l, -2);
        lua_call(l, 1, 0);
    }
}

void lua_PushUiPlaneRef(lua_State *l, int64_t objid) {
    scriptobjref *ref = lua_newuserdata(l, sizeof(*ref));
    memset(ref, 0, sizeof(*ref));
    ref->magic = OBJREFMAGIC;
    ref->type = OBJREF_UIPLANE;
    ref->value = objid;
    luaL_newmetatable(l, "horse3d.uiplane.plane");
    lua_setmetatable(l, -2);
    lua_pushstring(l, "horse3d_uiplane_plane_set_metatableref");
    lua_gettable(l, LUA_REGISTRYINDEX);
    if (lua_type(l, -1) != LUA_TFUNCTION) {
        lua_pop(l, 1);
    } else {
        lua_pushvalue(l, -2);
        lua_call(l, 1, 0);
    }
}

static int _uiobject_addnewsprite(lua_State *l) {
    if (lua_gettop(l) < 1 ||
            lua_type(l, 1) != LUA_TSTRING) {
        lua_pushstring(l, "expected arg of type string");
        return lua_error(l);
    }

    h3duiobject *obj = uiobject_CreateSprite(NULL, lua_tostring(l, 1));
    if (!obj) {
        lua_pushstring(l, "object creation failed, "
            "wrong sprite path or out of memory?");
        return lua_error(l);
    }

    lua_PushUiObjectRef(l, obj->id);
    return 1;
}

static int _uiplane_getdefaultplane(lua_State *l) {
    if (!defaultplane) {
        lua_pushnil(l);
        return lua_error(l);
    }

    lua_PushUiPlaneRef(l, defaultplane->id);
    return 1;
}

static int _uiplane_getbyid(lua_State *l) {
    if (lua_gettop(l) < 1 ||
            lua_type(l, 1) != LUA_TNUMBER) {
        lua_pushstring(l, "expected arg of type number");
        return lua_error(l);
    }

    int64_t id = lua_tointeger(l, 1);
    if (id < 0) {
        lua_pushstring(l, "id must be positive");
        return lua_error(l);
    }

    h3duiplane *plane = uiplane_GetPlaneById(id);
    if (!plane) {
        lua_pushnil(l);
        return 1;
    }

    lua_PushUiPlaneRef(l, plane->id);
    return 1;
}

static int _uiobject_destroy(lua_State *l) {
    if (lua_gettop(l) < 1 ||
            lua_type(l, 1) != LUA_TUSERDATA) {
        lua_pushstring(l, "expected arg of type "
                       "horsed.uiplane.object");
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_UIOBJECT) {
        lua_pushstring(l, "expected arg #1 to be horse3d.uiplane.object");
        return lua_error(l);
    }
    h3duiobject *obj = uiobject_GetObjectById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!obj) {
        lua_pushstring(
            l, "failed to get horse3d.uiplane.object - was it deleted?"
        );
        return lua_error(l);
    }

    uiobject_DestroyObject(obj);
    return 0;
}

static int _uiplane_destroy(lua_State *l) {
    if (lua_gettop(l) < 1 ||
            lua_type(l, 1) != LUA_TUSERDATA) {
        lua_pushstring(l, "expected arg of type "
                       "horsed.uiplane.plane");
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_UIPLANE) {
        lua_pushstring(l, "expected arg #1 to be horse3d.uiplane.plane");
        return lua_error(l);
    }
    h3duiplane *plane = uiplane_GetPlaneById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!plane) {
        lua_pushstring(
            l, "failed to get horse3d.uiplane.plane - was it deleted?"
        );
        return lua_error(l);
    }

    if (plane == defaultplane) {
        lua_pushstring(l, "cannot delete default plane");
        return lua_error(l);
    }

    uiplane_Destroy(plane);
    return 0;
}

static int _uiobject_getbyid(lua_State *l) {
    if (lua_gettop(l) < 1 ||
            lua_type(l, 1) != LUA_TNUMBER) {
        lua_pushstring(l, "expected arg of type number");
        return lua_error(l);
    }

    int64_t id = lua_tointeger(l, 1);
    if (id < 0) {
        lua_pushstring(l, "id must be positive");
        return lua_error(l);
    }

    h3duiobject *obj = uiobject_GetObjectById(id);
    if (!obj) {
        lua_pushnil(l);
        return 1;
    }

    lua_PushUiObjectRef(l, obj->id);
    return 1;
}

static int _uiobject_getobjectid(lua_State *l) {
    if (lua_gettop(l) < 1 ||
            lua_type(l, 1) != LUA_TUSERDATA) {
        lua_pushstring(l, "expected arg of type "
                       "horsed.uiplane.object");
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_UIOBJECT) {
        lua_pushstring(l, "expected arg #1 to be horse3d.uiplane.object");
        return lua_error(l);
    }
    h3duiobject *obj = uiobject_GetObjectById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!obj) {
        lua_pushstring(
            l, "failed to get horse3d.uiplane.object - was it deleted?"
        );
        return lua_error(l);
    }
    lua_pushinteger(l, obj->id);
    return 1;
}

static int _uiplane_addnewplane(lua_State *l) {
    if (lua_gettop(l) < 2 ||
            lua_type(l, 1) != LUA_TNUMBER ||
            lua_type(l, 2) != LUA_TNUMBER) {
        lua_pushstring(l, "expected args of types "
                       "number, number");
        return lua_error(l);
    }

    int64_t width = lua_tointeger(l, 1);
    int64_t height = lua_tointeger(l, 2);
    if (width < 0 || height < 0 ||
            width > 10000 || height > 10000) {
        lua_pushstring(l, "invalid dimensions");
        return lua_error(l);
    }

    h3duiplane *plane = uiplane_New();
    if (!plane) {
        lua_pushstring(l, "creation failed, out of memory?");
        return lua_error(l);
    }
    plane->width = width;
    plane->height = height;

    lua_PushUiPlaneRef(l, plane->id);
    return 1;
}

static int _uiplane_setoffset(lua_State *l) {
    if (lua_gettop(l) < 2 ||
            lua_type(l, 1) != LUA_TUSERDATA ||
            lua_type(l, 2) != LUA_TTABLE) {
        lua_pushstring(l, "expected args of type "
                       "horsed.uiplane.plane, table");
        return lua_error(l);
    }
    if (lua_rawlen(l, 2) < 2) {
        lua_pushstring(l, "offset table must have two values "
            "of type number");
        return lua_error(l);
    }

    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_UIPLANE) {
        lua_pushstring(l, "expected arg #1 to be horse3d.uiplane.plane");
        return lua_error(l);
    }
    h3duiplane *plane = uiplane_GetPlaneById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!plane) {
        lua_pushstring(
            l, "failed to get horse3d.uiplane.plane - was it deleted?"
        );
        return lua_error(l);
    }

    if (plane == defaultplane) {
        lua_pushstring(
            l, "cannot change offset of default plane, "
            "please add your own plane to do so"
        );
        return lua_error(l);
    }

    lua_pushnumber(l, 1);
    lua_gettable(l, 2);
    double width = lua_tonumber(l, -1);
    lua_pop(l, 1);
    lua_pushnumber(l, 2);
    lua_gettable(l, 2);
    double height = lua_tonumber(l, -1);
    lua_pop(l, 1);
    if (!plane->allow_fractional_positions) {
        width = round(width);
        height = round(height);
    }

    plane->offset_x = width;
    plane->offset_y = height;
    return 0;
}

static int _uiplane_getoffset(lua_State *l) {
    if (lua_gettop(l) < 1 ||
            lua_type(l, 1) != LUA_TUSERDATA) {
        lua_pushstring(l, "expected arg of type "
                       "horsed.uiplane.plane");
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_UIPLANE) {
        lua_pushstring(l, "expected arg #1 to be horse3d.uiplane.plane");
        return lua_error(l);
    }
    h3duiplane *plane = uiplane_GetPlaneById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!plane) {
        lua_pushstring(
            l, "failed to get horse3d.uiplane.plane - was it deleted?"
        );
        return lua_error(l);
    }

    lua_newtable(l);
    lua_pushnumber(l, 1);
    lua_pushnumber(l, plane->offset_x);
    lua_settable(l, -3);
    lua_pushnumber(l, 2);
    lua_pushnumber(l, plane->offset_y);
    lua_settable(l, -3);
    return 1;
}

static int _uiplane_resize(lua_State *l) {
    if (lua_gettop(l) < 2 ||
            lua_type(l, 1) != LUA_TUSERDATA ||
            lua_type(l, 2) != LUA_TTABLE) {
        lua_pushstring(l, "expected args of type "
                       "horsed.uiplane.plane, table");
        return lua_error(l);
    }
    if (lua_rawlen(l, 2) < 2) {
        lua_pushstring(l, "size table must have two values "
            "of type number");
        return lua_error(l);
    }

    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_UIPLANE) {
        lua_pushstring(l, "expected arg #1 to be horse3d.uiplane.plane");
        return lua_error(l);
    }
    h3duiplane *plane = uiplane_GetPlaneById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!plane) {
        lua_pushstring(
            l, "failed to get horse3d.uiplane.plane - was it deleted?"
        );
        return lua_error(l);
    }

    if (plane == defaultplane) {
        lua_pushstring(
            l, "cannot resize default plane which needs to remain "
            "screen sized, please add your own plane to do so"
        );
        return lua_error(l);
    }

    lua_pushnumber(l, 1);
    lua_gettable(l, 2);
    double width = lua_tonumber(l, -1);
    lua_pop(l, 1);
    lua_pushnumber(l, 2);
    lua_gettable(l, 2);
    double height = lua_tonumber(l, -1);
    lua_pop(l, 1);
    if (!plane->allow_fractional_positions) {
        width = round(width);
        height = round(height);
    }
    if (width < 0 || height < 0 ||
            width > 10000 || height > 10000) {
        lua_pushstring(l, "invalid dimensions");
        return lua_error(l);
    }

    plane->width = width;
    plane->height = height;
    return 0;
}

static int _uiplane_getplaneid(lua_State *l) {
    if (lua_gettop(l) < 1 ||
            lua_type(l, 1) != LUA_TUSERDATA) {
        lua_pushstring(l, "expected arg of type "
                       "horsed.uiplane.plane");
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_UIPLANE) {
        lua_pushstring(l, "expected arg #1 to be horse3d.uiplane.plane");
        return lua_error(l);
    }
    h3duiplane *plane = uiplane_GetPlaneById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!plane) {
        lua_pushstring(
            l, "failed to get horse3d.uiplane.plane - was it deleted?"
        );
        return lua_error(l);
    }
    lua_pushinteger(l, plane->id);
    return 1;
}

static int _uiobject_setposition(lua_State *l) {
    if (lua_gettop(l) < 2 ||
            lua_type(l, 1) != LUA_TUSERDATA ||
            lua_type(l, 2) != LUA_TTABLE) {
        lua_pushstring(l, "expected two args of types "
                       "horsed.uiplane.object, table");
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_UIOBJECT) {
        lua_pushstring(l, "expected arg #1 to be horse3d.uiplane.object");
        return lua_error(l);
    }
    h3duiobject *obj = uiobject_GetObjectById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!obj) {
        lua_pushstring(
            l, "failed to get horse3d.uiplane.object - was it deleted?"
        );
        return lua_error(l);
    }

    double pos[2];
    lua_pushnumber(l, 1);
    lua_gettable(l, 2);
    pos[0] = lua_tonumber(l, -1);
    lua_pushnumber(l, 2);
    lua_gettable(l, 2);
    pos[1] = lua_tonumber(l, -1);

    if (obj->parentplane &&
            !obj->parentplane->allow_fractional_positions) {
        pos[0] = round(pos[0]);
        pos[1] = round(pos[1]);
    }

    obj->x = pos[0];
    obj->y = pos[1];
    return 0;
}

static int _uiobject_getposition(lua_State *l) {
    if (lua_gettop(l) < 1 ||
            lua_type(l, 1) != LUA_TUSERDATA) {
        lua_pushstring(l, "expected arg of type "
                       "horsed.uiplane.object");
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_UIOBJECT) {
        lua_pushstring(l, "expected arg #1 to be horse3d.uiplane.object");
        return lua_error(l);
    }
    h3duiobject *obj = uiobject_GetObjectById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!obj) {
        lua_pushstring(
            l, "failed to get horse3d.uiplane.object - was it deleted?"
        );
        return lua_error(l);
    }

    lua_newtable(l);
    lua_pushnumber(l, 1);
    lua_pushnumber(l, obj->x);
    lua_settable(l, -3);
    lua_pushnumber(l, 2);
    lua_pushnumber(l, obj->y);
    lua_settable(l, -3);
    return 1;
}

static int _uiobject_addnewtext(lua_State *l) {
    if (lua_gettop(l) < 3 ||
            lua_type(l, 1) != LUA_TSTRING ||
            lua_type(l, 2) != LUA_TNUMBER ||
            lua_type(l, 3) != LUA_TSTRING) {
        lua_pushstring(l, "expected three args of types "
                       "text, number, text");
        return lua_error(l);
    }

    h3duiobject *obj = uiobject_CreateText(
        NULL, lua_tostring(l, 3), lua_tonumber(l, 2), lua_tostring(l, 1)
    );
    if (!obj) {
        lua_pushstring(l, "object creation failed, "
            "wrong font path or out of memory?");
        return lua_error(l);
    }

    lua_PushUiObjectRef(l, obj->id);
    return 1;
}

static int _uiobject_removefromparent(lua_State *l) {
    if (lua_gettop(l) < 1 ||
            lua_type(l, 1) != LUA_TUSERDATA) {
        lua_pushstring(l, "expected arg of types "
                       "horse3d.object.uiobject");
        return lua_error(l);
    }

    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_UIOBJECT) {
        lua_pushstring(l, "expected arg #1 to be horse3d.uiplane.object");
        return lua_error(l);
    }
    h3duiobject *obj = uiobject_GetObjectById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!obj) {
        lua_pushstring(
            l, "failed to get horse3d.uiplane.object - was it deleted?"
        );
        return lua_error(l);
    }

    uiobject_RemoveFromParent(obj);
    return 0;
}

static int _uiobject_addchild(lua_State *l) {
    if (lua_gettop(l) < 2 ||
            lua_type(l, 1) != LUA_TUSERDATA ||
            lua_type(l, 2) != LUA_TUSERDATA) {
        lua_pushstring(l, "expected two args of types "
                       "horse3d.object.uiobject, "
                       "horse3d.object.uiobject");
        return lua_error(l);
    }

    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_UIOBJECT) {
        lua_pushstring(l, "expected arg #1 to be horse3d.uiplane.object");
        return lua_error(l);
    }
    h3duiobject *obj = uiobject_GetObjectById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!obj) {
        lua_pushstring(
            l, "failed to get horse3d.uiplane.object - was it deleted?"
        );
        return lua_error(l);
    }

    if (((scriptobjref*)lua_touserdata(l, 2))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 2))->type !=
            OBJREF_UIOBJECT) {
        lua_pushstring(l, "expected arg #2 to be horse3d.uiplane.object");
        return lua_error(l);
    }
    h3duiobject *obj2 = uiobject_GetObjectById(
        ((scriptobjref*)lua_touserdata(l, 2))->value
    );
    if (!obj2) {
        lua_pushstring(
            l, "failed to get horse3d.uiplane.object - was it deleted?"
        );
        return lua_error(l);
    }

    char *error = NULL;
    if (!uiobject_AddChild(
            obj, obj2, &error
            )) {
        char buf[512];
        snprintf(buf, sizeof(buf) - 1,
            "failed to add child: unknown error "
            "(failed to allocate error maybe?)");
        if (error) {
            snprintf(buf, sizeof(buf) - 1,
                "failed to add child: %s",
                error
            );
            free(error);
        }
        lua_pushstring(l, buf);
        return lua_error(l);
    }
    return 0;
}

static int _uiobject_setnewfont(lua_State *l) {
    if (lua_gettop(l) < 2 ||
            lua_type(l, 1) != LUA_TUSERDATA ||
            lua_type(l, 2) != LUA_TSTRING) {
        lua_pushstring(l, "expected two args of types "
                       "horse3d.object.uiobject, text");
        return lua_error(l);
    }

    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_UIOBJECT) {
        lua_pushstring(l, "expected arg #1 to be horse3d.uiplane.object");
        return lua_error(l);
    }
    h3duiobject *obj = uiobject_GetObjectById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!obj) {
        lua_pushstring(
            l, "failed to get horse3d.uiplane.object - was it deleted?"
        );
        return lua_error(l);
    }
    if (obj->type != UIOBJECT_TYPE_TEXT) {
        lua_pushstring(l, "can only be used on text objects");
        return lua_error(l);
    }

    if (!uiobject_ChangeObjectFont(
            obj, lua_tostring(l, 2), obj->font.ptsize
            )) {
        lua_pushstring(l,
            "setting font failed - does the font exist, or "
            "are we out of memory?"
        );
        return lua_error(l);
    }
    return 0;
}

static int _uiobject_setnewptsize(lua_State *l) {
    if (lua_gettop(l) < 2 ||
            lua_type(l, 1) != LUA_TUSERDATA ||
            lua_type(l, 2) != LUA_TNUMBER) {
        lua_pushstring(l, "expected two args of types "
                       "horse3d.object.uiobject, number");
        return lua_error(l);
    }

    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_UIOBJECT) {
        lua_pushstring(l, "expected arg #1 to be horse3d.uiplane.object");
        return lua_error(l);
    }
    h3duiobject *obj = uiobject_GetObjectById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!obj) {
        lua_pushstring(
            l, "failed to get horse3d.uiplane.object - was it deleted?"
        );
        return lua_error(l);
    }
    if (obj->type != UIOBJECT_TYPE_TEXT) {
        lua_pushstring(l, "can only be used on text objects");
        return lua_error(l);
    }

    double new_pt_size = lua_tonumber(l, 2);
    if (new_pt_size <= 0) {
        lua_pushstring(l, "font size must be a positive number");
        return lua_error(l);
    }

    if (!uiobject_ChangeObjectFont(
            obj, obj->font.name, new_pt_size
            )) {
        lua_pushstring(l,
            "setting font size failed - are we out of memory?"
        );
        return lua_error(l);
    }
    return 0;
}


void scriptcoreuiplane_AddFunctions(lua_State *l) {
    lua_pushcfunction(l, _uiobject_addnewsprite);
    lua_setglobal(l, "_uiobject_addnewsprite");
    lua_pushcfunction(l, _uiobject_setposition);
    lua_setglobal(l, "_uiobject_setposition");
    lua_pushcfunction(l, _uiobject_getposition);
    lua_setglobal(l, "_uiobject_getposition");
    lua_pushcfunction(l, _uiobject_addnewtext);
    lua_setglobal(l, "_uiobject_addnewtext");
    lua_pushcfunction(l, _uiobject_setnewptsize);
    lua_setglobal(l, "_uiobject_setnewptsize");
    lua_pushcfunction(l, _uiobject_setnewfont);
    lua_setglobal(l, "_uiobject_setnewfont");
    lua_pushcfunction(l, _uiplane_getdefaultplane);
    lua_setglobal(l, "_uiplane_getdefaultplane");
    lua_pushcfunction(l, _uiobject_getbyid);
    lua_setglobal(l, "_uiobject_getbyid");
    lua_pushcfunction(l, _uiplane_getbyid);
    lua_setglobal(l, "_uiplane_getbyid");
    lua_pushcfunction(l, _uiobject_getobjectid);
    lua_setglobal(l, "_uiobject_getobjectid");
    lua_pushcfunction(l, _uiplane_getplaneid);
    lua_setglobal(l, "_uiplane_getplaneid");
    lua_pushcfunction(l, _uiplane_addnewplane);
    lua_setglobal(l, "_uiplane_addnewplane");
    lua_pushcfunction(l, _uiplane_resize);
    lua_setglobal(l, "_uiplane_resize");
    lua_pushcfunction(l, _uiplane_destroy);
    lua_setglobal(l, "_uiplane_destroy");
    lua_pushcfunction(l, _uiobject_destroy);
    lua_setglobal(l, "_uiobject_destroy");
    lua_pushcfunction(l, _uiplane_setoffset);
    lua_setglobal(l, "_uiplane_setoffset");
    lua_pushcfunction(l, _uiplane_getoffset);
    lua_setglobal(l, "_uiplane_getoffset");
    lua_pushcfunction(l, _uiobject_addchild);
    lua_setglobal(l, "_uiobject_addchild");
    lua_pushcfunction(l, _uiobject_removefromparent);
    lua_setglobal(l, "_uiobject_removefromparent");
}
